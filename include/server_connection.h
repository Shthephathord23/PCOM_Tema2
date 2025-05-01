#ifndef SERVER_CONNECTION_H
#define SERVER_CONNECTION_H

#include "server_state.h" // Includes maps, pollfds, Subscriber struct

// Handles a new incoming TCP connection request on the listening socket.
void handle_new_connection(int listener_socket, PollFds& poll_fds, SubscribersMap& subscribers, SocketToIdMap& socket_to_id);

#endif // SERVER_CONNECTION_H