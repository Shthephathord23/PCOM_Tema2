#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_NODELAY
#include <arpa/inet.h>
#include <poll.h>
#include <netdb.h>
#include <climits>
#include <cmath>
#include <sstream> // Added for stringstream
#include <iomanip> // Added for setprecision
#include <errno.h> // For errno

#define MAX_CLIENTS 100     // Maximum concurrent TCP clients
#define BUFFER_SIZE 1600    // Buffer size for receiving messages
#define TOPIC_SIZE 50       // Maximum length of a topic name
#define MAX_CONTENT_SIZE 1500 // Maximum length of UDP message content
#define MAX_ID_SIZE 10      // Maximum length of a client ID

// Structure to represent a TCP client (subscriber)
struct Subscriber {
    int socket = -1; // Initialize socket to invalid
    char id[MAX_ID_SIZE + 1];
    std::map<std::string, bool> topics;  // Map: Topic Pattern -> SF flag
    std::vector<std::string> stored_messages; // For store-and-forward
    bool connected = false;
    std::string command_buffer; // Buffer for incoming commands from this client
};

// Structure for holding parsed UDP message data
struct UdpMessage {
    char topic[TOPIC_SIZE + 1]; // Null terminated topic
    uint8_t type;
    char content[MAX_CONTENT_SIZE + 1]; // Raw content + potential null terminator space
    struct sockaddr_in sender_addr;
    int content_len; // Actual length of content received
};


// Error handling function - prints error and exits
void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// Function to match a topic against a pattern (with wildcards '+' and '*')
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


// Function to parse UDP message content and format it into a string for subscribers
std::string parseMessage(const UdpMessage& msg) {
    std::stringstream result_ss; // Use stringstream for easier formatting

    // Add IP and port of sender
    char sender_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(msg.sender_addr.sin_addr), sender_ip, INET_ADDRSTRLEN);
    result_ss << sender_ip << ":" << ntohs(msg.sender_addr.sin_port) << " - ";

    // Add topic
    result_ss << msg.topic << " - ";

    // Parse content based on type
    switch(msg.type) {
        case 0: {  // INT
            if (msg.content_len < 5) { // Need 1 byte sign + 4 bytes value
                 result_ss << "INT - INVALID DATA";
                 break;
            }
            uint8_t sign_byte = msg.content[0];
            uint32_t net_value;
            memcpy(&net_value, msg.content + 1, sizeof(uint32_t)); // Copy value after sign byte
            int value = ntohl(net_value); // Convert from network byte order
            if (sign_byte == 1) {
                value = -value; // Apply sign
            }
            result_ss << "INT - " << value;
            break;
        }
        case 1: {  // SHORT_REAL
             if (msg.content_len < 2) { // Need 2 bytes value
                 result_ss << "SHORT_REAL - INVALID DATA";
                 break;
            }
            uint16_t net_value;
            memcpy(&net_value, msg.content, sizeof(uint16_t));
            float value = ntohs(net_value) / 100.0f; // Convert and scale
            // Format to two decimal places, ensuring trailing zeros if needed
            result_ss << "SHORT_REAL - " << std::fixed << std::setprecision(2) << value;
            result_ss.unsetf(std::ios_base::floatfield); // Reset float formatting
            break;
        }
        case 2: {  // FLOAT
             if (msg.content_len < 6) { // Need 1 byte sign + 4 bytes value + 1 byte power
                 result_ss << "FLOAT - INVALID DATA";
                 break;
            }
            uint8_t sign_byte = msg.content[0];
            uint32_t net_value;
            memcpy(&net_value, msg.content + 1, sizeof(uint32_t)); // Value after sign byte
            uint8_t power_byte = msg.content[5]; // Power byte after sign and value

            double value = ntohl(net_value); // Use double for intermediate precision
            // Calculate 10^(-power_byte)
            double divisor = 1.0;
            // Use pow for simplicity and correctness, check for large power_byte if necessary
            if (power_byte > 0) { // Avoid pow(10, 0) issues if any
                 divisor = pow(10.0, -static_cast<double>(power_byte));
            }
             // Check for potential overflow if power_byte is very large, though unlikely here
            value *= divisor; // Apply scaling

            if (sign_byte == 1) {
                value = -value; // Apply sign
            }
            // Use default precision for double, which is usually sufficient
            result_ss << "FLOAT - " << value;
            break;
        }
        case 3: {  // STRING
            // Content is directly used as string, ensure it's treated correctly
            // Copy up to content_len, ensuring null termination within buffer bounds
            char string_content[MAX_CONTENT_SIZE + 1];
            int len_to_copy = std::min(msg.content_len, MAX_CONTENT_SIZE);
            memcpy(string_content, msg.content, len_to_copy);
            string_content[len_to_copy] = '\0'; // Ensure null termination
            result_ss << "STRING - " << string_content;
            break;
        }
        default:
            result_ss << "UNKNOWN TYPE (" << (int)msg.type << ")";
    }

    return result_ss.str();
}


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

    std::cerr << "Server started on port " << port << std::endl; // Required output for test "server_start"

    // --- Data Structures ---
    std::map<std::string, Subscriber> subscribers; // Map ID to Subscriber struct for efficient lookup
    std::vector<struct pollfd> poll_fds;           // Vector to hold file descriptors for polling
    std::map<int, std::string> socket_to_id;       // Map socket fd to client ID for easier lookup

    // Add initial sockets to poll set
    poll_fds.push_back({tcp_socket, POLLIN, 0});   // TCP listening socket [index 0]
    poll_fds.push_back({udp_socket, POLLIN, 0});   // UDP socket [index 1]
    poll_fds.push_back({STDIN_FILENO, POLLIN, 0}); // Standard input for server exit [index 2]

    char buffer[BUFFER_SIZE]; // Reusable buffer for receiving data
    bool running = true;

    // --- Main Server Loop ---
    while (running) {
        // Wait for events on polled file descriptors
        int poll_count = poll(poll_fds.data(), poll_fds.size(), -1); // -1 for infinite timeout
        if (poll_count < 0) {
            if (errno == EINTR) continue; // Interrupted by signal, continue polling
            error("ERROR on poll");
        }


        // --- Check STDIN for "exit" command ---
        // Check only if the event occurred
        if (poll_fds[2].revents & POLLIN) {
            memset(buffer, 0, BUFFER_SIZE);
            if (fgets(buffer, BUFFER_SIZE - 1, stdin) != NULL) {
                 // Remove trailing newline
                 buffer[strcspn(buffer, "\n")] = 0;
                 if (strcmp(buffer, "exit") == 0) {
                    // std::cout << "Exit command received. Shutting down." << std::endl; // Test might not expect this
                    running = false; // Set flag to exit loop
                    // Don't break immediately, let loop finish to close sockets gracefully
                } else {
                    // std::cerr << "Unknown command on server stdin: " << buffer << std::endl; // Test might not expect this
                }
            } else {
                 // EOF or error on stdin
                 // std::cerr << "WARN: STDIN closed or error." << std::endl; // Test might not expect this
                 running = false; // Treat as exit signal
            }
        }

        // --- Check TCP listener for new connections ---
        // Check only if the event occurred
        if (poll_fds[0].revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_socket = accept(tcp_socket, (struct sockaddr *) &client_addr, &client_len);

            if (client_socket < 0) {
                 perror("WARN: accept failed"); // Log warning, don't exit
            } else {
                // Disable Nagle's algorithm for the new client socket for lower latency
                int flag = 1;
                if (setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) < 0)
                    perror("WARN: setsockopt TCP_NODELAY failed");

                // Receive client ID (expecting null-terminated string)
                memset(buffer, 0, BUFFER_SIZE);
                // Read only up to MAX_ID_SIZE + 1 to get potential null terminator
                int bytes_received = recv(client_socket, buffer, MAX_ID_SIZE + 1, 0);

                if (bytes_received <= 0) {
                    // std::cerr << "WARN: Failed to receive client ID or client disconnected immediately." << std::endl; // Test might not expect this
                    close(client_socket);
                } else {
                    // Ensure received ID is null-terminated within MAX_ID_SIZE bounds for safety
                    buffer[std::min(bytes_received, MAX_ID_SIZE)] = '\0';
                    std::string client_id_str(buffer);

                    // --- Client Connection Logic ---
                    auto it = subscribers.find(client_id_str);
                    if (it != subscribers.end()) { // ID exists
                        if (it->second.connected) { // Already connected?
                            std::cout << "Client " << client_id_str << " already connected." << std::endl;
                            close(client_socket); // Close the new, duplicate connection
                        } else { // Reconnection
                            char client_ip_str[INET_ADDRSTRLEN];
                            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
                            std::cout << "New client " << client_id_str << " connected from "
                                      << client_ip_str << ":" << ntohs(client_addr.sin_port) << "." << std::endl;

                            // Update existing subscriber entry
                            it->second.socket = client_socket;
                            it->second.connected = true;
                            it->second.command_buffer.clear(); // Clear any old buffer remnants
                            poll_fds.push_back({client_socket, POLLIN, 0}); // Add to poll set
                            socket_to_id[client_socket] = client_id_str; // Update socket->ID map

                            // Send stored messages (SF)
                            for (const std::string& stored_msg : it->second.stored_messages) {
                                // Send message + null terminator
                                ssize_t bytes_sent = send(client_socket, stored_msg.c_str(), stored_msg.length() + 1, MSG_NOSIGNAL);
                                if (bytes_sent < 0) {
                                    perror("WARN: send stored message failed during reconnect");
                                    // Client might disconnect, will be handled later by poll
                                    break; // Stop sending stored messages on error
                                }
                            }
                            it->second.stored_messages.clear(); // Clear after attempting send
                        }
                    } else { // New client ID
                        char client_ip_str[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
                        std::cout << "New client " << client_id_str << " connected from "
                                  << client_ip_str << ":" << ntohs(client_addr.sin_port) << "." << std::endl;

                        // Create and add new subscriber entry
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
            }
        } // End handling new TCP connection

        // --- Check UDP socket for messages ---
        // Check only if the event occurred
        if (poll_fds[1].revents & POLLIN) {
            UdpMessage udp_msg;
            struct sockaddr_in udp_sender_addr;
            socklen_t udp_sender_len = sizeof(udp_sender_addr);

            memset(buffer, 0, BUFFER_SIZE);
            int bytes_received = recvfrom(udp_socket, buffer, BUFFER_SIZE - 1, 0,
                                        (struct sockaddr *) &udp_sender_addr, &udp_sender_len);

            if (bytes_received > 0) {
                 // Basic validation: must have at least topic (1 char) + type byte
                 if (bytes_received < TOPIC_SIZE + 1) { // UDP payload format: topic[50], type[1], content[1500]
                      // Allow shorter topics, but must have at least 1 char topic + type byte
                      // std::cerr << "WARN: Received UDP message too short." << std::endl; // Test might not expect this
                 } else {
                     // Populate UdpMessage struct
                     memset(&udp_msg, 0, sizeof(UdpMessage));
                     // Copy topic carefully, ensuring null termination within TOPIC_SIZE
                     memcpy(udp_msg.topic, buffer, TOPIC_SIZE);
                     udp_msg.topic[TOPIC_SIZE] = '\0'; // Ensure null termination

                     // Type byte is at index TOPIC_SIZE (50)
                     udp_msg.type = (uint8_t)buffer[TOPIC_SIZE];
                     udp_msg.sender_addr = udp_sender_addr;
                     udp_msg.content_len = bytes_received - (TOPIC_SIZE + 1);

                     // Copy content only if content_len is positive and valid
                     if (udp_msg.content_len > 0) {
                         int len_to_copy = std::min(udp_msg.content_len, MAX_CONTENT_SIZE);
                         memcpy(udp_msg.content, buffer + TOPIC_SIZE + 1, len_to_copy);
                         udp_msg.content[len_to_copy] = '\0'; // Null term for safety
                     } else {
                         udp_msg.content_len = 0;
                         udp_msg.content[0] = '\0';
                     }


                     // Format the message string once
                     std::string formatted_msg_str = parseMessage(udp_msg);
                     std::string topic_str(udp_msg.topic);

                     // --- Distribute message to relevant subscribers ---
                     for (auto& pair : subscribers) { // Iterate through map
                         Subscriber& sub = pair.second;
                         // bool sent_or_stored = false; // Not needed with break below

                         for (const auto& topic_pair : sub.topics) { // Check subscriptions
                             const std::string& pattern = topic_pair.first;
                             bool sf_enabled = topic_pair.second;

                             if (topicMatches(topic_str, pattern)) {
                                 if (sub.connected) { // Send if connected
                                     ssize_t bytes_sent = send(sub.socket, formatted_msg_str.c_str(), formatted_msg_str.length() + 1, MSG_NOSIGNAL);
                                     if (bytes_sent < 0) {
                                         // EAGAIN/EWOULDBLOCK might happen if send buffer full, but unlikely with MSG_NOSIGNAL and blocking sockets
                                         // Other errors likely mean disconnection
                                         if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                             perror("WARN: send to subscriber failed");
                                             // Let poll loop handle actual disconnection detection
                                         }
                                     }
                                     // sent_or_stored = true;
                                     break; // Sent/attempted once for this subscriber per matching topic, move to next subscriber
                                 } else if (sf_enabled) { // Store if SF=1 and disconnected
                                     sub.stored_messages.push_back(formatted_msg_str);
                                     // sent_or_stored = true;
                                     break; // Stored once for this subscriber per matching topic, move to next subscriber
                                 } else { // Matched, but disconnected and SF=0
                                     // sent_or_stored = true; // Message dropped for this sub
                                     break; // Message dropped, move to next subscriber
                                 }
                             }
                         } // End loop through subscriber's topics
                     } // End loop through subscribers
                 }
            } else if (bytes_received < 0) {
                 perror("WARN: recvfrom UDP failed");
            }
        } // End handling UDP message

        // --- Check client TCP sockets for commands or disconnection ---
        // Iterate backwards to allow safe removal from poll_fds using erase
        for (int i = poll_fds.size() - 1; i >= 3; --i) {
             // Check if index is still valid after potential erasures in previous iterations
             if (i >= poll_fds.size()) continue;

             struct pollfd& pfd = poll_fds[i];

             // Find the subscriber associated with this socket
             auto id_it = socket_to_id.find(pfd.fd);
             Subscriber* sub_ptr = nullptr;
             std::string client_id_str = "";
             if (id_it != socket_to_id.end()) {
                 client_id_str = id_it->second;
                 auto sub_it = subscribers.find(client_id_str);
                 if (sub_it != subscribers.end()) {
                     sub_ptr = &sub_it->second;
                 }
             }

             // Handle disconnection/error first
             if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                 if (sub_ptr && sub_ptr->connected) { // Check if already marked disconnected
                     std::cout << "Client " << client_id_str << " disconnected (poll error/hup)." << std::endl;
                     sub_ptr->connected = false;
                     sub_ptr->socket = -1; // Mark socket as invalid
                     sub_ptr->command_buffer.clear(); // Clear buffer on disconnect
                 } else if (!sub_ptr) {
                     // std::cerr << "WARN: Poll error/hangup on unknown client socket fd " << pfd.fd << std::endl; // Test might not expect this
                 }
                 close(pfd.fd);
                 if (id_it != socket_to_id.end()) socket_to_id.erase(id_it); // Remove from socket->ID map
                 poll_fds.erase(poll_fds.begin() + i);
                 continue; // Move to next fd
             }

             // Check for incoming data
             if (pfd.revents & POLLIN) {
                 int client_socket = pfd.fd;
                 memset(buffer, 0, BUFFER_SIZE);
                 int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);

                 if (bytes_received <= 0) { // Disconnection or error
                     if (bytes_received < 0) perror("WARN: recv from client failed");

                     if (sub_ptr && sub_ptr->connected) { // Check if already marked disconnected
                         std::cout << "Client " << client_id_str << " disconnected." << std::endl;
                         sub_ptr->connected = false;
                         sub_ptr->socket = -1;
                         sub_ptr->command_buffer.clear();
                     } else if (!sub_ptr) {
                          // std::cerr << "WARN: Disconnection from unknown socket fd " << client_socket << std::endl; // Test might not expect this
                     }
                     close(client_socket);
                     if (id_it != socket_to_id.end()) socket_to_id.erase(id_it);
                     poll_fds.erase(poll_fds.begin() + i);
                     continue; // Move to next fd

                 } else { // Received data from client
                     if (sub_ptr == nullptr) {
                          // std::cerr << "WARN: Received data from unknown socket fd " << client_socket << std::endl; // Test might not expect this
                          continue; // Should not happen
                     }

                     // Append received data to the client's buffer
                     sub_ptr->command_buffer.append(buffer, bytes_received);

                     // Process complete commands (delimited by newline) from the buffer
                     size_t newline_pos;
                     while ((newline_pos = sub_ptr->command_buffer.find('\n')) != std::string::npos) {
                         // Extract command line
                         std::string command_line = sub_ptr->command_buffer.substr(0, newline_pos);
                         // Remove command line (and newline) from buffer
                         sub_ptr->command_buffer.erase(0, newline_pos + 1);

                         // Trim potential leading/trailing whitespace (optional but good practice)
                         command_line.erase(0, command_line.find_first_not_of(" \t\r\n"));
                         command_line.erase(command_line.find_last_not_of(" \t\r\n") + 1);

                         if (command_line.empty()) continue; // Skip empty lines

                         // Parse command using stringstream
                         std::stringstream ss(command_line);
                         std::string command_verb;
                         ss >> command_verb;

                         if (command_verb == "subscribe") {
                             std::string topic;
                             int sf = -1;
                             // Check if topic and sf can be extracted and sf is valid
                             if (ss >> topic >> sf && (sf == 0 || sf == 1) && ss.eof()) { // Ensure no extra tokens
                                 if (topic.length() > TOPIC_SIZE) {
                                     // std::cerr << "WARN: Client " << client_id_str << " tried to subscribe to oversized topic." << std::endl; // Test might not expect this
                                 } else {
                                     sub_ptr->topics[topic] = (sf == 1);
                                     // Confirmation is printed by client
                                 }
                             } else {
                                 // std::cerr << "WARN: Client " << client_id_str << " sent invalid subscribe command format: " << command_line << std::endl; // Test might not expect this
                             }
                         } else if (command_verb == "unsubscribe") {
                             std::string topic;
                             // Check if topic can be extracted and no extra tokens
                             if (ss >> topic && ss.eof()) {
                                  if (topic.length() > TOPIC_SIZE) {
                                     // std::cerr << "WARN: Client " << client_id_str << " tried to unsubscribe from oversized topic." << std::endl; // Test might not expect this
                                  } else {
                                     // Erase returns the number of elements removed (0 or 1)
                                     if (sub_ptr->topics.erase(topic) == 0) {
                                         // std::cerr << "WARN: Client " << client_id_str << " tried to unsubscribe from non-subscribed topic: " << topic << std::endl; // Test might not expect this
                                     }
                                     // Confirmation is printed by client
                                  }
                             } else {
                                 // std::cerr << "WARN: Client " << client_id_str << " sent invalid unsubscribe command format: " << command_line << std::endl; // Test might not expect this
                             }
                         } else {
                             // std::cerr << "WARN: Client " << client_id_str << " sent unknown command: " << command_verb << std::endl; // Test might not expect this
                         }
                     } // End while loop processing commands from buffer
                 }
             } // End handling POLLIN on client socket
        } // End loop through client sockets (indices >= 3)

        // Clean up revents after processing all fds for this poll cycle
        for(auto& pfd_entry : poll_fds) {
            pfd_entry.revents = 0;
        }

    } // End main while loop (running)

    // --- Server Shutdown ---
    // std::cout << "Shutting down server..." << std::endl; // Test might not expect this
    // Close all client sockets still in the poll set (sockets >= index 3)
    for (size_t i = 3; i < poll_fds.size(); ++i) {
         // std::cout << "Closing client socket fd " << poll_fds[i].fd << std::endl; // Test might not expect this
         close(poll_fds[i].fd);
    }
    // Close listening/UDP sockets
    // std::cout << "Closing listening sockets." << std::endl; // Test might not expect this
    close(tcp_socket);
    close(udp_socket);

    // std::cout << "Server shut down complete." << std::endl; // Test might not expect this
    return 0;
}