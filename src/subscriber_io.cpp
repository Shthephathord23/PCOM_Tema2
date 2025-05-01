#include "subscriber_io.h"
#include "common.h" // For constants, send_all, error()
#include <unistd.h>  // For STDIN_FILENO, read, write
#include <poll.h>
#include <sys/socket.h> // for recv
#include <cstdio>    // For fgets, stdout
#include <cstring>   // For memset, strcspn, strlen
#include <string>
#include <vector>
#include <sstream>   // For parsing commands
#include <iostream>  // For cout, cerr

// --- Internal Helper Declarations ---
static void send_command_to_server(int client_socket, const std::string& command, bool& running_flag);
static ssize_t receive_data_into_buffer(int client_socket, CircularBuffer<char>& buffer);
static void process_complete_messages(CircularBuffer<char>& buffer);

// --- Public Function Implementations ---

void initialize_subscriber_poll_fds(std::vector<struct pollfd>& poll_fds, int client_socket) {
    if (poll_fds.size() < 2) {
        poll_fds.resize(2); // Ensure the vector has space for 2 fds
    }
    // Index 0: Standard Input
    poll_fds[0].fd = STDIN_FILENO;
    poll_fds[0].events = POLLIN; // Monitor for readable data
    poll_fds[0].revents = 0;     // Clear any previous events

    // Index 1: Socket connected to the server
    poll_fds[1].fd = client_socket;
    poll_fds[1].events = POLLIN; // Monitor for readable data
    poll_fds[1].revents = 0;     // Clear any previous events
}


void handle_user_input_command(int client_socket, bool& running_flag) {
    char input_buffer[BUFFER_SIZE]; // Reusable buffer for reading stdin
    memset(input_buffer, 0, BUFFER_SIZE);

    // Read a line from standard input
    if (fgets(input_buffer, BUFFER_SIZE - 1, stdin) == NULL) {
        // fgets returns NULL on EOF or error. Treat as implicit "exit".
        running_flag = false;
        return;
    }

    // Remove trailing newline character if present
    input_buffer[strcspn(input_buffer, "\n\r")] = 0;
    std::string input_line(input_buffer);

    // Trim leading/trailing whitespace (optional but good practice)
    input_line.erase(0, input_line.find_first_not_of(" \t"));
    input_line.erase(input_line.find_last_not_of(" \t") + 1);

    // Skip empty lines
    if (input_line.empty()) {
        return;
    }

    // Parse the command using stringstream
    std::stringstream ss(input_line);
    std::string command_verb;
    ss >> command_verb; // Extract the first word

    // --- Command Processing ---
    if (command_verb == "exit") {
        running_flag = false; // Signal main loop to terminate
        // No need to send anything to server, just close socket locally
    } else if (command_verb == "subscribe") {
        std::string topic;
        int sf_dummy = 0; // Subscribe command from client only takes topic (SF=0 implied)
        if (ss >> topic && ss.eof()) { // Expecting: subscribe <topic>
            if (topic.length() > TOPIC_SIZE) {
                // Log error locally, don't send invalid command
                std::cout << "ERROR: Topic too long (max " << TOPIC_SIZE << " characters)." << std::endl;
            } else {
                // Format command string for server (expects SF value)
                std::string command_to_send = "subscribe " + topic + " 0\n"; // SF is always 0 from client input
                send_command_to_server(client_socket, command_to_send, running_flag);
                 if (running_flag) { // Only print if send didn't fail
                    // Required Log Message:
                    std::cout << "Subscribed to topic." << std::endl;
                 }
            }
        } else {
            // Invalid format for subscribe command
            std::cout << "Usage: subscribe <topic>" << std::endl;
        }
    } else if (command_verb == "unsubscribe") {
        std::string topic;
        if (ss >> topic && ss.eof()) { // Expecting: unsubscribe <topic>
             if (topic.length() > TOPIC_SIZE) {
                 // Log error locally
                 std::cout << "ERROR: Topic too long (max " << TOPIC_SIZE << " characters)." << std::endl;
             } else {
                 // Format command string for server
                 std::string command_to_send = "unsubscribe " + topic + "\n";
                 send_command_to_server(client_socket, command_to_send, running_flag);
                 if (running_flag) { // Only print if send didn't fail
                    // Required Log Message:
                    std::cout << "Unsubscribed from topic." << std::endl;
                 }
             }
        } else {
            // Invalid format for unsubscribe command
            std::cout << "Usage: unsubscribe <topic>" << std::endl;
        }
    } else {
        // Unknown command entered by user
        std::cout << "Unknown command. Available: subscribe, unsubscribe, exit." << std::endl;
    }
}


void handle_server_message_data(int client_socket, CircularBuffer<char>& server_buffer, bool& running_flag) {
    // Attempt to receive data from the server socket into the circular buffer
    ssize_t bytes_received = receive_data_into_buffer(client_socket, server_buffer);

    if (bytes_received == -1) { // Error during recv
        // Error already printed by perror in helper. Signal loop termination.
        running_flag = false;
        return;
    } else if (bytes_received == 0) { // Server closed connection gracefully
        // Required Log Message (implicitly handled by spec? Let's add one for clarity)
        std::cerr << "Server closed connection." << std::endl;
        running_flag = false;
        return;
    } else if (bytes_received == -2) { // Custom code for buffer overflow
        // Error message printed by helper. Signal loop termination.
         running_flag = false;
         return;
    }
    // else: bytes_received > 0, data was successfully received and buffered.

    // Process any complete, null-terminated messages now present in the buffer
    process_complete_messages(server_buffer);
}


// --- Internal Helper Implementations ---

// Sends a command string (including newline) to the server.
static void send_command_to_server(int client_socket, const std::string& command, bool& running_flag) {
    ssize_t sent = send_all(client_socket, command.c_str(), command.size(), 0);
    if (sent < 0 || (size_t)sent != command.size()) {
        // send_all prints perror. Set running flag to false to stop client.
        running_flag = false;
        // Avoid printing confirmation messages if send failed.
    }
}

// Receives data from socket and writes it into the circular buffer.
// Returns:
// > 0: Number of bytes received and buffered.
//   0: Server closed connection gracefully.
//  -1: Error during recv (perror called).
//  -2: Buffer overflow error (cerr message printed).
static ssize_t receive_data_into_buffer(int client_socket, CircularBuffer<char>& buffer) {
    char recv_temp[BUFFER_SIZE]; // Temporary stack buffer for recv
    memset(recv_temp, 0, BUFFER_SIZE);

    ssize_t bytes = recv(client_socket, recv_temp, BUFFER_SIZE - 1, 0);

    if (bytes > 0) {
        // Data received, try to write to circular buffer
        if (!buffer.write(recv_temp, bytes)) {
            // Circular buffer is full - cannot store received data. Fatal for subscriber.
            std::cerr << "ERROR: Subscriber buffer overflow. Server data potentially lost. Disconnecting." << std::endl;
            return -2; // Special code for buffer overflow
        }
    } else if (bytes == 0) {
        // Peer (server) closed the connection gracefully.
        return 0;
    } else { // bytes < 0
        // Error occurred during recv. Check errno.
        if (errno == EINTR) {
            // Interrupted by signal, should retry (though poll handles this mostly)
            // Let's return 0 to indicate no data processed *this time*, but not an error state.
            // Or return a specific code if caller needs to know about EINTR. Assume poll handles it.
            // For simplicity, treat other errors as fatal.
             return bytes; // Propagate negative value, but maybe filter EINTR higher up?
        }
         if (errno != ECONNRESET) { // Connection reset is common, treat like graceful close somewhat
            perror("ERROR receiving from server");
         } else {
             std::cerr << "Server closed connection (reset)." << std::endl; // Log reset specifically
         }
        return -1; // General error
    }
    return bytes; // Return number of bytes successfully received and buffered
}

// Finds null-terminated messages in the buffer, prints them, and consumes them.
static void process_complete_messages(CircularBuffer<char>& buffer) {
    ssize_t null_terminator_offset;

    // Loop as long as we can find a null terminator in the buffered data
    while ((null_terminator_offset = buffer.find('\0')) >= 0) {
        // A complete message exists from the start of the buffer up to the null terminator.
        // Length of the message string itself is null_terminator_offset.
        size_t message_length = null_terminator_offset;

        // Extract the message string (excluding the null terminator)
        std::string message = buffer.substr(0, message_length);

        // Consume the message *and* the null terminator from the buffer
        buffer.consume(message_length + 1);

        // Print the received message to standard output
        // Required Log Format: Just print the message content directly.
        std::cout << message << std::endl;
    }
    // No more null terminators found, remaining data is incomplete.
}