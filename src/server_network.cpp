#include "server_network.h"
#include "common.h"       // For error()
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>       // For close()
#include <cstring>        // For memset()

ServerSockets setup_server_sockets(int port) {
    ServerSockets sockets;
    int enable = 1;
    struct sockaddr_in server_addr;

    // TCP Socket
    sockets.tcp = socket(AF_INET, SOCK_STREAM, 0);
    if (sockets.tcp < 0) { error("ERROR opening TCP socket"); /* exits */ }
    // Allow address reuse immediately after server closes
    if (setsockopt(sockets.tcp, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        close(sockets.tcp); error("ERROR setting SO_REUSEADDR on TCP"); return {-1,-1}; }

    // UDP Socket
    sockets.udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockets.udp < 0) { close(sockets.tcp); error("ERROR opening UDP socket"); return {-1,-1}; }
    // Allow address reuse (less critical for UDP, but good practice)
    if (setsockopt(sockets.udp, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        close(sockets.tcp); close(sockets.udp); error("ERROR setting SO_REUSEADDR on UDP"); return {-1,-1}; }

    // Address Configuration (common for both)
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces

    // Bind Sockets
    if (bind(sockets.tcp, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        close(sockets.tcp); close(sockets.udp); error("ERROR binding TCP socket"); return {-1,-1}; }
    if (bind(sockets.udp, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        close(sockets.tcp); close(sockets.udp); error("ERROR binding UDP socket"); return {-1,-1}; }

    // Listen on TCP Socket
    if (listen(sockets.tcp, MAX_CLIENTS) < 0) { // MAX_CLIENTS defined in server.h included via server_state.h -> server.h
        close_server_sockets(sockets);
        error("ERROR on listen");
    }

    return sockets;
}

void close_server_sockets(const ServerSockets& sockets) {
    if (sockets.tcp >= 0) close(sockets.tcp);
    if (sockets.udp >= 0) close(sockets.udp);
}

void initialize_poll_fds(PollFds& poll_fds, const ServerSockets& sockets) {
    poll_fds.clear(); // Ensure it's empty before initializing
    // Order matters for indexing in the main loop
    poll_fds.push_back({sockets.tcp, POLLIN, 0});   // TCP listening [index 0]
    poll_fds.push_back({sockets.udp, POLLIN, 0});   // UDP socket [index 1]
    poll_fds.push_back({STDIN_FILENO, POLLIN, 0}); // Standard input [index 2]
}