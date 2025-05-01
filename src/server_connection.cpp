#include "server_connection.h"
#include "common.h"      // For common definitions, error()
#include "server.h"      // For Subscriber struct definition, constants
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>      // For close()
#include <cstring>       // For strncpy, memset, strlen, strcspn
#include <iostream>      // For cout
#include <algorithm>     // For std::min

// --- Internal Helper Declarations ---
static bool receive_and_validate_client_id(int client_socket, std::string& client_id_str);
static void process_reconnection(Subscriber& sub, int new_socket, const struct sockaddr_in& client_addr, PollFds& poll_fds, SocketToIdMap& socket_to_id);
static void process_new_client(const std::string& client_id, int client_socket, const struct sockaddr_in& client_addr, PollFds& poll_fds, SubscribersMap& subscribers, SocketToIdMap& socket_to_id);
static void send_stored_messages_on_reconnect(Subscriber& sub);

// --- Public Function Implementation ---

void handle_new_connection(int listener_socket, PollFds& poll_fds, SubscribersMap& subscribers, SocketToIdMap& socket_to_id) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_socket = accept(listener_socket, (struct sockaddr *) &client_addr, &client_len);
    if (client_socket < 0) {
        // Don't exit server for accept error, just log and continue
        if (errno != EINTR && errno != ECONNABORTED) {
           perror("WARN: accept failed");
        }
        return;
    }

    // Disable Nagle's algorithm for responsiveness
    int flag = 1;
    if (setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) < 0) {
        // Non-fatal, just log
        perror("WARN: setsockopt TCP_NODELAY failed");
    }

    std::string client_id_str;
    if (!receive_and_validate_client_id(client_socket, client_id_str)) {
        // Error message already printed by helper or implied by closed connection
        close(client_socket);
        return;
    }

    auto it = subscribers.find(client_id_str);
    if (it != subscribers.end()) { // Client ID already exists
        if (it->second.connected) {
            // Client is already actively connected, reject this new connection
            std::cout << "Client " << client_id_str << " already connected." << std::endl;
            close(client_socket); // Close the newly accepted socket
        } else {
            // Client is reconnecting
            process_reconnection(it->second, client_socket, client_addr, poll_fds, socket_to_id);
        }
    } else { // This is a completely new client ID
        process_new_client(client_id_str, client_socket, client_addr, poll_fds, subscribers, socket_to_id);
    }
}

// --- Internal Helper Implementations ---

// Receives the client ID, validates length and content.
static bool receive_and_validate_client_id(int client_socket, std::string& client_id_str) {
    char buffer[BUFFER_SIZE]; // Use common buffer size, larger than needed for ID
    memset(buffer, 0, BUFFER_SIZE);

    // Read up to MAX_ID_SIZE + 1 bytes. Expect ID + null terminator.
    // Reading one extra byte helps detect if the ID was too long *before* null termination.
    ssize_t bytes_received = recv(client_socket, buffer, MAX_ID_SIZE + 1, 0);

    if (bytes_received <= 0) {
        // Connection closed or error during ID send
        // No specific log message required by spec here.
        return false;
    }

    // Check if received data actually exceeds MAX_ID_SIZE before null term
    if (bytes_received > MAX_ID_SIZE + 1 ||      // Should not happen with buffer limit, but defensive
        (bytes_received == MAX_ID_SIZE + 1 && buffer[MAX_ID_SIZE] != '\0')) {
        // ID is too long (either filled buffer + no null, or more bytes than ID size + null)
        // Spec doesn't require a server log message for this case. Client handles its own validation.
        return false;
    }

    // Ensure null termination within the buffer for safety, even if client sent < MAX_ID_SIZE
    buffer[std::min((ssize_t)MAX_ID_SIZE, bytes_received)] = '\0';

     // Check for invalid characters (like newline) within the supposed ID string
    if (strcspn(buffer, "\n\r") != strlen(buffer)) {
         // Invalid characters found before the end of the string.
         // Spec doesn't require server log.
         return false;
    }

    // Check effective length after ensuring null termination
    if (strlen(buffer) > MAX_ID_SIZE) {
         // This check is somewhat redundant due to the recv size limit, but good defense.
         return false;
    }


    client_id_str = buffer; // Assign the validated ID string
    return !client_id_str.empty(); // Ensure ID is not empty
}

// Updates state for a reconnecting client.
static void process_reconnection(Subscriber& sub, int new_socket, const struct sockaddr_in& client_addr, PollFds& poll_fds, SocketToIdMap& socket_to_id) {
    char client_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
    // Required Log Message:
    std::cout << "New client " << sub.id << " connected from "
              << client_ip_str << ":" << ntohs(client_addr.sin_port) << "." << std::endl;

    // Update the subscriber object
    sub.socket = new_socket; // Assign the new socket fd
    sub.connected = true;
    sub.command_buffer.reset(); // Clear any stale command fragments

    // Add new socket to poll set
    poll_fds.push_back({new_socket, POLLIN, 0});
    // Update the socket-to-ID mapping
    socket_to_id[new_socket] = sub.id;

    // Send any messages stored while disconnected (SF mechanism)
    send_stored_messages_on_reconnect(sub);
    sub.stored_messages.clear(); // Clear stored messages regardless of send success
}

// Creates state for a newly connected client.
static void process_new_client(const std::string& client_id, int client_socket, const struct sockaddr_in& client_addr, PollFds& poll_fds, SubscribersMap& subscribers, SocketToIdMap& socket_to_id) {
    char client_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
     // Required Log Message:
    std::cout << "New client " << client_id << " connected from "
              << client_ip_str << ":" << ntohs(client_addr.sin_port) << "." << std::endl;

    // Create and insert the new subscriber using operator[] which default-constructs
    // The Subscriber constructor initializes the command_buffer.
    Subscriber& new_sub = subscribers[client_id];

    // Populate the fields
    strncpy(new_sub.id, client_id.c_str(), MAX_ID_SIZE);
    new_sub.id[MAX_ID_SIZE] = '\0'; // Ensure null termination
    new_sub.socket = client_socket;
    new_sub.connected = true;
    new_sub.topics.clear(); // Ensure topics map is empty for a new client
    new_sub.stored_messages.clear(); // Ensure stored messages are empty

    // Add to poll set and socket-to-ID map
    poll_fds.push_back({client_socket, POLLIN, 0});
    socket_to_id[client_socket] = client_id;
}

// Sends stored messages for SF=1 topics upon client reconnection.
static void send_stored_messages_on_reconnect(Subscriber& sub) {
    // Iterate using indices to allow potential modification (though not needed here)
    for (size_t i = 0; i < sub.stored_messages.size(); ++i) {
        const std::string& stored_msg = sub.stored_messages[i];
        // Send message + null terminator
        ssize_t sent = send_all(sub.socket, stored_msg.c_str(), stored_msg.length() + 1, MSG_NOSIGNAL);

        // Check for errors (send_all returns -1 on error, or less than requested on partial/error)
        if (sent < 0 || (size_t)sent != stored_msg.length() + 1) {
            // Log specific errors only if needed for debugging, spec doesn't require it.
            // Check for non-critical errors like broken pipe which just mean client disconnected again.
            if (errno != EPIPE && errno != ECONNRESET) {
                 perror("WARN: send_all for stored message failed");
            }
            // If send fails (e.g., socket closed), stop trying to send more stored messages.
            // The main loop will handle the disconnection event properly.
            break;
        }
    }
    // Messages are cleared in process_reconnection after this function returns.
}