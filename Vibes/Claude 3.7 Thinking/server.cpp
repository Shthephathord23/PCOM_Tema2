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
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <netdb.h>
#include <climits>
#include <cmath>

#define MAX_CLIENTS 100
#define BUFFER_SIZE 1600
#define TOPIC_SIZE 50
#define MAX_CONTENT_SIZE 1500
#define MAX_ID_SIZE 10

// Structure to represent a TCP client (subscriber)
struct Subscriber {
    int socket;
    char id[MAX_ID_SIZE + 1];
    std::map<std::string, bool> topics;  // Topic + SF flag
    std::vector<std::string> stored_messages; // For store-and-forward
    bool connected;
};

// Structure for messages
struct Message {
    char topic[TOPIC_SIZE + 1];
    uint8_t type;
    char content[MAX_CONTENT_SIZE + 1];
    struct sockaddr_in sender;
};

// Structure for parsing UDP message content
union ContentParser {
    uint8_t sign_byte;
    uint16_t short_value;
    uint32_t int_value;
    float float_value;
    char string_value[MAX_CONTENT_SIZE + 1];
};

// Error handling function
void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// Function to match a topic against a pattern (with wildcards)
bool topicMatches(const std::string& topic, const std::string& pattern) {
    // Exact match
    if (pattern == topic) return true;
    
    // Single wildcard match (*)
    if (pattern == "*") return true;
    
    // If pattern contains no wildcards, it must be an exact match
    if (pattern.find('+') == std::string::npos && pattern.find('*') == std::string::npos) {
        return pattern == topic;
    }
    
    // Split the pattern and topic into segments
    std::vector<std::string> patternSegments;
    std::vector<std::string> topicSegments;
    
    size_t start = 0, end;
    // Split pattern
    while ((end = pattern.find('/', start)) != std::string::npos) {
        patternSegments.push_back(pattern.substr(start, end - start));
        start = end + 1;
    }
    patternSegments.push_back(pattern.substr(start));
    
    start = 0;
    // Split topic
    while ((end = topic.find('/', start)) != std::string::npos) {
        topicSegments.push_back(topic.substr(start, end - start));
        start = end + 1;
    }
    topicSegments.push_back(topic.substr(start));
    
    // Check if the pattern ends with '*' which would match any number of segments
    if (patternSegments.back() == "*") {
        // If only '*', it was already checked above
        if (patternSegments.size() == 1) return true;
        
        // Check all segments except the '*' at the end
        size_t i;
        for (i = 0; i < patternSegments.size() - 1; i++) {
            if (i >= topicSegments.size()) return false;
            
            if (patternSegments[i] == "+") {
                // '+' matches exactly one segment
                continue;
            } else if (patternSegments[i] != topicSegments[i]) {
                return false;
            }
        }
        
        // All segments before '*' matched
        return true;
    }
    
    // If different number of segments and no '*' at the end, no match
    if (patternSegments.size() != topicSegments.size()) {
        return false;
    }
    
    // Check each segment
    for (size_t i = 0; i < patternSegments.size(); i++) {
        if (patternSegments[i] == "+") {
            // '+' matches exactly one segment
            continue;
        } else if (patternSegments[i] != topicSegments[i]) {
            return false;
        }
    }
    
    return true;
}

// Function to parse UDP message into a nice format
std::string parseMessage(const Message& msg) {
    std::string result;
    
    // Add IP and port of sender
    char sender_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(msg.sender.sin_addr), sender_ip, INET_ADDRSTRLEN);
    result += std::string(sender_ip) + ":" + std::to_string(ntohs(msg.sender.sin_port)) + " - ";
    
    // Add topic
    result += std::string(msg.topic) + " - ";
    
    // Parse content based on type
    ContentParser parser;
    memcpy(&parser, msg.content, MAX_CONTENT_SIZE);
    
    switch(msg.type) {
        case 0: {  // INT
            int value = ntohl(parser.int_value);
            if (parser.sign_byte == 1) {
                value = -value;
            }
            result += "INT - " + std::to_string(value);
            break;
        }
        case 1: {  // SHORT_REAL
            float value = ntohs(parser.short_value) / 100.0;
            result += "SHORT_REAL - " + std::to_string(value);
            break;
        }
        case 2: {  // FLOAT
            float value = ntohl(parser.int_value) / pow(10, (int)parser.sign_byte);
            if (msg.content[0] == 1) {
                value = -value;
            }
            result += "FLOAT - " + std::to_string(value);
            break;
        }
        case 3: {  // STRING
            result += "STRING - " + std::string(parser.string_value);
            break;
        }
        default:
            result += "UNKNOWN TYPE";
    }
    
    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <PORT>" << std::endl;
        return 1;
    }

    int port = atoi(argv[1]);
    
    // Create TCP socket
    int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket < 0) {
        error("ERROR opening TCP socket");
    }

    // Set socket options for TCP
    int enable = 1;
    if (setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        error("ERROR setting TCP socket options");
    }

    // Create UDP socket
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0) {
        error("ERROR opening UDP socket");
    }

    // Configure server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind TCP socket
    if (bind(tcp_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        error("ERROR binding TCP socket");
    }

    // Bind UDP socket
    if (bind(udp_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        error("ERROR binding UDP socket");
    }

    // Listen for TCP connections
    if (listen(tcp_socket, MAX_CLIENTS) < 0) {
        error("ERROR on listen");
    }

    std::cout << "Server started on port " << port << std::endl;

    // Data structures to keep track of clients and topics
    std::vector<Subscriber> subscribers;
    std::map<std::string, std::vector<int>> topic_subscribers; // Topic -> subscriber indices
    
    // Set up polling
    std::vector<struct pollfd> poll_fds;
    poll_fds.push_back({tcp_socket, POLLIN, 0});  // TCP listening socket
    poll_fds.push_back({udp_socket, POLLIN, 0});  // UDP socket
    poll_fds.push_back({STDIN_FILENO, POLLIN, 0}); // Standard input for server exit
    
    char buffer[BUFFER_SIZE];
    
    while (true) {
        // Poll for events
        if (poll(poll_fds.data(), poll_fds.size(), -1) < 0) {
            error("ERROR on poll");
        }

        // Check for user input (exit command)
        if (poll_fds[2].revents & POLLIN) {
            memset(buffer, 0, BUFFER_SIZE);
            fgets(buffer, BUFFER_SIZE, stdin);
            if (strncmp(buffer, "exit", 4) == 0) {
                break;
            }
        }

        // Check TCP socket for new connections
        if (poll_fds[0].revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            int client_socket = accept(tcp_socket, (struct sockaddr *) &client_addr, &client_len);
            if (client_socket < 0) {
                error("ERROR on accept");
            }

            // Disable Nagle's algorithm
            int flag = 1;
            if (setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) < 0) {
                error("ERROR setting TCP_NODELAY");
            }

           // In the main function, where we handle new client connections:
            // Read client ID
            memset(buffer, 0, BUFFER_SIZE);
            int bytes_received = recv(client_socket, buffer, MAX_ID_SIZE, 0);
            if (bytes_received <= 0) {
                close(client_socket);
                continue;
            }
            // Ensure null termination (the client may have sent it, but let's be safe)
            buffer[bytes_received] = '\0';

            // Also ensure Subscriber struct initialization correctly handles the ID
            Subscriber new_subscriber;
            memset(&new_subscriber, 0, sizeof(Subscriber)); // Initialize all fields to zero
            strncpy(new_subscriber.id, buffer, MAX_ID_SIZE);
            new_subscriber.id[MAX_ID_SIZE] = '\0'; // Make absolutely sure it's null-terminated
            new_subscriber.socket = client_socket;
            new_subscriber.connected = true;

            // Check if client already exists
            bool reconnection = false;
            int client_idx = -1;
            
            for (size_t i = 0; i < subscribers.size(); i++) {
                if (strcmp(subscribers[i].id, buffer) == 0) {
                    client_idx = i;
                    reconnection = true;
                    break;
                }
            }

            if (reconnection) {
                // Check if client was already connected
                if (subscribers[client_idx].connected) {
                    std::cout << "Client " << buffer << " already connected." << std::endl;
                    // Send ID already taken error
                    std::string error_msg = "Client ID already connected";
                    send(client_socket, error_msg.c_str(), error_msg.length(), 0);
                    close(client_socket);
                    continue;
                }
                
                std::cout << "New client " << buffer << " connected from " 
                          << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port)
                          << "." << std::endl;
                
                // Update socket and status
                subscribers[client_idx].socket = client_socket;
                subscribers[client_idx].connected = true;
                
                // Send stored messages for this client
                for (const auto& msg : subscribers[client_idx].stored_messages) {
                    send(client_socket, msg.c_str(), msg.length(), 0);
                }
                subscribers[client_idx].stored_messages.clear();
                
                // Add to poll
                poll_fds.push_back({client_socket, POLLIN, 0});
            } else {
                // New client
                Subscriber new_subscriber;
                strncpy(new_subscriber.id, buffer, MAX_ID_SIZE);
                new_subscriber.socket = client_socket;
                new_subscriber.connected = true;
                
                subscribers.push_back(new_subscriber);
                poll_fds.push_back({client_socket, POLLIN, 0});
                
                std::cout << "New client " << buffer << " connected from " 
                          << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port)
                          << "." << std::endl;
            }
        }

        // Check UDP socket for messages
        if (poll_fds[1].revents & POLLIN) {
            struct sockaddr_in udp_client_addr;
            socklen_t udp_client_len = sizeof(udp_client_addr);
            
            memset(buffer, 0, BUFFER_SIZE);
            int bytes = recvfrom(udp_socket, buffer, BUFFER_SIZE, 0,
                                (struct sockaddr *) &udp_client_addr, &udp_client_len);
            
            if (bytes > 0) {
                // Parse UDP message
                Message msg;
                memset(&msg, 0, sizeof(msg));
                msg.sender = udp_client_addr;
                
                // Extract topic (first 50 bytes)
                strncpy(msg.topic, buffer, TOPIC_SIZE);
                msg.topic[TOPIC_SIZE] = '\0';
                
                // Extract data type (1 byte)
                msg.type = buffer[TOPIC_SIZE];
                
                // Extract content (remaining bytes)
                memcpy(msg.content, buffer + TOPIC_SIZE + 1, bytes - TOPIC_SIZE - 1);
                
                // Format message for subscribers
                std::string formatted_msg = parseMessage(msg);
                
                // Broadcast to subscribers of this topic
                std::string topic_str(msg.topic);
                for (size_t i = 0; i < subscribers.size(); i++) {
                    // Check all subscriptions for this subscriber
                    for (const auto& sub : subscribers[i].topics) {
                        // If the topic matches the subscription pattern
                        if (topicMatches(topic_str, sub.first)) {
                            if (subscribers[i].connected) {
                                send(subscribers[i].socket, formatted_msg.c_str(), formatted_msg.length(), 0);
                                // Only send once per subscriber, even if multiple patterns match
                                break;
                            } else if (sub.second) {  // SF=1
                                subscribers[i].stored_messages.push_back(formatted_msg);
                                // Only store once per subscriber
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Check client sockets for messages
        for (size_t i = 3; i < poll_fds.size(); i++) {
            if (poll_fds[i].revents & POLLIN) {
                int client_socket = poll_fds[i].fd;
                
                memset(buffer, 0, BUFFER_SIZE);
                int bytes = recv(client_socket, buffer, BUFFER_SIZE, 0);
                
                if (bytes <= 0) {
                    // Client disconnected
                    int client_idx = -1;
                    for (size_t j = 0; j < subscribers.size(); j++) {
                        if (subscribers[j].socket == client_socket) {
                            client_idx = j;
                            break;
                        }
                    }
                    
                    if (client_idx != -1) {
                        std::cout << "Client " << subscribers[client_idx].id << " disconnected." << std::endl;
                        subscribers[client_idx].connected = false;
                        close(client_socket);
                    }
                    
                    // Remove from polling
                    poll_fds.erase(poll_fds.begin() + i);
                    i--;
                } else {
                    // Process client command
                    std::string cmd(buffer);
                    
                    // Find client
                    int client_idx = -1;
                    for (size_t j = 0; j < subscribers.size(); j++) {
                        if (subscribers[j].socket == client_socket) {
                            client_idx = j;
                            break;
                        }
                    }
                    
                    if (client_idx != -1) {
                        // Parse command: "subscribe topic sf" or "unsubscribe topic"
                        if (cmd.find("subscribe") == 0) {
                            size_t topic_start = cmd.find(' ') + 1;
                            size_t sf_start = cmd.find(' ', topic_start) + 1;
                            
                            if (sf_start > topic_start) {
                                std::string topic = cmd.substr(topic_start, sf_start - topic_start - 1);
                                int sf = std::stoi(cmd.substr(sf_start));
                                
                                subscribers[client_idx].topics[topic] = (sf == 1);
                                std::cout << "Client " << subscribers[client_idx].id 
                                          << " subscribed to topic " << topic 
                                          << " with SF " << sf << std::endl;
                            }
                        } else if (cmd.find("unsubscribe") == 0) {
                            size_t topic_start = cmd.find(' ') + 1;
                            std::string topic = cmd.substr(topic_start);
                            
                            subscribers[client_idx].topics.erase(topic);
                            std::cout << "Client " << subscribers[client_idx].id 
                                      << " unsubscribed from topic " << topic << std::endl;
                        }
                    }
                }
            }
        }
    }

    // Clean up
    for (const auto& subscriber : subscribers) {
        if (subscriber.connected) {
            close(subscriber.socket);
        }
    }
    close(tcp_socket);
    close(udp_socket);

    return 0;
}