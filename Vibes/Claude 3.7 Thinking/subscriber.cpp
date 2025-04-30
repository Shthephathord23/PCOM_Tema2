#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <netdb.h>

#define BUFFER_SIZE 1600

void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <ID> <SERVER_IP> <SERVER_PORT>" << std::endl;
        return 1;
    }

    std::string client_id = argv[1];
    std::string server_ip = argv[2];
    int server_port = atoi(argv[3]);
    
    // Create socket
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        error("ERROR opening socket");
    }

    // Disable Nagle's algorithm
    int flag = 1;
    if (setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) < 0) {
        error("ERROR setting TCP_NODELAY");
    }

    // Configure server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    
    // Convert server IP from string to binary
    if (inet_aton(server_ip.c_str(), &server_addr.sin_addr) == 0) {
        error("ERROR invalid server IP");
    }

    // Connect to server
    if (connect(client_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        error("ERROR connecting to server");
    }

    // Send client ID - THIS IS THE KEY FIX:
    // Including the null terminator in the length calculation
    if (send(client_socket, client_id.c_str(), client_id.length() + 1, 0) < 0) {
        error("ERROR sending client ID");
    }

    // Set up polling
    struct pollfd poll_fds[2];
    poll_fds[0].fd = STDIN_FILENO;  // Standard input
    poll_fds[0].events = POLLIN;
    poll_fds[1].fd = client_socket;  // Server socket
    poll_fds[1].events = POLLIN;

    char buffer[BUFFER_SIZE];
    bool running = true;

    while (running) {
        // Poll for events
        if (poll(poll_fds, 2, -1) < 0) {
            error("ERROR on poll");
        }

        // Check for user input
        if (poll_fds[0].revents & POLLIN) {
            memset(buffer, 0, BUFFER_SIZE);
            if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
                break;
            }

            // Remove trailing newline
            buffer[strcspn(buffer, "\n")] = 0;
            std::string input(buffer);
            
            if (input == "exit") {
                running = false;
                break;
            } 
            else if (input.substr(0, 9) == "subscribe") {
                // Check format: subscribe topic sf
                size_t first_space = input.find(' ');
                size_t second_space = input.find(' ', first_space + 1);
                
                if (first_space != std::string::npos && second_space != std::string::npos) {
                    std::string topic = input.substr(first_space + 1, second_space - first_space - 1);
                    std::string sf_str = input.substr(second_space + 1);
                    
                    int sf;
                    try {
                        sf = std::stoi(sf_str);
                        if (sf != 0 && sf != 1) {
                            std::cout << "SF must be 0 or 1" << std::endl;
                            continue;
                        }
                    } catch (...) {
                        std::cout << "Invalid SF value" << std::endl;
                        continue;
                    }
                    
                    // Send subscribe command to server
                    if (send(client_socket, input.c_str(), input.length(), 0) < 0) {
                        error("ERROR sending command");
                    }
                    
                    std::cout << "Subscribed to topic: " << topic << " with SF: " << sf << std::endl;
                } else {
                    std::cout << "Usage: subscribe <topic> <sf>" << std::endl;
                }
            } 
            else if (input.substr(0, 11) == "unsubscribe") {
                // Check format: unsubscribe topic
                size_t space = input.find(' ');
                
                if (space != std::string::npos) {
                    std::string topic = input.substr(space + 1);
                    
                    // Send unsubscribe command to server
                    if (send(client_socket, input.c_str(), input.length(), 0) < 0) {
                        error("ERROR sending command");
                    }
                    
                    std::cout << "Unsubscribed from topic: " << topic << std::endl;
                } else {
                    std::cout << "Usage: unsubscribe <topic>" << std::endl;
                }
            } 
            else {
                std::cout << "Unknown command. Available commands: subscribe <topic> <sf>, unsubscribe <topic>, exit" << std::endl;
            }
        }

        // Check for messages from server
        if (poll_fds[1].revents & POLLIN) {
            memset(buffer, 0, BUFFER_SIZE);
            int bytes = recv(client_socket, buffer, BUFFER_SIZE, 0);
            
            if (bytes <= 0) {
                std::cout << "Connection to server closed." << std::endl;
                break;
            }

            // Display received message
            std::cout << buffer << std::endl;
        }
    }

    // Clean up
    close(client_socket);

    return 0;
}