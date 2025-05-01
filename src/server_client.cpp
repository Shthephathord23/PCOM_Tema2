#include "server_client.h"
#include "common.h"      // For constants, error()
#include <unistd.h>      // For close()
#include <sys/socket.h>  // For recv()
#include <netinet/in.h>  // For socket structures (though not directly used here)
#include <poll.h>
#include <cstring>       // For memset
#include <iostream>      // For cout, cerr
#include <sstream>       // For parsing commands
#include <vector>
#include <string>
#include <map>
#include <algorithm>     // For std::find_if, std::min etc. if needed

// --- Internal Helper Declarations ---
static void process_incoming_data(Subscriber& sub, int client_socket, PollFds& poll_fds, size_t poll_index, SocketToIdMap& socket_to_id, SubscribersMap& subscribers, bool& should_disconnect);
static bool process_commands_in_buffer(Subscriber& sub);
static void execute_subscribe_command(Subscriber& sub, std::stringstream& ss, const std::string& full_cmd);
static void execute_unsubscribe_command(Subscriber& sub, std::stringstream& ss, const std::string& full_cmd);
static void perform_client_disconnection(int client_socket, size_t poll_index, const std::string& client_id, PollFds& poll_fds, SubscribersMap& subscribers, SocketToIdMap& socket_to_id);

// --- Public Function Implementation ---

void handle_client_activity(PollFds& poll_fds, SubscribersMap& subscribers, SocketToIdMap& socket_to_id) {
    // Iterate backwards through the poll_fds vector, starting from index 3 (client sockets),
    // to allow safe removal of elements during iteration.
    for (int i = poll_fds.size() - 1; i >= 3; --i) {
        // Safety check in case the vector size changed unexpectedly (shouldn't happen in single thread)
        if (i >= (int)poll_fds.size()) continue;

        struct pollfd& current_pfd = poll_fds[i];
        int client_socket = current_pfd.fd;

        // Skip if no events occurred for this client
        if (current_pfd.revents == 0) continue;

        // Find the client ID associated with this socket
        auto id_it = socket_to_id.find(client_socket);
        if (id_it == socket_to_id.end()) {
            // Socket exists in poll_fds but not in map - indicates an inconsistency.
            // Should not normally happen. Clean up defensively.
            // No spec log message required.
            close(client_socket);
            poll_fds.erase(poll_fds.begin() + i);
            continue;
        }
        std::string client_id = id_it->second;

        // Find the Subscriber object using the client ID
        auto sub_it = subscribers.find(client_id);
        if (sub_it == subscribers.end()) {
            // ID exists in socket_to_id map, but not in subscribers map - inconsistency.
            // Clean up defensively.
            // No spec log message required.
            close(client_socket);
            socket_to_id.erase(id_it); // Remove from socket map
            poll_fds.erase(poll_fds.begin() + i); // Remove from poll vector
            continue;
        }
        Subscriber& sub = sub_it->second;

        bool disconnect_client = false;

        // Check for errors first (POLLERR, POLLHUP, POLLNVAL)
        if (current_pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            if (sub.connected) { // Log only if we previously thought it was connected
                 // Required Log Message:
                 std::cout << "Client " << client_id << " disconnected (poll error/hup)." << std::endl;
            }
            disconnect_client = true;
        }
        // Check for incoming data if no error flag was set
        else if (current_pfd.revents & POLLIN) {
            process_incoming_data(sub, client_socket, poll_fds, i, socket_to_id, subscribers, disconnect_client);
            // process_incoming_data sets disconnect_client to true on recv error/EOF or buffer overflow
        }

        // If any event indicated a disconnect, perform cleanup
        if (disconnect_client) {
            perform_client_disconnection(client_socket, i, client_id, poll_fds, subscribers, socket_to_id);
            // `perform_client_disconnection` handles closing the socket and removing entries.
            // The loop continues to the next index (automatically decremented by `i--`).
        }

        // Reset revents for this fd (optional if loop re-initializes revents each time)
        // current_pfd.revents = 0; // Doing it in main loop is safer
    } // End loop through client sockets
}


// --- Internal Helper Implementations ---

// Handles receiving data from a client socket, writing to buffer, and processing commands.
static void process_incoming_data(Subscriber& sub, int client_socket, PollFds& poll_fds, size_t poll_index, SocketToIdMap& socket_to_id, SubscribersMap& subscribers, bool& should_disconnect) {
    char recv_temp_buffer[BUFFER_SIZE];
    memset(recv_temp_buffer, 0, BUFFER_SIZE);

    ssize_t bytes_received = recv(client_socket, recv_temp_buffer, BUFFER_SIZE - 1, 0);

    if (bytes_received <= 0) { // Client disconnected gracefully (0) or error (<0)
        if (bytes_received < 0 && errno != ECONNRESET && errno != EINTR) {
            // Log unexpected recv errors (excluding connection reset, handled as disconnect)
            perror("WARN: recv from client failed");
        }
        if (sub.connected) { // Log disconnect only if we thought it was connected
             // Required Log Message:
             std::cout << "Client " << sub.id << " disconnected." << std::endl;
        }
        should_disconnect = true; // Signal to outer loop to clean up
        return; // No data to process
    }

    // Received data successfully, write it to the subscriber's circular buffer
    if (!sub.command_buffer.write(recv_temp_buffer, bytes_received)) {
        // Buffer overflow - client sent data too fast or commands too large.
        // Treat this as a fatal error for this client.
        std::cerr << "ERROR: Client " << sub.id << " command buffer overflow. Disconnecting." << std::endl;
        should_disconnect = true; // Signal to outer loop to clean up
        return; // Don't process potentially incomplete commands
    }

    // Process any complete commands (newline-terminated) now in the buffer
    if (!process_commands_in_buffer(sub)) {
        // Command processing itself indicated an error (e.g., invalid format, though current parser doesn't signal this way)
        // Decide if this should cause a disconnect. For robustness, maybe disconnect on bad commands.
        // Current implementation: process_commands_in_buffer returns true, so this path isn't taken.
        // If it could return false, we might set `should_disconnect = true;` here.
    }
}

// Processes complete newline-delimited commands from the subscriber's buffer.
// Returns true if successful, false if a severe error occurred during processing (currently always returns true).
static bool process_commands_in_buffer(Subscriber& sub) {
    ssize_t newline_offset;
    // Loop while newline characters are found in the buffer
    while ((newline_offset = sub.command_buffer.find('\n')) >= 0) {
        // Extract the command line string up to (but not including) the newline
        std::string command_line = sub.command_buffer.substr(0, newline_offset);

        // Consume the command AND the newline delimiter from the buffer
        sub.command_buffer.consume(newline_offset + 1);

        // Trim whitespace from the extracted command (robustness)
        command_line.erase(0, command_line.find_first_not_of(" \t\r\n"));
        command_line.erase(command_line.find_last_not_of(" \t\r\n") + 1);

        // Skip empty lines resulting from trimming or consecutive newlines
        if (command_line.empty()) continue;

        // Parse and execute the command
        std::stringstream ss(command_line);
        std::string command_verb;
        ss >> command_verb; // Extract the first word as the command verb

        if (command_verb == "subscribe") {
            execute_subscribe_command(sub, ss, command_line);
        } else if (command_verb == "unsubscribe") {
            execute_unsubscribe_command(sub, ss, command_line);
        } else {
            // Unknown command received. Spec doesn't require logging. Ignore.
            // std::cerr << "WARN: Client " << sub.id << " unknown command: " << command_verb << std::endl;
        }
    }
    // Reached end of buffer or no more complete commands found
    return true; // Indicate success (no critical failure in this function)
}

// Handles the 'subscribe' command.
static void execute_subscribe_command(Subscriber& sub, std::stringstream& ss, const std::string& full_cmd) {
    std::string topic;
    int sf_value = -1; // Initialize to invalid value

    // Try to extract topic and SF value. Check for valid SF (0 or 1) and ensure no trailing characters.
    if (ss >> topic >> sf_value && (sf_value == 0 || sf_value == 1) && ss.eof()) {
        // Command format is valid. Validate topic length.
        if (topic.length() > TOPIC_SIZE) {
            // Topic too long. Spec doesn't require server log. Ignore request.
            // Client should handle this based on common.h constant.
        } else {
            // Add or update the subscription in the map.
            sub.topics[topic] = (sf_value == 1);
            // Client prints confirmation: "Subscribed to topic."
        }
    } else {
        // Invalid command format (missing args, invalid SF, extra args). Ignore.
        // Spec doesn't require server log.
        // std::cerr << "WARN: Client " << sub.id << " invalid subscribe format: " << full_cmd << std::endl;
    }
}

// Handles the 'unsubscribe' command.
static void execute_unsubscribe_command(Subscriber& sub, std::stringstream& ss, const std::string& full_cmd) {
    std::string topic;

    // Try to extract topic and ensure no trailing characters.
    if (ss >> topic && ss.eof()) {
        // Command format is valid. Validate topic length.
        if (topic.length() > TOPIC_SIZE) {
            // Topic too long. Ignore request.
        } else {
            // Attempt to remove the topic from the map.
            // map::erase(key) returns the number of elements removed (0 or 1).
            sub.topics.erase(topic);
            // No check needed if topic existed; erase does nothing if key not found.
            // Client prints confirmation: "Unsubscribed from topic."
        }
    } else {
        // Invalid command format (missing topic, extra args). Ignore.
        // Spec doesn't require server log.
        // std::cerr << "WARN: Client " << sub.id << " invalid unsubscribe format: " << full_cmd << std::endl;
    }
}


// Cleans up resources associated with a disconnected client.
static void perform_client_disconnection(int client_socket, size_t poll_index, const std::string& client_id, PollFds& poll_fds, SubscribersMap& subscribers, SocketToIdMap& socket_to_id) {
    // 1. Close the socket
    close(client_socket);

    // 2. Update Subscriber state (mark as disconnected, reset socket, clear buffer)
    auto sub_it = subscribers.find(client_id);
    if (sub_it != subscribers.end()) {
        sub_it->second.connected = false;
        sub_it->second.socket = -1; // Mark socket as invalid
        sub_it->second.command_buffer.reset(); // Clear any partial commands
        // IMPORTANT: Keep sub.topics and sub.stored_messages for potential reconnection (SF).
    }

    // 3. Remove from socket -> ID map
    socket_to_id.erase(client_socket);

    // 4. Remove from poll vector using the provided index
    // Ensure index is still valid before erasing (paranoid check)
    if (poll_index < poll_fds.size() && poll_fds[poll_index].fd == client_socket) {
         poll_fds.erase(poll_fds.begin() + poll_index);
    } else {
        // Index mismatch - indicates a potential issue elsewhere, maybe log this?
        // Or find the fd again for robustness, though iteration assumes index stability.
    }
}