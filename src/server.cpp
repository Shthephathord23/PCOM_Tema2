// File: ../../src/server.cpp
#include "server.h"
#include <algorithm> // For std::min, std::find_if
#include <cstdio>   // For setvbuf
#include <cstdlib>  // For atoi, exit
#include <csignal>  // For signal handling (related to EINTR)

// --- Type Definitions ---
using PollFds = std::vector<struct pollfd>;
using SubscribersMap = std::map<std::string, Subscriber>;
using SocketToIdMap = std::map<int, std::string>;

// --- Forward Declarations ---

// --- Socket Setup ---
struct ServerSockets { int tcp = -1; int udp = -1; };
static ServerSockets setup_server_sockets(int port);
static void close_server_sockets(const ServerSockets& sockets);

// --- Poll Initialization ---
static void initialize_poll_fds(PollFds& poll_fds, const ServerSockets& sockets);

// --- Main Loop Event Handlers ---
static void handle_stdin(bool& running);
static void handle_new_connection(int listener_socket, PollFds& poll_fds, SubscribersMap& subscribers, SocketToIdMap& socket_to_id);
static void handle_udp_message(int udp_socket, SubscribersMap& subscribers);
static void handle_client_activity(PollFds& poll_fds, SubscribersMap& subscribers, SocketToIdMap& socket_to_id);

// --- Client Connection Helpers ---
static bool receive_client_id(int client_socket, std::string& client_id_str);
static void handle_reconnection(Subscriber& sub, int new_socket, const struct sockaddr_in& client_addr, PollFds& poll_fds, SocketToIdMap& socket_to_id);
static void handle_new_client(const std::string& client_id, int client_socket, const struct sockaddr_in& client_addr, PollFds& poll_fds, SubscribersMap& subscribers, SocketToIdMap& socket_to_id);
static void send_stored_messages(Subscriber& sub);

// --- Client Activity Helpers ---
static void handle_client_disconnection(int client_socket, size_t poll_index, const std::string& client_id, PollFds& poll_fds, SubscribersMap& subscribers, SocketToIdMap& socket_to_id);
static bool process_client_data(Subscriber& sub);
static bool process_commands_from_buffer(Subscriber& sub);
static void parse_and_execute_command(Subscriber& sub, const std::string& command_line);

// --- UDP Message Helpers ---
static bool parse_udp_datagram(const char* buffer, int bytes_received, UdpMessage& udp_msg);
static void distribute_udp_message(const UdpMessage& msg, const std::string& formatted_msg, SubscribersMap& subscribers);

// --- UdpMessage Method (Moved from header, Renamed for clarity) ---
std::string UdpMessage::formatMessage() {
    std::stringstream result_ss;
    char sender_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(this->sender_addr.sin_addr), sender_ip, INET_ADDRSTRLEN);
    result_ss << sender_ip << ":" << ntohs(this->sender_addr.sin_port) << " - ";
    result_ss << this->topic << " - ";

    switch(this->type) {
        case 0: { // INT
            if (this->content_len < 5) { result_ss << "INT - INVALID DATA"; break; }
            uint8_t sign = this->content[0];
            uint32_t net_val; memcpy(&net_val, this->content + 1, 4);
            int val = ntohl(net_val);
            if (sign == 1) val = -val;
            else if (sign != 0) { result_ss << "INT - INVALID SIGN BYTE"; break; }
            result_ss << "INT - " << val;
            break;
        }
        case 1: { // SHORT_REAL
            if (this->content_len < 2) { result_ss << "SHORT_REAL - INVALID DATA"; break; }
            uint16_t net_val; memcpy(&net_val, this->content, 2);
            float val = ntohs(net_val) / 100.0f;
            result_ss << "SHORT_REAL - " << std::fixed << std::setprecision(2) << val;
            result_ss.unsetf(std::ios_base::floatfield);
            break;
        }
        case 2: { // FLOAT
            if (this->content_len < 6) { result_ss << "FLOAT - INVALID DATA"; break; }
            uint8_t sign = this->content[0];
            uint32_t net_val; memcpy(&net_val, this->content + 1, 4);
            uint8_t power = this->content[5];
            double val = ntohl(net_val);
            if (power > 0) val *= pow(10.0, -static_cast<double>(power));
            if (sign == 1) val = -val;
            else if (sign != 0) { result_ss << "FLOAT - INVALID SIGN BYTE"; break; }
            result_ss << "FLOAT - " << std::fixed << std::setprecision(power) << val;
            result_ss.unsetf(std::ios_base::floatfield);
            break;
        }
        case 3: { // STRING
            char str_content[MAX_CONTENT_SIZE + 1];
            int len = std::min(this->content_len, MAX_CONTENT_SIZE);
            memcpy(str_content, this->content, len);
            str_content[len] = '\0';
            result_ss << "STRING - " << str_content;
            break;
        }
        default:
            result_ss << "UNKNOWN TYPE (" << (int)this->type << ")";
    }
    return result_ss.str();
}

// --- topicMatches (Unchanged) ---
bool topicMatches(const std::string& topic, const std::string& pattern) {
    std::vector<std::string> p_segs, t_segs;
    std::string segment;
    std::stringstream ss_p(pattern); while (getline(ss_p, segment, '/')) p_segs.push_back(segment);
    std::stringstream ss_t(topic); while (getline(ss_t, segment, '/')) t_segs.push_back(segment);
    size_t p_idx = 0, t_idx = 0;
    while (p_idx < p_segs.size() && t_idx < t_segs.size()) {
        if (p_segs[p_idx] == "+") { p_idx++; t_idx++; }
        else if (p_segs[p_idx] == "*") {
            p_idx++; if (p_idx == p_segs.size()) return true;
            while (t_idx < t_segs.size()) {
                 std::string rem_t, rem_p;
                 for(size_t i = t_idx; i < t_segs.size(); ++i) rem_t += (i > t_idx ? "/" : "") + t_segs[i];
                 for(size_t i = p_idx; i < p_segs.size(); ++i) rem_p += (i > p_idx ? "/" : "") + p_segs[i];
                 if (topicMatches(rem_t, rem_p)) return true;
                 t_idx++;
            } return false;
        } else { if (p_segs[p_idx] != t_segs[t_idx]) return false; p_idx++; t_idx++; }
    }
    if (t_idx == t_segs.size() && p_idx < p_segs.size() && p_segs[p_idx] == "*" && p_idx == p_segs.size() - 1) p_idx++;
    return p_idx == p_segs.size() && t_idx == t_segs.size();
}


// --- Main Server Logic ---
int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <PORT>" << std::endl;
        return 1;
    }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        std::cerr << "ERROR: Invalid port number." << std::endl;
        return 1;
    }

    ServerSockets sockets = setup_server_sockets(port);
    if (sockets.tcp < 0 || sockets.udp < 0) return 1; // setup logs errors

    if (listen(sockets.tcp, MAX_CLIENTS) < 0) {
        close_server_sockets(sockets);
        error("ERROR on listen");
    }
    std::cerr << "Server started on port " << port << std::endl;

    SubscribersMap subscribers;
    PollFds poll_fds;
    SocketToIdMap socket_to_id;

    initialize_poll_fds(poll_fds, sockets);

    bool running = true;
    while (running) {
        int poll_count = poll(poll_fds.data(), poll_fds.size(), -1);
        if (poll_count < 0) { if (errno == EINTR) continue; error("ERROR on poll"); }

        // Order: Check stdin first, then listeners, then clients
        if (poll_fds[2].revents & POLLIN) { handle_stdin(running); if (!running) break; }
        if (poll_fds[0].revents & POLLIN) { handle_new_connection(sockets.tcp, poll_fds, subscribers, socket_to_id); }
        if (poll_fds[1].revents & POLLIN) { handle_udp_message(sockets.udp, subscribers); }
        handle_client_activity(poll_fds, subscribers, socket_to_id); // Check indices >= 3

        // Reset revents (can be done within handlers too, but less error prone here)
        for (auto& pfd_entry : poll_fds) pfd_entry.revents = 0;
    }

    close_server_sockets(sockets); // Close listening sockets
    // Client sockets are closed by handle_client_disconnection or implicitly here if server exits first
    // Optional: Message indicating shutdown completion
    // std::cout << "Server shut down complete." << std::endl;
    return 0;
}


// --- Socket Setup ---
static ServerSockets setup_server_sockets(int port) {
    ServerSockets sockets;
    int enable = 1;
    struct sockaddr_in server_addr;

    // TCP Socket
    sockets.tcp = socket(AF_INET, SOCK_STREAM, 0);
    if (sockets.tcp < 0) { error("ERROR opening TCP socket"); return sockets; } // error() exits
    if (setsockopt(sockets.tcp, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        close(sockets.tcp); error("ERROR setting SO_REUSEADDR on TCP"); return {-1,-1}; }

    // UDP Socket
    sockets.udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockets.udp < 0) { close(sockets.tcp); error("ERROR opening UDP socket"); return {-1,-1}; }
    if (setsockopt(sockets.udp, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        close(sockets.tcp); close(sockets.udp); error("ERROR setting SO_REUSEADDR on UDP"); return {-1,-1}; }

    // Address Configuration
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind Sockets
    if (bind(sockets.tcp, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        close(sockets.tcp); close(sockets.udp); error("ERROR binding TCP socket"); return {-1,-1}; }
    if (bind(sockets.udp, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        close(sockets.tcp); close(sockets.udp); error("ERROR binding UDP socket"); return {-1,-1}; }

    return sockets;
}

static void close_server_sockets(const ServerSockets& sockets) {
    if (sockets.tcp >= 0) close(sockets.tcp);
    if (sockets.udp >= 0) close(sockets.udp);
}

// --- Poll Initialization ---
static void initialize_poll_fds(PollFds& poll_fds, const ServerSockets& sockets) {
    poll_fds.clear(); // Ensure it's empty before initializing
    poll_fds.push_back({sockets.tcp, POLLIN, 0});   // TCP listening [index 0]
    poll_fds.push_back({sockets.udp, POLLIN, 0});   // UDP socket [index 1]
    poll_fds.push_back({STDIN_FILENO, POLLIN, 0}); // Standard input [index 2]
}

// --- Main Loop Event Handlers ---

// Handle STDIN commands (only "exit")
static void handle_stdin(bool& running) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    if (fgets(buffer, BUFFER_SIZE - 1, stdin) != NULL) {
        buffer[strcspn(buffer, "\n")] = 0;
        if (strcmp(buffer, "exit") == 0) {
            running = false;
        } // else: ignore unknown commands
    } else {
        // EOF or error on stdin, treat as exit
        running = false;
    }
}

// Accept new TCP connection, get client ID, handle connection/reconnection
static void handle_new_connection(int listener_socket, PollFds& poll_fds, SubscribersMap& subscribers, SocketToIdMap& socket_to_id) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_socket = accept(listener_socket, (struct sockaddr *) &client_addr, &client_len);
    if (client_socket < 0) { perror("WARN: accept failed"); return; }

    // Disable Nagle's algorithm
    int flag = 1;
    if (setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) < 0)
        perror("WARN: setsockopt TCP_NODELAY failed");

    std::string client_id_str;
    if (!receive_client_id(client_socket, client_id_str)) {
        // std::cerr << "WARN: Failed to receive client ID or client disconnected early." << std::endl;
        close(client_socket);
        return;
    }

    auto it = subscribers.find(client_id_str);
    if (it != subscribers.end()) { // ID exists
        if (it->second.connected) {
            std::cout << "Client " << client_id_str << " already connected." << std::endl;
            close(client_socket); // Close the new socket, keep the old one
        } else {
            handle_reconnection(it->second, client_socket, client_addr, poll_fds, socket_to_id);
        }
    } else { // New client ID
        handle_new_client(client_id_str, client_socket, client_addr, poll_fds, subscribers, socket_to_id);
    }
}

// Receive UDP message, parse it, and distribute to subscribers
static void handle_udp_message(int udp_socket, SubscribersMap& subscribers) {
    char buffer[BUFFER_SIZE];
    UdpMessage udp_msg;
    struct sockaddr_in udp_sender_addr;
    socklen_t udp_sender_len = sizeof(udp_sender_addr);

    memset(buffer, 0, BUFFER_SIZE);
    int bytes_received = recvfrom(udp_socket, buffer, BUFFER_SIZE - 1, 0,
                                (struct sockaddr *) &udp_sender_addr, &udp_sender_len);
    if (bytes_received <= 0) {
        if (bytes_received < 0) perror("WARN: recvfrom UDP failed");
        return;
    }

    // Populate UdpMessage struct from raw bytes
    if (!parse_udp_datagram(buffer, bytes_received, udp_msg)) {
        // std::cerr << "WARN: Received invalid UDP message format." << std::endl;
        return;
    }
    udp_msg.sender_addr = udp_sender_addr; // Store sender address

    // Format the message string once
    std::string formatted_msg_str = udp_msg.formatMessage();

    // Distribute message to relevant subscribers
    distribute_udp_message(udp_msg, formatted_msg_str, subscribers);
}


// Iterate through client sockets, check for activity (data/disconnect)
static void handle_client_activity(PollFds& poll_fds, SubscribersMap& subscribers, SocketToIdMap& socket_to_id) {
    char recv_tmp_buffer[BUFFER_SIZE]; // Temporary buffer for recv

    // Iterate backwards for safe removal from poll_fds
    for (int i = poll_fds.size() - 1; i >= 3; --i) {
        // Check index validity (might change due to removals) - already handled by loop condition and check below
         if (i >= (int)poll_fds.size()) continue;

        struct pollfd& pfd = poll_fds[i];
        int client_socket = pfd.fd;

        if (pfd.revents == 0) continue; // Skip sockets with no activity

        auto id_it = socket_to_id.find(client_socket);
        if (id_it == socket_to_id.end()) {
             // Should not happen if maps are consistent, indicates an issue
             // std::cerr << "WARN: No client ID found for active socket fd " << client_socket << ". Closing." << std::endl;
             close(client_socket);
             poll_fds.erase(poll_fds.begin() + i);
             continue;
        }
        std::string client_id_str = id_it->second;
        auto sub_it = subscribers.find(client_id_str);
        if (sub_it == subscribers.end()) {
            // Should not happen if maps are consistent
            // std::cerr << "WARN: No subscriber object found for client ID " << client_id_str << ". Closing." << std::endl;
            close(client_socket);
            socket_to_id.erase(id_it); // Remove from socket map too
            poll_fds.erase(poll_fds.begin() + i);
            continue;
        }
        Subscriber& sub = sub_it->second;

        bool client_disconnected = false;

        // Handle disconnection/error first
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            if (sub.connected) { // Check if we haven't already marked it disconnected
                std::cout << "Client " << client_id_str << " disconnected (poll error/hup)." << std::endl;
            }
            client_disconnected = true;
        }
        // Check for incoming data
        else if (pfd.revents & POLLIN) {
            memset(recv_tmp_buffer, 0, BUFFER_SIZE); // Clear temp buffer
            int bytes_received = recv(client_socket, recv_tmp_buffer, BUFFER_SIZE - 1, 0);

            if (bytes_received <= 0) { // Disconnection or recv error
                if (bytes_received < 0 && errno != ECONNRESET && errno != EINTR) {
                    perror("WARN: recv from client failed");
                }
                 if (sub.connected) { // Log only if not already known disconnected
                     std::cout << "Client " << client_id_str << " disconnected." << std::endl;
                 }
                client_disconnected = true;
            } else { // Received data
                // Write to buffer and process commands
                if (!sub.command_buffer.write(recv_tmp_buffer, bytes_received)) {
                    std::cerr << "ERROR: Client " << client_id_str << " command buffer overflow. Disconnecting." << std::endl;
                    client_disconnected = true; // Disconnect misbehaving client
                } else if (!process_commands_from_buffer(sub)) {
                    // Error during command processing (though current parser doesn't signal errors this way)
                    // If needed, add error handling here. For now, assume success.
                }
            }
        } // End POLLIN handling

        // Cleanup if client disconnected
        if (client_disconnected) {
            handle_client_disconnection(client_socket, i, client_id_str, poll_fds, subscribers, socket_to_id);
            // The loop continues to the next index (decremented automatically by `i--`)
        }
    } // End loop through client sockets
}

// --- Client Connection Helpers ---

// Receives and validates the client ID string
static bool receive_client_id(int client_socket, std::string& client_id_str) {
    char buffer[BUFFER_SIZE]; // Use common buffer size
    memset(buffer, 0, BUFFER_SIZE);
    // Expect ID + null terminator. Read slightly more to detect overflow attempts.
    int bytes_received = recv(client_socket, buffer, MAX_ID_SIZE + 1, 0);

    if (bytes_received <= 0) { // Error or disconnect
        return false;
    }
    // Ensure received ID is null-terminated within buffer & max size
    buffer[std::min(bytes_received, MAX_ID_SIZE)] = '\0';

    // Check if ID contains problematic characters (like newline or null before end)
    if (strcspn(buffer, "\n\r\0") != (size_t)strlen(buffer)) {
         // std::cerr << "WARN: Client ID contains invalid characters." << std::endl;
         return false;
    }
     // Check if the actual ID length (before null) exceeds max size
    if (strlen(buffer) > MAX_ID_SIZE) {
        // std::cerr << "WARN: Received client ID exceeds max length." << std::endl;
        return false; // ID too long
    }


    client_id_str = buffer;
    return true;
}

// Handles a client reconnecting with an existing ID
static void handle_reconnection(Subscriber& sub, int new_socket, const struct sockaddr_in& client_addr, PollFds& poll_fds, SocketToIdMap& socket_to_id) {
    char client_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
    std::cout << "New client " << sub.id << " connected from "
              << client_ip_str << ":" << ntohs(client_addr.sin_port) << "." << std::endl;

    // Update the subscriber object
    sub.socket = new_socket;
    sub.connected = true;
    sub.command_buffer.reset(); // Clear any old command fragments

    // Add new socket to poll set and map
    poll_fds.push_back({new_socket, POLLIN, 0});
    socket_to_id[new_socket] = sub.id;

    // Send stored messages
    send_stored_messages(sub);
    sub.stored_messages.clear(); // Clear after attempting send
}

// Handles a new client connecting for the first time
static void handle_new_client(const std::string& client_id, int client_socket, const struct sockaddr_in& client_addr, PollFds& poll_fds, SubscribersMap& subscribers, SocketToIdMap& socket_to_id) {
    char client_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
    std::cout << "New client " << client_id << " connected from "
              << client_ip_str << ":" << ntohs(client_addr.sin_port) << "." << std::endl;

    // Create and insert the new subscriber using operator[] which default-constructs
    Subscriber& new_sub = subscribers[client_id];

    // Populate the fields
    strncpy(new_sub.id, client_id.c_str(), MAX_ID_SIZE);
    new_sub.id[MAX_ID_SIZE] = '\0'; // Ensure null termination
    new_sub.socket = client_socket;
    new_sub.connected = true;
    // command_buffer is already initialized by the constructor

    // Add to poll set and map
    poll_fds.push_back({client_socket, POLLIN, 0});
    socket_to_id[client_socket] = client_id;
}

// Sends messages stored for SF=1 clients upon reconnection
static void send_stored_messages(Subscriber& sub) {
    for (const std::string& stored_msg : sub.stored_messages) {
        // Send message + null terminator
        ssize_t sent = send_all(sub.socket, stored_msg.c_str(), stored_msg.length() + 1, MSG_NOSIGNAL);
        if (sent < 0 || (size_t)sent != stored_msg.length() + 1) {
            if (errno != EPIPE && errno != ECONNRESET) {
                perror("WARN: send stored message failed during reconnect");
            }
            // If send fails, stop sending more stored messages for this client now
            break;
        }
    }
}

// --- Client Activity Helpers ---

// Cleans up resources associated with a disconnected client
static void handle_client_disconnection(int client_socket, size_t poll_index, const std::string& client_id, PollFds& poll_fds, SubscribersMap& subscribers, SocketToIdMap& socket_to_id) {
    close(client_socket);

    auto sub_it = subscribers.find(client_id);
    if (sub_it != subscribers.end()) {
        sub_it->second.connected = false;
        sub_it->second.socket = -1;
        sub_it->second.command_buffer.reset(); // Clear buffer on disconnect
        // Keep sub_it->second.topics and sub_it->second.stored_messages for potential reconnect
    }

    socket_to_id.erase(client_socket); // Erase using socket FD as key
    poll_fds.erase(poll_fds.begin() + poll_index); // Erase using index
}

// Reads data from client socket into buffer and processes complete commands
// Returns true if processing was successful, false if buffer write failed.
static bool process_client_data(Subscriber& sub) {
    // Note: This function is integrated into handle_client_activity now.
    // Reading into a temporary buffer happens there.
    // This function's role is now just processing commands from the buffer.
    return process_commands_from_buffer(sub);
}

// Processes newline-delimited commands from the subscriber's buffer
// Returns true (always, in current implementation)
static bool process_commands_from_buffer(Subscriber& sub) {
    ssize_t newline_offset;
    while ((newline_offset = sub.command_buffer.find('\n')) >= 0) {
        // Extract command line up to (but not including) newline
        std::string command_line = sub.command_buffer.substr(0, newline_offset);

        // Consume the command line AND the newline delimiter
        sub.command_buffer.consume(newline_offset + 1);

        // Trim whitespace (optional but robust)
        command_line.erase(0, command_line.find_first_not_of(" \t\r\n"));
        command_line.erase(command_line.find_last_not_of(" \t\r\n") + 1);

        if (command_line.empty()) continue;

        parse_and_execute_command(sub, command_line);
    }
    return true; // Indicate success (no buffer errors detected here)
}

// Parses a single command line and updates subscriber state
static void parse_and_execute_command(Subscriber& sub, const std::string& command_line) {
    std::stringstream ss(command_line);
    std::string command_verb;
    ss >> command_verb;

    if (command_verb == "subscribe") {
        std::string topic;
        int sf = -1; // Use -1 to detect missing SF value
        if (ss >> topic >> sf && (sf == 0 || sf == 1) && ss.eof()) {
            if (topic.length() > TOPIC_SIZE) {
                // Warn or ignore oversized topic
                // std::cerr << "WARN: Client " << sub.id << " oversized topic subscribe: " << topic << std::endl;
            } else {
                sub.topics[topic] = (sf == 1);
            }
        } else {
            // Warn about invalid format
            // std::cerr << "WARN: Client " << sub.id << " invalid subscribe format: " << command_line << std::endl;
        }
    } else if (command_verb == "unsubscribe") {
        std::string topic;
        if (ss >> topic && ss.eof()) {
             if (topic.length() > TOPIC_SIZE) {
                // Warn or ignore oversized topic
                // std::cerr << "WARN: Client " << sub.id << " oversized topic unsubscribe: " << topic << std::endl;
            } else {
                 sub.topics.erase(topic); // OK if topic doesn't exist
            }
        } else {
            // Warn about invalid format
            // std::cerr << "WARN: Client " << sub.id << " invalid unsubscribe format: " << command_line << std::endl;
        }
    } else {
        // Warn about unknown command
        // std::cerr << "WARN: Client " << sub.id << " unknown command: " << command_verb << std::endl;
    }
}

// --- UDP Message Helpers ---

// Parses the raw UDP datagram into the UdpMessage struct
static bool parse_udp_datagram(const char* buffer, int bytes_received, UdpMessage& udp_msg) {
    memset(&udp_msg, 0, sizeof(UdpMessage)); // Zero out the structure first

    // Topic is first TOPIC_SIZE bytes
    int topic_len_to_copy = std::min(bytes_received, TOPIC_SIZE);
    memcpy(udp_msg.topic, buffer, topic_len_to_copy);
    // Ensure null termination, even if topic fills TOPIC_SIZE exactly
    udp_msg.topic[std::min(topic_len_to_copy, TOPIC_SIZE)] = '\0';

    // Type byte is immediately after the topic space
    if (bytes_received < (TOPIC_SIZE + 1)) {
        return false; // Message too short for topic + type
    }
    udp_msg.type = (uint8_t)buffer[TOPIC_SIZE];

    // Content starts after topic and type
    udp_msg.content_len = bytes_received - (TOPIC_SIZE + 1);
    if (udp_msg.content_len < 0) udp_msg.content_len = 0; // Should not happen if previous check passed

    // Copy content, ensuring not to overflow MAX_CONTENT_SIZE
    if (udp_msg.content_len > 0) {
        int content_len_to_copy = std::min(udp_msg.content_len, MAX_CONTENT_SIZE);
        memcpy(udp_msg.content, buffer + TOPIC_SIZE + 1, content_len_to_copy);
        // Add null terminator for safety (useful for STRING type, harmless for others)
        udp_msg.content[std::min(content_len_to_copy, MAX_CONTENT_SIZE)] = '\0';
    } else {
        udp_msg.content[0] = '\0'; // Ensure content is null-terminated if empty
    }
     // Update content_len to reflect potentially truncated content
    udp_msg.content_len = std::min(udp_msg.content_len, MAX_CONTENT_SIZE);


    return true; // Successfully parsed
}

// Sends the formatted UDP message to matching subscribers
static void distribute_udp_message(const UdpMessage& msg, const std::string& formatted_msg, SubscribersMap& subscribers) {
    std::string topic_str(msg.topic);

    for (auto& pair : subscribers) {
        Subscriber& sub = pair.second;
        bool matched = false; // Track if *any* pattern matched for this subscriber

        for (const auto& topic_pair : sub.topics) {
            const std::string& pattern = topic_pair.first;
            bool sf_enabled = topic_pair.second;

            if (topicMatches(topic_str, pattern)) {
                matched = true; // A pattern matched
                if (sub.connected) {
                    ssize_t sent = send_all(sub.socket, formatted_msg.c_str(), formatted_msg.length() + 1, MSG_NOSIGNAL);
                    if (sent < 0 || (size_t)sent != formatted_msg.length() + 1) {
                         if (errno != EPIPE && errno != ECONNRESET) {
                             perror("WARN: send_all to subscriber failed");
                         }
                         // Let the main poll loop detect and handle the disconnect.
                    }
                } else if (sf_enabled) {
                    sub.stored_messages.push_back(formatted_msg);
                }
                // else: Matched, but disconnected and SF=0 -> Drop message

                // Important: Break inner loop once a match is found for this subscriber.
                // A subscriber receives the message at most once per UDP datagram.
                break;
            } // end if topicMatches
        } // end loop through subscriber's topic patterns

        // No need for goto, the inner loop break serves the purpose.
    } // end loop through subscribers
}