#include "common.h"              // Common constants, functions (error)
#include "circular_buffer.h"     // The buffer implementation
#include "subscriber_network.h"  // Connection setup
#include "subscriber_io.h"       // IO handling (stdin, server)
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>              // For atoi, exit
#include <cstdio>               // For setvbuf
#include <poll.h>               // For poll structure and function
#include <unistd.h>             // For close()
#include <errno.h>              // For errno checking

// --- Helper Function Declaration ---
static bool parse_subscriber_arguments(int argc, char *argv[], std::string& client_id, std::string& server_ip, int& server_port);

// --- Main Subscriber Logic ---
int main(int argc, char *argv[]) {
    // Disable stdout buffering for immediate output, crucial for testers
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    setvbuf(stderr, NULL, _IONBF, BUFSIZ);


    // --- Argument Parsing ---
    std::string client_id;
    std::string server_ip;
    int server_port;
    if (!parse_subscriber_arguments(argc, argv, client_id, server_ip, server_port)) {
        // Error message already printed by parser
        return 1; // Exit with error code
    }

    // --- Network Setup ---
    int client_socket = setup_and_connect(server_ip, server_port);
    // setup_and_connect calls error() which exits on failure

    // --- Send Client ID ---
    if (!send_client_id_to_server(client_socket, client_id)) {
        // Error message printed by helper
        close(client_socket);
        return 1; // Exit with error code
    }
    // Server prints connection confirmation if successful

    // --- I/O Multiplexing Setup ---
    std::vector<struct pollfd> poll_fds(2); // 0: stdin, 1: server socket
    initialize_subscriber_poll_fds(poll_fds, client_socket);

    // --- Circular Buffer for Server Messages ---
    // Initialize buffer with a reasonable capacity (e.g., twice the network buffer size)
    CircularBuffer<char> server_message_buffer(2 * BUFFER_SIZE);

    // --- Main Event Loop ---
    bool running = true;
    while (running) {
        // Wait for I/O events indefinitely
        int poll_count = poll(poll_fds.data(), poll_fds.size(), -1); // -1 timeout
        if (poll_count < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, restart poll
            } else {
                error("ERROR on poll"); // Fatal poll error
            }
        }

        // --- Event Handling ---

        // Check for input from Standard Input (Index 0)
        if (poll_fds[0].revents & POLLIN) {
            handle_user_input_command(client_socket, running);
            // handle_user_input sets running=false if "exit" or send error
        }

        // Check for data or connection issues from Server Socket (Index 1)
        if (poll_fds[1].revents & POLLIN) {
            handle_server_message_data(client_socket, server_message_buffer, running);
            // handle_server_message_data sets running=false on disconnect/error
        } else if (poll_fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            // Server connection error (e.g., server crashed, network issue)
            // Required Log Message:
            std::cerr << "ERROR: Server connection error/hangup." << std::endl;
            running = false; // Signal loop termination
        }

         // Reset revents fields for the next poll() call (essential)
        // Although handle_user_input and handle_server_message only process
        // if POLLIN is set, it's good practice to clear all checked revents.
        poll_fds[0].revents = 0;
        poll_fds[1].revents = 0;

    } // End while(running)

    // --- Clean up ---
    close(client_socket); // Close the connection to the server

    // Optional: Message indicating shutdown
    // std::cout << "Subscriber shut down." << std::endl;

    return 0;
}

// --- Helper Function Implementation ---

static bool parse_subscriber_arguments(int argc, char *argv[], std::string& client_id, std::string& server_ip, int& server_port) {
    if (argc != 4) {
        // Required Usage Message:
        std::cerr << "Usage: " << argv[0] << " <ID_CLIENT> <IP_SERVER> <PORT_SERVER>" << std::endl;
        return false;
    }

    // Client ID
    client_id = argv[1];
    if (client_id.empty()) {
         std::cerr << "ERROR: Client ID cannot be empty." << std::endl;
         return false;
    }
    if (client_id.length() > MAX_ID_SIZE) {
        // Required Error Message:
        std::cerr << "ERROR: Client ID too long (max " << MAX_ID_SIZE << " characters)." << std::endl;
        return false;
    }
     // Add check for invalid characters like spaces? Spec doesn't forbid, but might be good.
     // Let's assume IDs are simple alphanumeric for now, matching original behavior.

    // Server IP Address
    server_ip = argv[2];
    // IP format validation happens later in inet_pton() within setup_and_connect

    // Server Port
    server_port = atoi(argv[3]);
    if (server_port <= 0 || server_port > 65535) {
        // Required Error Message:
        std::cerr << "ERROR: Invalid server port." << std::endl;
        return false;
    }

    return true; // Arguments parsed successfully
}