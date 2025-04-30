#include "subscriber.h"
#include <cstdio>   // For setvbuf
#include <cstdlib>  // For atoi, exit


// --- Helper function declarations for main logic (optional modularity) ---
static void handle_user_input(int client_socket, bool& running);
static void handle_server_message(int client_socket, std::string& server_buffer, bool& running);


// --- Main Client Logic ---
int main(int argc, char *argv[]) {
    // Disable stdout buffering
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    // --- Argument Parsing ---
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <ID_CLIENT> <IP_SERVER> <PORT_SERVER>" << std::endl;
        return 1;
    }

    std::string client_id = argv[1];
    if (client_id.length() > MAX_ID_SIZE) {
         std::cerr << "ERROR: Client ID too long (max " << MAX_ID_SIZE << " characters)." << std::endl;
         return 1;
    }
    const char* server_ip_cstr = argv[2];
    int server_port = atoi(argv[3]);
     if (server_port <= 0 || server_port > 65535) {
         std::cerr << "ERROR: Invalid server port." << std::endl;
         return 1;
    }

    // --- Socket Creation and Setup ---
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) error("ERROR opening socket");

    // Disable Nagle's algorithm
    int flag = 1;
    if (setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) < 0)
        perror("WARN: setsockopt TCP_NODELAY failed"); // Non-fatal

    // Configure server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip_cstr, &server_addr.sin_addr) <= 0)
        error("ERROR invalid server IP address");

    // --- Connect to Server ---
    if (connect(client_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0)
        error("ERROR connecting to server");

    // --- Send Client ID ---
    // Use send_all for robustness (send ID + null terminator)
    if (send_all(client_socket, client_id.c_str(), client_id.length() + 1, 0) < 0) {
        error("ERROR sending client ID"); // send_all prints details via perror
    }
    // Server prints connection confirmation

    // --- Polling Setup ---
    struct pollfd poll_fds[2];
    poll_fds[0].fd = STDIN_FILENO;  // Standard input [index 0]
    poll_fds[0].events = POLLIN;
    poll_fds[1].fd = client_socket; // Server socket [index 1]
    poll_fds[1].events = POLLIN;

    std::string server_buffer; // Buffer for potentially fragmented messages
    bool running = true;

    // --- Main Client Loop ---
    while (running) {
        // Wait indefinitely for events
        int poll_count = poll(poll_fds, 2, -1);
        if (poll_count < 0) {
             if (errno == EINTR) continue; // Interrupted by signal
             error("ERROR on poll");
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

        // Reset revents for next poll
        poll_fds[0].revents = 0;
        poll_fds[1].revents = 0;

    } // End while(running)

    // --- Clean up ---
    close(client_socket);
    // Optional: message indicating shutdown
    // std::cout << "Subscriber shut down." << std::endl;
    return 0;
}


// --- Helper Function Implementations ---

static void handle_user_input(int client_socket, bool& running) {
    char buffer[BUFFER_SIZE]; // Use common buffer size
    memset(buffer, 0, BUFFER_SIZE);
    if (fgets(buffer, BUFFER_SIZE - 1, stdin) == NULL) {
         // EOF on stdin, treat as exit command
         running = false;
         return;
    }

    // Remove trailing newline
    buffer[strcspn(buffer, "\n")] = 0;
    std::string input_line(buffer);

    // Trim whitespace (optional but robust)
    input_line.erase(0, input_line.find_first_not_of(" \t\r\n"));
    input_line.erase(input_line.find_last_not_of(" \t\r\n") + 1);

    if (input_line.empty()) return; // Ignore empty lines

    std::stringstream ss(input_line);
    std::string command_verb;
    ss >> command_verb;

    if (command_verb == "exit") {
        running = false;
    } else if (command_verb == "subscribe") {
        std::string topic;
        int sf_val = 0; // Default SF is 0 according to original logic
        // Check format: subscribe <topic> [<sf>] (original logic only had <topic>)
        // We will stick to original: subscribe <topic> implies SF=0
        if (ss >> topic && ss.eof()) { // Only topic expected
            if (topic.length() > TOPIC_SIZE) {
                std::cout << "ERROR: Topic too long (max " << TOPIC_SIZE << " characters)." << std::endl;
            } else {
                // Format command string for server (subscribe <topic> <sf>\n)
                std::string cmd = "subscribe " + topic + " " + std::to_string(sf_val) + "\n";
                if (send_all(client_socket, cmd.c_str(), cmd.size(), 0) < 0) {
                    // Error already printed by send_all
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
        // Check format: unsubscribe <topic>
        if (ss >> topic && ss.eof()) {
             if (topic.length() > TOPIC_SIZE) {
                std::cout << "ERROR: Topic too long (max " << TOPIC_SIZE << " characters)." << std::endl;
            } else {
                 // Format command string for server (unsubscribe <topic>\n)
                std::string cmd = "unsubscribe " + topic + "\n";
                if (send_all(client_socket, cmd.c_str(), cmd.size(), 0) < 0) {
                    // Error already printed by send_all
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


static void handle_server_message(int client_socket, std::string& server_buffer, bool& running) {
    char buffer[BUFFER_SIZE]; // Use common buffer size
    memset(buffer, 0, BUFFER_SIZE);
    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);

    if (bytes_received <= 0) {
        if (bytes_received < 0 && errno != ECONNRESET) {
            perror("ERROR receiving from server");
        }
        // If 0 or error, server has disconnected
        running = false;
        return;
    }

    // Append received data to buffer
    server_buffer.append(buffer, bytes_received);

    // Process complete messages (null-terminated) from the buffer
    size_t null_pos;
    while ((null_pos = server_buffer.find('\0')) != std::string::npos) {
         // Extract message up to (but not including) the null terminator
         std::string message = server_buffer.substr(0, null_pos);
         // Remove the message and the null terminator from the buffer
         server_buffer.erase(0, null_pos + 1);

         // Print the received message
         std::cout << message << std::endl;
    }
}
