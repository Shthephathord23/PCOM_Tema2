#ifndef SERVER_UDP_H
#define SERVER_UDP_H

#include "server_state.h" // Includes maps, Subscriber struct

// Handles incoming UDP datagrams on the UDP socket.
void handle_udp_message(int udp_socket, SubscribersMap& subscribers);

// Helper: Formats a parsed UDP message into the string representation for subscribers.
// Definition needs to be accessible (e.g., moved to .cpp and declared here, or kept inline if simple)
// Moved definition to server_udp.cpp
std::string format_parsed_udp_message(const UdpMessage& msg);

#endif // SERVER_UDP_H