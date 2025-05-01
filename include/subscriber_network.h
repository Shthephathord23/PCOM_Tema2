#ifndef SUBSCRIBER_NETWORK_H
#define SUBSCRIBER_NETWORK_H

#include <string>

// Sets up the TCP socket, connects to the server, and sets TCP_NODELAY.
// Returns the connected socket fd, or exits on error.
int setup_and_connect(const std::string& server_ip, int server_port);

// Sends the client ID (including null terminator) to the server.
// Returns true on success, false on failure.
bool send_client_id_to_server(int client_socket, const std::string& client_id);

#endif // SUBSCRIBER_NETWORK_H