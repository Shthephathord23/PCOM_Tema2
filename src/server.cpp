#include "server.h"
#include <algorithm> // For std::min
#include <cstdio>   // For setvbuf
#include <cstdlib>  // For atoi, exit
#include <csignal>  // For signal handling (related to EINTR)

// --- Function Implementations ---

// (topicMatches implementation remains the same as in the original file)
bool topicMatches(const std::string& topic, const std::string& pattern) {
    // Split pattern and topic by '/'
    std::vector<std::string> p_segs, t_segs;
    std::string segment;
    std::stringstream ss_p(pattern);
    while (getline(ss_p, segment, '/')) p_segs.push_back(segment);
    std::stringstream ss_t(topic);
    while (getline(ss_t, segment, '/')) t_segs.push_back(segment);

    size_t p_idx = 0, t_idx = 0;
    while (p_idx < p_segs.size() && t_idx < t_segs.size()) {
        if (p_segs[p_idx] == "+") {
            // '+' matches exactly one segment
            p_idx++;
            t_idx++;
        } else if (p_segs[p_idx] == "*") {
            // '*' matches zero or more segments
            p_idx++;
            if (p_idx == p_segs.size()) return true; // '*' at end matches rest

            // Try matching remaining pattern against remaining topic segments
            while (t_idx < t_segs.size()) {
                 // Construct remaining topic/pattern strings for recursive-like check
                 std::string remaining_topic = "";
                 for(size_t i = t_idx; i < t_segs.size(); ++i) remaining_topic += (i > t_idx ? "/" : "") + t_segs[i];
                 std::string remaining_pattern = "";
                 for(size_t i = p_idx; i < p_segs.size(); ++i) remaining_pattern += (i > p_idx ? "/" : "") + p_segs[i];

                 if (topicMatches(remaining_topic, remaining_pattern)) {
                     return true;
                 }
                 t_idx++; // '*' consumes one segment, try again
            }
            return false; // Ran out of topic segments to match remaining pattern
        } else {
            // Segments must match exactly
            if (p_segs[p_idx] != t_segs[t_idx]) return false;
            p_idx++;
            t_idx++;
        }
    }

    // Handle trailing '*' matching zero segments
    if (t_idx == t_segs.size() && p_idx < p_segs.size() && p_segs[p_idx] == "*" && p_idx == p_segs.size() - 1) {
        p_idx++;
    }

    // Both pattern and topic must be fully consumed for a match
    return p_idx == p_segs.size() && t_idx == t_segs.size();
}


// (parseMessage implementation remains the same as in the original file)
std::string UdpMessage::parseMessage()
{
    std::stringstream result_ss; // Use stringstream for easier formatting

    // Add IP and port of sender
    char sender_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(this->sender_addr.sin_addr), sender_ip, INET_ADDRSTRLEN);
    result_ss << sender_ip << ":" << ntohs(this->sender_addr.sin_port) << " - ";

    // Add topic
    result_ss << this->topic << " - ";

    // Parse content based on type
    switch(this->type) {
        case 0: {  // INT
            if (this->content_len < 5) { // Need 1 byte sign + 4 bytes value
                 result_ss << "INT - INVALID DATA";
                 break;
            }
            uint8_t sign_byte = this->content[0];
            uint32_t net_value;
            memcpy(&net_value, this->content + 1, sizeof(uint32_t)); // Copy value after sign byte
            int value = ntohl(net_value); // Convert from network byte order
            if (sign_byte == 1) {
                value = -value; // Apply sign
            } else if (sign_byte != 0) {
                result_ss << "INT - INVALID SIGN BYTE";
                break;
            }
            result_ss << "INT - " << value;
            break;
        }
        case 1: {  // SHORT_REAL
             if (this->content_len < 2) { // Need 2 bytes value
                 result_ss << "SHORT_REAL - INVALID DATA";
                 break;
            }
            uint16_t net_value;
            memcpy(&net_value, this->content, sizeof(uint16_t));
            float value = ntohs(net_value) / 100.0f; // Convert and scale
            // Format to two decimal places, ensuring trailing zeros if needed
            result_ss << "SHORT_REAL - " << std::fixed << std::setprecision(2) << value;
            result_ss.unsetf(std::ios_base::floatfield); // Reset float formatting
            break;
        }
        case 2: {  // FLOAT
             if (this->content_len < 6) { // Need 1 byte sign + 4 bytes value + 1 byte power
                 result_ss << "FLOAT - INVALID DATA";
                 break;
             }
             uint8_t sign_byte = this->content[0];
             uint32_t net_value;
             memcpy(&net_value, this->content + 1, sizeof(uint32_t));
             uint8_t power_byte = this->content[5];

             double value = ntohl(net_value);
             if (power_byte > 0) {
                 value *= pow(10.0, -static_cast<double>(power_byte));
             }

             if (sign_byte == 1) {
                 value = -value;
             } else if (sign_byte != 0) {
                 result_ss << "FLOAT - INVALID SIGN BYTE";
                 break;
             }
             // Print exactly power_byte decimals
             result_ss << "FLOAT - "
                       << std::fixed << std::setprecision(power_byte)
                       << value;
             // Reset floatfield so it doesn't leak into next prints
             result_ss.unsetf(std::ios_base::floatfield);
             break;
        }
        case 3: {  // STRING
            char string_content[MAX_CONTENT_SIZE + 1];
            int len_to_copy = std::min(this->content_len, MAX_CONTENT_SIZE);
            memcpy(string_content, this->content, len_to_copy);
            string_content[len_to_copy] = '\0';
            result_ss << "STRING - " << string_content;
            break;
        }
        default:
            result_ss << "UNKNOWN TYPE (" << (int)this->type << ")";
    }

    return result_ss.str();
}

static void handle_stdin(bool& running);
static void handle_new_connection(int tcp_socket,
                                  std::vector<struct pollfd>& poll_fds,
                                  std::map<std::string, Subscriber>& subscribers,
                                  std::map<int, std::string>& socket_to_id);
static void handle_udp_message(int udp_socket,
                               std::map<std::string, Subscriber>& subscribers);
static void handle_client_activity(std::vector<struct pollfd>& poll_fds,
                                   std::map<std::string, Subscriber>& subscribers,
                                   std::map<int, std::string>& socket_to_id);

// --- Main Server Logic ---
int main(int argc, char *argv[]) {
    // Disable stdout buffering for immediate output
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <PORT>" << std::endl;
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) { // Basic port validation
         std::cerr << "ERROR: Invalid port number." << std::endl;
         return 1;
    }

    // --- Socket Creation and Setup ---
    int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket < 0) error("ERROR opening TCP socket");

    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0) error("ERROR opening UDP socket");

    // Allow address reuse for quicker restarts
    int enable = 1;
    if (setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        error("ERROR setting SO_REUSEADDR on TCP");
    if (setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        error("ERROR setting SO_REUSEADDR on UDP");

    // Server address configuration
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces

    // Bind sockets
    if (bind(tcp_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0)
        error("ERROR binding TCP socket");
    if (bind(udp_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0)
        error("ERROR binding UDP socket");

    // Listen for TCP connections
    if (listen(tcp_socket, MAX_CLIENTS) < 0) error("ERROR on listen");

    std::cerr << "Server started on port " << port << std::endl;

    // --- Data Structures ---
    std::map<std::string, Subscriber> subscribers; // Map ID to Subscriber struct
    std::vector<struct pollfd> poll_fds;           // File descriptors for polling
    std::map<int, std::string> socket_to_id;       // Map socket fd to client ID

    // Add initial sockets to poll set
    poll_fds.push_back({tcp_socket, POLLIN, 0});   // TCP listening [index 0]
    poll_fds.push_back({udp_socket, POLLIN, 0});   // UDP socket [index 1]
    poll_fds.push_back({STDIN_FILENO, POLLIN, 0}); // Standard input [index 2]

    bool running = true;

    // --- Main Server Loop ---
    while (running) {
        // Wait for events
        int poll_count = poll(poll_fds.data(), poll_fds.size(), -1);
        if (poll_count < 0) {
            if (errno == EINTR) continue; // Interrupted by signal
            error("ERROR on poll");
        }

        // Check STDIN [index 2]
        if (poll_fds[2].revents & POLLIN) {
            handle_stdin(running);
            if (!running) break; // Exit immediately if requested
        }

        // Check TCP listener [index 0]
        if (poll_fds[0].revents & POLLIN) {
            handle_new_connection(tcp_socket, poll_fds, subscribers, socket_to_id);
        }

        // Check UDP socket [index 1]
        if (poll_fds[1].revents & POLLIN) {
            handle_udp_message(udp_socket, subscribers);
        }

        // Check client TCP sockets [indices >= 3]
        handle_client_activity(poll_fds, subscribers, socket_to_id);

        // Clean up revents for the next iteration (important!)
        for (auto& pfd_entry : poll_fds) {
            pfd_entry.revents = 0;
        }
    } // End main while loop (running)

    // --- Server Shutdown ---
    // Close all client sockets first (indices >= 3)
    for (size_t i = 3; i < poll_fds.size(); ++i) {
         close(poll_fds[i].fd);
    }
    // Close listening/UDP sockets
    close(tcp_socket);
    close(udp_socket);

    // Optional: Message indicating shutdown completion
    // std::cout << "Server shut down complete." << std::endl;

    return 0;
}

// --- Helper Function Implementations ---

static void handle_stdin(bool& running) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    if (fgets(buffer, BUFFER_SIZE - 1, stdin) != NULL) {
        buffer[strcspn(buffer, "\n")] = 0; // Remove trailing newline
        if (strcmp(buffer, "exit") == 0) {
            running = false; // Signal shutdown
        } else {
            // Unknown command (optional: log)
            // std::cerr << "Unknown command on server stdin: " << buffer << std::endl;
        }
    } else {
        // EOF or error on stdin
        // std::cerr << "WARN: STDIN closed or error." << std::endl;
        running = false; // Treat as exit signal
    }
}

static void handle_new_connection(int tcp_socket,
                                  std::vector<struct pollfd>& poll_fds,
                                  std::map<std::string, Subscriber>& subscribers,
                                  std::map<int, std::string>& socket_to_id)
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_socket = accept(tcp_socket, (struct sockaddr *) &client_addr, &client_len);

    if (client_socket < 0) {
         perror("WARN: accept failed");
         return;
    }

    // Disable Nagle's algorithm
    int flag = 1;
    if (setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) < 0)
        perror("WARN: setsockopt TCP_NODELAY failed");

    // Receive client ID
    char buffer[BUFFER_SIZE]; // Use common buffer size
    memset(buffer, 0, BUFFER_SIZE);
    int bytes_received = recv(client_socket, buffer, MAX_ID_SIZE + 1, 0); // Read ID + potential null

    if (bytes_received <= 0) {
        // std::cerr << "WARN: Failed to receive client ID or client disconnected." << std::endl;
        close(client_socket);
        return;
    }

    buffer[std::min(bytes_received, MAX_ID_SIZE)] = '\0'; // Ensure null termination within limits
    std::string client_id_str(buffer);

    // --- Client Connection Logic ---
    auto it = subscribers.find(client_id_str);
    if (it != subscribers.end()) { // ID exists
        if (it->second.connected) { // Already connected?
            std::cout << "Client " << client_id_str << " already connected." << std::endl;
            // Send error/notification? Spec doesn't require, just close duplicate.
            close(client_socket);
        } else { // Reconnection
            char client_ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
            std::cout << "New client " << client_id_str << " connected from "
                      << client_ip_str << ":" << ntohs(client_addr.sin_port) << "." << std::endl;

            it->second.socket = client_socket;
            it->second.connected = true;
            it->second.command_buffer.clear();
            poll_fds.push_back({client_socket, POLLIN, 0}); // Add to poll set
            socket_to_id[client_socket] = client_id_str; // Update map

            // Send stored messages (SF)
            for (const std::string& stored_msg : it->second.stored_messages) {
                // Use send_all for robustness (although original used send)
                // Add 1 for the null terminator expected by the client
                ssize_t sent = send_all(client_socket, stored_msg.c_str(), stored_msg.length() + 1, MSG_NOSIGNAL);
                if (sent < 0 || (size_t)sent != stored_msg.length() + 1) {
                    // Error sending, likely client disconnected during send burst
                    perror("WARN: send stored message failed during reconnect");
                    // Client disconnect will be handled by poll loop, stop sending more
                    break;
                }
            }
            it->second.stored_messages.clear(); // Clear after attempting send
        }
    } else { // New client ID
        char client_ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
        std::cout << "New client " << client_id_str << " connected from "
                  << client_ip_str << ":" << ntohs(client_addr.sin_port) << "." << std::endl;

        Subscriber new_sub;
        strncpy(new_sub.id, client_id_str.c_str(), MAX_ID_SIZE);
        new_sub.id[MAX_ID_SIZE] = '\0'; // Ensure null termination
        new_sub.socket = client_socket;
        new_sub.connected = true;
        subscribers[client_id_str] = new_sub; // Add to map
        poll_fds.push_back({client_socket, POLLIN, 0}); // Add to poll set
        socket_to_id[client_socket] = client_id_str; // Add to socket->ID map
    }
}


static void handle_udp_message(int udp_socket,
                               std::map<std::string, Subscriber>& subscribers)
{
    char buffer[BUFFER_SIZE]; // Use common buffer size
    UdpMessage udp_msg;
    struct sockaddr_in udp_sender_addr;
    socklen_t udp_sender_len = sizeof(udp_sender_addr);

    memset(buffer, 0, BUFFER_SIZE);
    int bytes_received = recvfrom(udp_socket, buffer, BUFFER_SIZE - 1, 0,
                                (struct sockaddr *) &udp_sender_addr, &udp_sender_len);

    if (bytes_received <= 0) {
        if (bytes_received < 0) perror("WARN: recvfrom UDP failed");
        return; // Nothing received or error
    }

    // Basic validation: must have topic (min 1 char) + type byte + content (min 0)
    // The UDP format is topic[50] + type[1] + content[1500]
    if (bytes_received < (TOPIC_SIZE + 1)) {
        // std::cerr << "WARN: Received UDP message too short (less than topic+type)." << std::endl;
        // The original code had a similar check but allowed processing. Let's stick to that.
        // However, ensure we don't read past the received bytes.
    }

    // Populate UdpMessage struct
    memset(&udp_msg, 0, sizeof(UdpMessage));
    // Copy topic carefully, ensuring null termination within TOPIC_SIZE
    int topic_len_to_copy = std::min(bytes_received, TOPIC_SIZE);
    memcpy(udp_msg.topic, buffer, topic_len_to_copy);
    udp_msg.topic[topic_len_to_copy < TOPIC_SIZE ? topic_len_to_copy : TOPIC_SIZE] = '\0'; // Ensure null term

    // Check if we have enough bytes for the type
    if (bytes_received < TOPIC_SIZE + 1) {
         // std::cerr << "WARN: UDP message too short to contain type byte." << std::endl;
         return; // Cannot process further
    }

    udp_msg.type = (uint8_t)buffer[TOPIC_SIZE];
    udp_msg.sender_addr = udp_sender_addr;
    udp_msg.content_len = bytes_received - (TOPIC_SIZE + 1);

    // Copy content only if content_len is positive and valid
    if (udp_msg.content_len > 0) {
        int len_to_copy = std::min(udp_msg.content_len, MAX_CONTENT_SIZE);
        memcpy(udp_msg.content, buffer + TOPIC_SIZE + 1, len_to_copy);
        // Ensure null termination for string safety, although binary data might not need it
        udp_msg.content[std::min(len_to_copy, MAX_CONTENT_SIZE)] = '\0';
    } else {
        udp_msg.content_len = 0;
        udp_msg.content[0] = '\0';
    }

    // Format the message string once
    std::string formatted_msg_str = udp_msg.parseMessage();
    std::string topic_str(udp_msg.topic);

    // --- Distribute message to relevant subscribers ---
    for (auto& pair : subscribers) {
        Subscriber& sub = pair.second;
        // Find if any subscription matches
        for (const auto& topic_pair : sub.topics) {
            const std::string& pattern = topic_pair.first;
            bool sf_enabled = topic_pair.second;

            if (topicMatches(topic_str, pattern)) {
                if (sub.connected) { // Send if connected
                    // Send message + null terminator
                    // Use send_all for robustness, though original used send
                    ssize_t sent = send_all(sub.socket, formatted_msg_str.c_str(), formatted_msg_str.length() + 1, MSG_NOSIGNAL);
                     if (sent < 0 || (size_t)sent != formatted_msg_str.length() + 1) {
                        // Error sending, probably disconnected
                        if (errno != EPIPE && errno != ECONNRESET) { // EPIPE/ECONNRESET common on disconnect
                            perror("WARN: send_all to subscriber failed");
                        }
                        // Let the main poll loop handle the disconnect detection
                    }
                } else if (sf_enabled) { // Store if SF=1 and disconnected
                    sub.stored_messages.push_back(formatted_msg_str);
                }
                // else: Matched, but disconnected and SF=0 -> Message dropped for this sub

                // Optimization: Once a matching pattern is found for a subscriber,
                // send/store/drop it and move to the next subscriber.
                // A subscriber receives a message at most once per UDP datagram.
                goto next_subscriber; // Use goto for clarity in breaking outer loop
            }
        } // End loop through subscriber's topics
        next_subscriber:; // Label for goto
    } // End loop through subscribers
}


static void handle_client_activity(std::vector<struct pollfd>& poll_fds,
                                   std::map<std::string, Subscriber>& subscribers,
                                   std::map<int, std::string>& socket_to_id)
{
    char buffer[BUFFER_SIZE]; // Reusable buffer

    // Iterate backwards to allow safe removal from poll_fds using erase
    for (int i = poll_fds.size() - 1; i >= 3; --i) {
        // Check if index is still valid after potential erasures
        if (i >= (int)poll_fds.size()) continue;

        struct pollfd& pfd = poll_fds[i];
        int client_socket = pfd.fd;

        // Skip if no event for this fd in this poll cycle
        if (pfd.revents == 0) continue;

        // Find the subscriber associated with this socket
        auto id_it = socket_to_id.find(client_socket);
        Subscriber* sub_ptr = nullptr;
        std::string client_id_str = "";
        if (id_it != socket_to_id.end()) {
            client_id_str = id_it->second;
            auto sub_it = subscribers.find(client_id_str);
            if (sub_it != subscribers.end()) {
                sub_ptr = &sub_it->second;
            }
        }

        bool client_disconnected = false;

        if (!sub_ptr) {
            client_disconnected = true; // Treat as error, close it
            goto cleanup; // Skip to cleanup
        }

        // Handle disconnection/error first
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
             if (sub_ptr && sub_ptr->connected) { // Check if already marked disconnected
                std::cout << "Client " << client_id_str << " disconnected (poll error/hup)." << std::endl;
                sub_ptr->connected = false;
                sub_ptr->socket = -1;
                sub_ptr->command_buffer.clear();
            } else if (!sub_ptr) {
                 // std::cerr << "WARN: Poll error/hangup on unknown client socket fd " << client_socket << std::endl;
            }
            client_disconnected = true;
        }
        // Check for incoming data
        else if (pfd.revents & POLLIN) {
            memset(buffer, 0, BUFFER_SIZE);
            int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);

            if (bytes_received <= 0) { // Disconnection or error
                if (bytes_received < 0 && errno != ECONNRESET) { // ECONNRESET is common for disconnect
                     perror("WARN: recv from client failed");
                }

                if (sub_ptr && sub_ptr->connected) {
                    std::cout << "Client " << client_id_str << " disconnected." << std::endl;
                    sub_ptr->connected = false;
                    sub_ptr->socket = -1;
                    sub_ptr->command_buffer.clear();
                } else if (!sub_ptr) {
                    // std::cerr << "WARN: Disconnection from unknown socket fd " << client_socket << std::endl;
                }
                client_disconnected = true;

            } else { // Received data from client
                if (sub_ptr == nullptr) {
                    // std::cerr << "WARN: Received data from unknown socket fd " << client_socket << std::endl;
                    client_disconnected = true; // Treat as error, close it
                } else {
                    // Append received data to the client's buffer
                    sub_ptr->command_buffer.append(buffer, bytes_received);

                    // Process complete commands (delimited by newline) from the buffer
                    size_t newline_pos;
                    while ((newline_pos = sub_ptr->command_buffer.find('\n')) != std::string::npos) {
                        std::string command_line = sub_ptr->command_buffer.substr(0, newline_pos);
                        sub_ptr->command_buffer.erase(0, newline_pos + 1);

                        // Trim whitespace (optional but robust)
                        command_line.erase(0, command_line.find_first_not_of(" \t\r\n"));
                        command_line.erase(command_line.find_last_not_of(" \t\r\n") + 1);

                        if (command_line.empty()) continue;

                        // Parse command
                        std::stringstream ss(command_line);
                        std::string command_verb;
                        ss >> command_verb;

                        if (command_verb == "subscribe") {
                            std::string topic;
                            int sf = -1;
                            // Format expected: subscribe <TOPIC> <SF>
                            if (ss >> topic >> sf && (sf == 0 || sf == 1) && ss.eof()) {
                                if (topic.length() > TOPIC_SIZE) {
                                    // std::cerr << "WARN: Client " << client_id_str << " oversized topic subscribe." << std::endl;
                                } else {
                                    sub_ptr->topics[topic] = (sf == 1);
                                    // Client prints confirmation
                                }
                            } else {
                                 // std::cerr << "WARN: Client " << client_id_str << " invalid subscribe format: " << command_line << std::endl;
                            }
                        } else if (command_verb == "unsubscribe") {
                            std::string topic;
                            // Format expected: unsubscribe <TOPIC>
                            if (ss >> topic && ss.eof()) {
                                if (topic.length() > TOPIC_SIZE) {
                                    // std::cerr << "WARN: Client " << client_id_str << " oversized topic unsubscribe." << std::endl;
                                } else {
                                    if (sub_ptr->topics.erase(topic) == 0) {
                                         // std::cerr << "WARN: Client " << client_id_str << " unsubscribe non-subscribed topic: " << topic << std::endl;
                                    }
                                    // Client prints confirmation
                                }
                            } else {
                                // std::cerr << "WARN: Client " << client_id_str << " invalid unsubscribe format: " << command_line << std::endl;
                            }
                        } else {
                             // std::cerr << "WARN: Client " << client_id_str << " unknown command: " << command_verb << std::endl;
                        }
                    } // End while processing commands from buffer
                } // End if sub_ptr != nullptr
            } // End handling received data
        } // End handling POLLIN

        cleanup:;    // Cleanup label for goto
        // Cleanup if client disconnected or error occurred
        if (client_disconnected) {
            close(client_socket);
            if (id_it != socket_to_id.end()) socket_to_id.erase(id_it);
            poll_fds.erase(poll_fds.begin() + i);
            // Continue to next fd index (loop handles index decrement)
        }

    } // End loop through client sockets
}
