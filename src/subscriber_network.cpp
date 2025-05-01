#include "subscriber_network.h"
#include "common.h" // For error(), send_all(), constants
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_NODELAY
#include <arpa/inet.h>
#include <unistd.h>      // For close()
#include <cstring>       // For memset()
#include <iostream>      // For cerr in send_client_id

int setup_and_connect(const std::string& server_ip, int server_port) {
    // 1. Create Socket
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        error("ERROR opening socket"); // Exits
    }

    // 2. Disable Nagle's algorithm (TCP_NODELAY) for lower latency
    int flag = 1;
    if (setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) < 0) {
        // Non-fatal error, just print a warning
        perror("WARN: setsockopt TCP_NODELAY failed");
    }

    // 3. Configure Server Address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port); // Convert port to network byte order

    // Convert IP address string to binary form
    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
        // inet_pton returns 0 for invalid format, -1 for error
        close(client_socket); // Close socket before exiting
        error("ERROR invalid server IP address"); // Exits
    }

    // 4. Connect to Server
    if (connect(client_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        close(client_socket); // Close socket before exiting
        error("ERROR connecting to server"); // Exits
    }

    // Connection successful
    return client_socket;
}

bool send_client_id_to_server(int client_socket, const std::string& client_id) {
    // Send the client ID string *including* the null terminator.
    // The length passed to send_all should be strlen + 1.
    ssize_t bytes_sent = send_all(client_socket, client_id.c_str(), client_id.length() + 1, 0);

    // send_all returns total bytes sent on success, or -1 on error.
    if (bytes_sent < 0 || (size_t)bytes_sent != client_id.length() + 1) {
        // send_all already prints perror on error using its internal helper.
        std::cerr << "ERROR sending client ID failed." << std::endl; // Additional context
        return false;
    }
    return true;
}