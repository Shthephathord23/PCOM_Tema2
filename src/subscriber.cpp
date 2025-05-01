// File: ../../src/subscriber.cpp
#include "subscriber.h"
#include "circular_buffer.h" // Include Circular Buffer
#include <cstdio>   // For setvbuf
#include <cstdlib>  // For atoi, exit

// --- Helper Function Declarations ---

// Parses command line arguments
static bool parse_arguments(int argc, char *argv[], std::string& client_id, std::string& server_ip, int& server_port);

// Sets up the TCP socket, connects to the server, and sets TCP_NODELAY
static int setup_and_connect(const std::string& server_ip, int server_port);

// Sends the client ID to the server
static bool send_client_id(int client_socket, const std::string& client_id);

// Initializes the pollfd array for monitoring stdin and the server socket
static void initialize_poll_fds(std::vector<struct pollfd>& poll_fds, int client_socket);

// Handles input from stdin (subscribe, unsubscribe, exit)
static void handle_user_input(int client_socket, bool& running);

// Receives data from the server into the circular buffer
// Returns number of bytes received, 0 on clean disconnect, -1 on error
static ssize_t receive_server_data(int client_socket, CircularBuffer<char>& server_buffer);

// Processes fully received null-terminated messages from the buffer
static void process_messages_from_buffer(CircularBuffer<char>& server_buffer);

// Handles messages received from the server socket
static void handle_server_message(int client_socket, CircularBuffer<char>& server_buffer, bool& running);

// The main loop for handling events from stdin and the server
static void subscriber_loop(int client_socket, std::vector<struct pollfd>& poll_fds);

// --- Main Client Logic ---
int main(int argc, char *argv[]) {
    // Disable stdout buffering
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    std::string client_id;
    std::string server_ip;
    int server_port;

    if (!parse_arguments(argc, argv, client_id, server_ip, server_port)) {
        return 1;
    }

    int client_socket = setup_and_connect(server_ip, server_port);

    if (!send_client_id(client_socket, client_id)) {
        close(client_socket);
        return 1;
    }
    // Server prints connection confirmation

    std::vector<struct pollfd> poll_fds(2);
    
    initialize_poll_fds(poll_fds, client_socket);

    subscriber_loop(client_socket, poll_fds);

    // --- Clean up ---
    close(client_socket);
    // Optional: message indicating shutdown
    // std::cout << "Subscriber shut down." << std::endl;
    return 0;
}

// --- Helper Function Implementations ---

static bool parse_arguments(int argc, char *argv[], std::string& client_id, std::string& server_ip, int& server_port) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <ID_CLIENT> <IP_SERVER> <PORT_SERVER>" << std::endl;
        return false;
    }

    client_id = argv[1];
    if (client_id.length() > MAX_ID_SIZE) {
        std::cerr << "ERROR: Client ID too long (max " << MAX_ID_SIZE << " characters)." << std::endl;
        return false;
    }

    server_ip = argv[2]; // Validation happens during inet_pton

    server_port = atoi(argv[3]);
    if (server_port <= 0 || server_port > 65535) {
        std::cerr << "ERROR: Invalid server port." << std::endl;
        return false;
    }
    return true;
}

static int setup_and_connect(const std::string& server_ip, int server_port) {
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        error("ERROR opening socket"); // Exits
    }

    // Disable Nagle's algorithm
    int flag = 1;
    if (setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) < 0) {
        perror("WARN: setsockopt TCP_NODELAY failed"); // Non-fatal
    }

    // Configure server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
        close(client_socket); // Close socket before calling error
        error("ERROR invalid server IP address"); // Exits
    }

    // Connect to Server
    if (connect(client_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        close(client_socket); // Close socket before calling error
        error("ERROR connecting to server"); // Exits
    }

    return client_socket;
}

static bool send_client_id(int client_socket, const std::string& client_id) {
    // Send ID + null terminator
    if (send_all(client_socket, client_id.c_str(), client_id.length() + 1, 0) < 0) {
        // error() is not called here as send_all prints the error
        std::cerr << "ERROR sending client ID failed." << std::endl;
        return false;
    }
    return true;
}

static void initialize_poll_fds(std::vector<struct pollfd>& poll_fds, int client_socket) {
    poll_fds[0].fd = STDIN_FILENO;  // Standard input [index 0]
    poll_fds[0].events = POLLIN;
    poll_fds[0].revents = 0;
    poll_fds[1].fd = client_socket; // Server socket [index 1]
    poll_fds[1].events = POLLIN;
    poll_fds[1].revents = 0;
}

static void subscriber_loop(int client_socket, std::vector<struct pollfd>& poll_fds) {
    CircularBuffer<char> server_buffer(2 * BUFFER_SIZE); // Buffer for server messages
    bool running = true;

    while (running) {
        int poll_count = poll(poll_fds.data(), 2, -1); // Wait indefinitely
        if (poll_count < 0) {
            if (errno == EINTR) continue; // Interrupted by signal
            error("ERROR on poll"); // Exits
        }

        // Check STDIN [index 0]
        if (poll_fds[0].revents & POLLIN) {
            handle_user_input(client_socket, running);
        }

        // Check Server Socket [index 1] for data or errors
        if (poll_fds[1].revents & POLLIN) {
            handle_server_message(client_socket, server_buffer, running);
        } else if (poll_fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            std::cerr << "ERROR: Server connection error/hangup." << std::endl;
            running = false;
        }

        // Reset revents for next poll (only necessary if reusing the same struct array)
        for (auto& pfd : poll_fds) {
            pfd.revents = 0;
        }
        
    }
}

// Handles user input from stdin
static void handle_user_input(int client_socket, bool& running) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    if (fgets(buffer, BUFFER_SIZE - 1, stdin) == NULL) {
        running = false; // EOF on stdin, treat as exit command
        return;
    }

    buffer[strcspn(buffer, "\n")] = 0; // Remove trailing newline
    std::string input_line(buffer);

    // Trim whitespace
    input_line.erase(0, input_line.find_first_not_of(" \t\r\n"));
    input_line.erase(input_line.find_last_not_of(" \t\r\n") + 1);

    if (input_line.empty()) return;

    std::stringstream ss(input_line);
    std::string command_verb;
    ss >> command_verb;

    if (command_verb == "exit") {
        running = false;
    } else if (command_verb == "subscribe") {
        std::string topic;
        int sf_val = 0; // Default SF=0
        if (ss >> topic && ss.eof()) { // Expecting: subscribe <topic>
            if (topic.length() > TOPIC_SIZE) {
                std::cout << "ERROR: Topic too long (max " << TOPIC_SIZE << " characters)." << std::endl;
            } else {
                std::string cmd = "subscribe " + topic + " " + std::to_string(sf_val) + "\n";
                if (send_all(client_socket, cmd.c_str(), cmd.size(), 0) < 0) {
                    running = false; // Assume connection lost
                } else {
                    std::cout << "Subscribed to topic." << std::endl;
                }
            }
        } else {
            std::cout << "Usage: subscribe <topic>" << std::endl;
        }
    } else if (command_verb == "unsubscribe") {
        std::string topic;
        if (ss >> topic && ss.eof()) { // Expecting: unsubscribe <topic>
             if (topic.length() > TOPIC_SIZE) {
                std::cout << "ERROR: Topic too long (max " << TOPIC_SIZE << " characters)." << std::endl;
            } else {
                std::string cmd = "unsubscribe " + topic + "\n";
                if (send_all(client_socket, cmd.c_str(), cmd.size(), 0) < 0) {
                    running = false; // Assume connection lost
                } else {
                    std::cout << "Unsubscribed from topic." << std::endl;
                }
            }
        } else {
            std::cout << "Usage: unsubscribe <topic>" << std::endl;
        }
    } else {
        std::cout << "Unknown command. Available: subscribe, unsubscribe, exit." << std::endl;
    }
}

// Receives data into the buffer. Returns bytes received, 0 on disconnect, -1 on error.
static ssize_t receive_server_data(int client_socket, CircularBuffer<char>& server_buffer) {
    char recv_tmp_buffer[BUFFER_SIZE];
    memset(recv_tmp_buffer, 0, BUFFER_SIZE); // Not strictly necessary, but safe

    ssize_t bytes_received = recv(client_socket, recv_tmp_buffer, BUFFER_SIZE - 1, 0);

    if (bytes_received > 0) {
        if (!server_buffer.write(recv_tmp_buffer, bytes_received)) {
            // Buffer overflow - fatal error for the subscriber
            std::cerr << "ERROR: Subscriber buffer overflow. Server data potentially lost. Disconnecting." << std::endl;
            return -2; // Indicate buffer overflow error
        }
    } else if (bytes_received == 0) {
        // Server closed connection gracefully
        std::cerr << "Server closed connection." << std::endl;
    } else { // bytes_received < 0
        if (errno != ECONNRESET && errno != EINTR) { // EINTR should be handled by poll loop
            perror("ERROR receiving from server");
        } else if (errno == ECONNRESET) {
             std::cerr << "Server closed connection (reset)." << std::endl;
        }
        // Treat any error or reset as a disconnect signal
    }
    return bytes_received;
}

// Processes complete messages (null-terminated) from the buffer
static void process_messages_from_buffer(CircularBuffer<char>& server_buffer) {
    ssize_t null_offset;
    while ((null_offset = server_buffer.find('\0')) >= 0) {
        // Extract message up to (but not including) the null terminator
        std::string message = server_buffer.substr(0, null_offset);

        // Consume the message AND the null terminator
        server_buffer.consume(null_offset + 1);

        // Print the received message
        std::cout << message << std::endl;
    }
}


// Handles messages received from the server socket using the circular buffer
static void handle_server_message(int client_socket, CircularBuffer<char>& server_buffer, bool& running) {
    ssize_t bytes_received = receive_server_data(client_socket, server_buffer);

    if (bytes_received <= 0 || bytes_received == -2) { // Disconnect, error, or buffer overflow
        running = false; // Signal loop termination
        return;
    }

    // Process any complete messages now in the buffer
    process_messages_from_buffer(server_buffer);
}