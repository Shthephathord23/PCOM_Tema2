#include "server_udp.h"
#include "server_topic.h" // For topicMatches
#include "common.h"       // For constants, send_all, error()
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>        // For memcpy, memset
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>       // For setprecision
#include <cmath>         // For pow
#include <algorithm>     // For std::min

// --- Internal Helper Declarations ---
static bool parse_raw_udp_datagram(const char* buffer, int bytes_received, UdpMessage& udp_msg);
static void distribute_message_to_subscribers(const UdpMessage& msg, const std::string& formatted_msg, SubscribersMap& subscribers);

// --- Public Function Implementation ---

void handle_udp_message(int udp_socket, SubscribersMap& subscribers) {
    char raw_recv_buffer[BUFFER_SIZE]; // Buffer for recvfrom
    UdpMessage parsed_udp_msg;
    struct sockaddr_in udp_sender_addr;
    socklen_t udp_sender_len = sizeof(udp_sender_addr);

    memset(raw_recv_buffer, 0, BUFFER_SIZE);
    int bytes_received = recvfrom(udp_socket, raw_recv_buffer, BUFFER_SIZE - 1, 0,
                                (struct sockaddr *) &udp_sender_addr, &udp_sender_len);

    if (bytes_received <= 0) {
        // Error or no data (shouldn't happen with blocking socket unless error)
        if (bytes_received < 0 && errno != EINTR) {
            perror("WARN: recvfrom UDP failed");
        }
        return; // Ignore datagram if error or empty
    }

    // Store sender address in the message struct
    parsed_udp_msg.sender_addr = udp_sender_addr;

    // Parse the raw bytes into the UdpMessage structure
    if (!parse_raw_udp_datagram(raw_recv_buffer, bytes_received, parsed_udp_msg)) {
        // Invalid datagram format, ignore it. No log message required by spec.
        return;
    }

    // Format the parsed message into the final string representation once
    std::string formatted_msg_str = format_parsed_udp_message(parsed_udp_msg);

    // Distribute the formatted message to relevant subscribers based on topic matching
    distribute_message_to_subscribers(parsed_udp_msg, formatted_msg_str, subscribers);
}

// --- Internal Helper Implementations ---

// Parses the raw UDP datagram into the UdpMessage struct.
// Returns true on success, false on format errors.
static bool parse_raw_udp_datagram(const char* buffer, int bytes_received, UdpMessage& udp_msg) {
    memset(&udp_msg, 0, sizeof(UdpMessage)); // Zero out the structure first for safety

    // 1. Extract Topic (max TOPIC_SIZE bytes)
    int topic_len_to_copy = std::min(bytes_received, TOPIC_SIZE);
    memcpy(udp_msg.topic, buffer, topic_len_to_copy);
    // Ensure null termination, handles case where topic is exactly TOPIC_SIZE
    udp_msg.topic[std::min(topic_len_to_copy, TOPIC_SIZE)] = '\0';

    // 2. Check minimum length (Topic + Type byte)
    if (bytes_received < (TOPIC_SIZE + 1)) {
        // Not enough data for even a 0-length topic + type byte.
        // Note: Server allows topics shorter than TOPIC_SIZE, but the format reserves
        // the space. The content starts *after* the reserved topic space.
        return false; // Invalid format
    }

    // 3. Extract Type Byte
    udp_msg.type = (uint8_t)buffer[TOPIC_SIZE];

    // 4. Calculate and Extract Content
    int content_offset = TOPIC_SIZE + 1;
    udp_msg.content_len = bytes_received - content_offset;

    if (udp_msg.content_len < 0) {
        // Should not happen if length check above passed, but defensive.
        udp_msg.content_len = 0;
    }

    // Copy content, ensuring not to overflow MAX_CONTENT_SIZE
    if (udp_msg.content_len > 0) {
        int content_len_to_copy = std::min(udp_msg.content_len, MAX_CONTENT_SIZE);
        memcpy(udp_msg.content, buffer + content_offset, content_len_to_copy);
        // Ensure null termination for the content buffer (useful for STRING type)
        udp_msg.content[std::min(content_len_to_copy, MAX_CONTENT_SIZE)] = '\0';
        // Update content_len to reflect the *actual* length stored (after potential truncation)
        udp_msg.content_len = content_len_to_copy;
    } else {
        // No content, ensure buffer is null-terminated
        udp_msg.content[0] = '\0';
        udp_msg.content_len = 0; // Explicitly set length to 0
    }

    // Basic validation passed
    return true;
}

// Formats the UdpMessage struct into the specified string format.
// This function's logic is derived from the original `parseMessage`.
// NOTE: This implementation is tightly coupled to the UdpMessage struct defined in server.h
std::string format_parsed_udp_message(const UdpMessage& msg) {
    std::stringstream result_ss;
    char sender_ip[INET_ADDRSTRLEN];

    // Add Sender IP:Port
    inet_ntop(AF_INET, &(msg.sender_addr.sin_addr), sender_ip, INET_ADDRSTRLEN);
    result_ss << sender_ip << ":" << ntohs(msg.sender_addr.sin_port) << " - ";

    // Add Topic Name
    result_ss << msg.topic << " - "; // msg.topic is already null-terminated

    // Add Type Name and Value based on Type field
    switch(msg.type) {
        case 0: { // INT
            result_ss << "INT - ";
            // Requires 1 byte sign + 4 bytes value = 5 bytes
            if (msg.content_len < 5) {
                result_ss << "INVALID DATA"; // Or handle error as needed
            } else {
                uint8_t sign = msg.content[0];
                uint32_t net_val;
                memcpy(&net_val, msg.content + 1, 4);
                int32_t val = ntohl(net_val); // Use int32_t for clarity
                if (sign == 1) {
                    val = -val;
                } else if (sign != 0) {
                    result_ss << "INVALID SIGN BYTE"; // Handle invalid sign
                    break; // Exit case
                }
                result_ss << val;
            }
            break;
        }
        case 1: { // SHORT_REAL
            result_ss << "SHORT_REAL - ";
            // Requires 2 bytes value
            if (msg.content_len < 2) {
                result_ss << "INVALID DATA";
            } else {
                uint16_t net_val;
                memcpy(&net_val, msg.content, 2);
                float val = ntohs(net_val) / 100.0f;
                // Format to exactly two decimal places
                result_ss << std::fixed << std::setprecision(2) << val;
                result_ss.unsetf(std::ios_base::floatfield); // Reset flags if needed elsewhere
            }
            break;
        }
        case 2: { // FLOAT
            result_ss << "FLOAT - ";
            // Requires 1 byte sign + 4 bytes value + 1 byte power = 6 bytes
            if (msg.content_len < 6) {
                 result_ss << "INVALID DATA";
            } else {
                uint8_t sign = msg.content[0];
                uint32_t net_val;
                memcpy(&net_val, msg.content + 1, 4);
                uint8_t power = msg.content[5];
                double val = ntohl(net_val); // Use double for calculation

                // Apply negative power of 10 divisor safely
                if (power > 0) {
                    // Using pow is generally safe for reasonable exponent ranges
                    val *= pow(10.0, -static_cast<double>(power));
                }

                if (sign == 1) {
                    val = -val;
                } else if (sign != 0) {
                    result_ss << "INVALID SIGN BYTE";
                    break; // Exit case
                }
                 // Format with precision determined by the power byte
                 // Using default double precision might be simpler if exact trailing zeros aren't critical
                 // result_ss << val; // Simpler default precision
                 // Or match the original logic:
                 result_ss << std::fixed << std::setprecision(power) << val;
                 result_ss.unsetf(std::ios_base::floatfield); // Reset flags
            }
            break;
        }
        case 3: { // STRING
            result_ss << "STRING - ";
            // Content is already null-terminated within msg.content buffer
            // by parse_raw_udp_datagram, up to MAX_CONTENT_SIZE.
            result_ss << msg.content;
            break;
        }
        default:
            result_ss << "UNKNOWN TYPE (" << static_cast<int>(msg.type) << ")";
            // Optionally include raw content dump for unknown types if useful
    }

    return result_ss.str();
}


// Sends the formatted message to subscribers matching the topic, respects SF flag.
static void distribute_message_to_subscribers(const UdpMessage& msg, const std::string& formatted_msg, SubscribersMap& subscribers) {
    std::string topic_str(msg.topic); // Use std::string for matching

    // Iterate through all registered subscribers
    for (auto it = subscribers.begin(); it != subscribers.end(); ++it) {
        Subscriber& sub = it->second;
        bool match_found_for_subscriber = false;

        // Iterate through the topics this subscriber is interested in
        for (const auto& topic_pair : sub.topics) {
            const std::string& pattern = topic_pair.first;
            bool sf_enabled = topic_pair.second;

            // Check if the message topic matches the subscriber's pattern
            if (topicMatches(topic_str, pattern)) {
                match_found_for_subscriber = true; // Mark that a match occurred

                if (sub.connected) {
                    // Client is connected, attempt to send immediately
                    // Send message + null terminator
                    ssize_t sent = send_all(sub.socket, formatted_msg.c_str(), formatted_msg.length() + 1, MSG_NOSIGNAL);
                    if (sent < 0 || (size_t)sent != formatted_msg.length() + 1) {
                         // Error sending (e.g., client disconnected between poll and send)
                         // Log warning only for unexpected errors
                         if (errno != EPIPE && errno != ECONNRESET) {
                             perror("WARN: send_all to subscriber failed");
                         }
                         // Don't modify subscriber state here; let the main loop handle
                         // the disconnection detected by poll or future sends.
                    }
                } else if (sf_enabled) {
                    // Client is disconnected, but Store-and-Forward is enabled for this topic
                    sub.stored_messages.push_back(formatted_msg);
                }
                // else: Client disconnected and SF=0, so message is dropped for this subscriber.

                // IMPORTANT: A subscriber receives the message AT MOST ONCE per UDP datagram,
                // even if multiple patterns match. Break from the inner loop (topic patterns)
                // once the first match is found and handled for this subscriber.
                break;
            }
        } // End inner loop (subscriber's topic patterns)
        // Continue to the next subscriber
    } // End outer loop (all subscribers)
}