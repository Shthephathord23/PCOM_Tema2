#ifndef SUBSCRIBER_IO_H
#define SUBSCRIBER_IO_H

#include "circular_buffer.h" // Needs the buffer definition
#include <vector>
#include <poll.h>

// Initializes the pollfd array for monitoring stdin and the server socket.
void initialize_subscriber_poll_fds(std::vector<struct pollfd>& poll_fds, int client_socket);

// Handles input read from standard input (stdin).
// Parses commands (subscribe, unsubscribe, exit) and sends them to the server.
// Modifies the running flag if 'exit' is entered or send fails.
void handle_user_input_command(int client_socket, bool& running_flag);

// Handles messages received from the server.
// Reads data into the circular buffer, processes complete messages, and prints them.
// Modifies the running flag if the connection is closed or an error occurs.
void handle_server_message_data(int client_socket, CircularBuffer<char>& server_buffer, bool& running_flag);

#endif // SUBSCRIBER_IO_H