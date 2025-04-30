#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_NODELAY
#include <arpa/inet.h>
#include <poll.h>
#include <netdb.h>
#include <vector>
#include <sstream> // For parsing commands
#include <errno.h> // For errno

#define BUFFER_SIZE 1600 // Should be large enough for formatted messages from server

// Error handling function
void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// Helper function to send data reliably
ssize_t send_all(int sockfd, const void *buf, size_t len, int flags) {
    size_t total = 0;
    const char *ptr = (const char*) buf;
    while(total < len) {
        ssize_t bytes_sent = send(sockfd, ptr + total, len - total, flags);
        if(bytes_sent < 0) {
            if (errno == EINTR) continue; // Interrupted by signal, try again
            return -1; // Error occurred
        }
        if (bytes_sent == 0) {
             // Socket closed or error? Should not happen with blocking sockets unless len was 0.
             return total; // Return bytes sent so far, indicate potential issue
        }
        total += bytes_sent;
    }
    return total; // Success
}


int main(int argc, char *argv[]) {
    // Disable stdout buffering for immediate output, crucial for tester script
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    // --- Argument Parsing ---
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <ID_CLIENT> <IP_SERVER> <PORT_SERVER>" << std::endl;
        return 1;
    }

    std::string client_id = argv[1];
    if (client_id.length() > 10) { // Validate ID length
         std::cerr << "ERROR: Client ID too long (max 10 characters)." << std::endl;
         return 1;
    }
    const char* server_ip_cstr = argv[2];
    int server_port = atoi(argv[3]);
     if (server_port <= 0 || server_port > 65535) { // Validate port
         std::cerr << "ERROR: Invalid server port." << std::endl;
         return 1;
    }

    // --- Socket Creation and Setup ---
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) error("ERROR opening socket");

    // Disable Nagle's algorithm for lower latency (optional but good practice)
    int flag = 1;
    if (setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) < 0)
        perror("WARN: setsockopt TCP_NODELAY failed"); // Non-fatal warning

    // Configure server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    // Convert server IP from string to binary format
    if (inet_pton(AF_INET, server_ip_cstr, &server_addr.sin_addr) <= 0)
        error("ERROR invalid server IP address");

    // --- Connect to Server ---
    if (connect(client_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0)
        error("ERROR connecting to server");

    // --- Send Client ID ---
    // Send ID including the null terminator (+1 length)
    if (send_all(client_socket, client_id.c_str(), client_id.length() + 1, 0) < 0)
        error("ERROR sending client ID");
    // Server handles connection confirmation/rejection

    // --- Polling Setup ---
    struct pollfd poll_fds[2];
    poll_fds[0].fd = STDIN_FILENO;  // Standard input [index 0]
    poll_fds[0].events = POLLIN;
    poll_fds[1].fd = client_socket; // Server socket [index 1]
    poll_fds[1].events = POLLIN;

    char buffer[BUFFER_SIZE]; // Reusable buffer
    std::string server_buffer; // Buffer for potentially fragmented messages from server
    bool running = true;

    // --- Main Client Loop ---
    while (running) {
        // Wait indefinitely for events
        int poll_count = poll(poll_fds, 2, -1);
        if (poll_count < 0) {
             if (errno == EINTR) continue; // Interrupted by signal, continue polling
             error("ERROR on poll");
        }

        // --- Check for user input from STDIN ---
        if (poll_fds[0].revents & POLLIN) {
            memset(buffer, 0, BUFFER_SIZE);
            if (fgets(buffer, BUFFER_SIZE - 1, stdin) == NULL) {
                 // EOF or error on stdin, treat as exit command
                 // std::cout << "STDIN closed, exiting." << std::endl; // Test might not expect this output
                 running = false; // Signal loop exit
                 // Don't break immediately, let loop finish to close socket gracefully
                 continue; // Skip rest of loop iteration
            }

            // Remove trailing newline if present from fgets
            buffer[strcspn(buffer, "\n")] = 0;
            std::string input_line(buffer);

            // Parse command using stringstream
            std::stringstream ss(input_line);
            std::string command_verb;
            ss >> command_verb;

            if (command_verb == "exit") {
                running = false; // Signal loop exit
                // Don't break immediately
                continue;
            } else if (command_verb == "subscribe") {
                std::string topic;
                int sf = -1;
                // Check if topic and sf are provided and valid, and no extra input
                if (ss >> topic >> sf && (sf == 0 || sf == 1) && ss.eof()) {
                     if (topic.length() > 50) { // Basic topic length validation
                          std::cout << "ERROR: Topic too long (max 50 characters)." << std::endl;
                     } else {
                         // Construct command string to send, add newline as delimiter
                         std::string command_to_send = "subscribe " + topic + " " + std::to_string(sf) + "\n";
                         // Send command reliably
                         if (send_all(client_socket, command_to_send.c_str(), command_to_send.length(), 0) < 0) {
                             perror("ERROR sending subscribe command");
                             running = false; // Assume connection lost on send error
                         } else {
                             // Print confirmation locally as required by tests
                             std::cout << "Subscribed to topic." << std::endl;
                         }
                     }
                } else {
                    std::cout << "Usage: subscribe <topic> <sf(0 or 1)>" << std::endl;
                }
            } else if (command_verb == "unsubscribe") {
                 std::string topic;
                 // Check if topic is provided and no extra input
                 if (ss >> topic && ss.eof()) {
                      if (topic.length() > 50) {
                          std::cout << "ERROR: Topic too long (max 50 characters)." << std::endl;
                      } else {
                         // Construct command string to send, add newline as delimiter
                         std::string command_to_send = "unsubscribe " + topic + "\n";
                         // Send command reliably
                         if (send_all(client_socket, command_to_send.c_str(), command_to_send.length(), 0) < 0) {
                             perror("ERROR sending unsubscribe command");
                             running = false; // Assume connection lost
                         } else {
                             // Print confirmation locally
                             std::cout << "Unsubscribed from topic." << std::endl;
                         }
                     }
                 } else {
                     std::cout << "Usage: unsubscribe <topic>" << std::endl;
                 }
            } else if (!input_line.empty()) { // Avoid printing for empty input
                std::cout << "Unknown command. Available: subscribe, unsubscribe, exit." << std::endl;
            }
        } // End handling STDIN

        // --- Check for messages from the server ---
        if (poll_fds[1].revents & POLLIN) {
            memset(buffer, 0, BUFFER_SIZE);
            int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);

            if (bytes_received <= 0) { // Server disconnected or error
                if (bytes_received < 0) {
                    perror("ERROR receiving from server");
                } else { // bytes_received == 0
                    // std::cout << "Server disconnected." << std::endl; // Test might not expect this
                }
                running = false; // Signal loop exit
                // Don't break immediately
                continue;
            }

            // Append received data to the server buffer
            server_buffer.append(buffer, bytes_received);

            // Process complete messages (delimited by null terminator, as sent by server)
            size_t null_pos;
            // Server sends "message\0", so we look for null terminators
            while ((null_pos = server_buffer.find('\0')) != std::string::npos) {
                 // Extract message (up to null terminator)
                 std::string message = server_buffer.substr(0, null_pos);

                 // Remove message and null terminator from buffer
                 server_buffer.erase(0, null_pos + 1);

                 // Display received message
                 // The test script expects a newline after each message printed by the subscriber.
                 std::cout << message << std::endl;
            }
        }

         // --- Check for errors on the server socket ---
         if (poll_fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
             std::cerr << "ERROR: Server connection error/hangup." << std::endl;
             running = false; // Signal loop exit
             // Don't break immediately
             continue;
         }

         // Clean up revents after processing
         poll_fds[0].revents = 0;
         poll_fds[1].revents = 0;

    } // End main while loop

    // --- Clean up ---
    close(client_socket);
    // Optional: message indicating client shutdown
    // std::cout << "Client " << client_id << " shutting down." << std::endl;

    return 0;
}
