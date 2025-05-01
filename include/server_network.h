#ifndef SERVER_NETWORK_H
#define SERVER_NETWORK_H

#include "server_state.h" // Includes type definitions
#include <poll.h>
#include <vector>

// Structure to hold the listening sockets
struct ServerSockets { int tcp = -1; int udp = -1; };

// Sets up TCP and UDP listening sockets
ServerSockets setup_server_sockets(int port);

// Closes the listening sockets
void close_server_sockets(const ServerSockets& sockets);

// Initializes the pollfd vector with listening sockets and stdin
void initialize_poll_fds(PollFds& poll_fds, const ServerSockets& sockets);

#endif // SERVER_NETWORK_H