#ifndef SERVER_CLIENT_H
#define SERVER_CLIENT_H

#include "server_state.h" // Includes maps, pollfds, Subscriber struct

// Handles activity (data reception, commands, errors, disconnections)
// on all connected TCP client sockets (indices >= 3 in poll_fds).
void handle_client_activity(PollFds& poll_fds, SubscribersMap& subscribers, SocketToIdMap& socket_to_id);

#endif // SERVER_CLIENT_H