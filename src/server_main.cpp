#include "common.h"             // For error(), constants
#include "server_state.h"       // Type definitions
#include "server_network.h"     // Socket setup, poll init
#include "server_connection.h"  // New connection handling
#include "server_udp.h"         // UDP handling
#include "server_client.h"      // Client activity handling
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <cstdlib>              // For atoi, exit
#include <cstdio>               // For setvbuf
#include <cstring>              // For strcmp, memset, strcspn
#include <poll.h>
#include <unistd.h>             // For STDIN_FILENO

// --- Main Loop Helpers ---
static void handle_stdin_command(bool& running_flag);

// --- Main Server Logic ---
int main(int argc, char *argv[]) {
    // Ensure immediate output flushing, crucial for testing environments
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    setvbuf(stderr, NULL, _IONBF, BUFSIZ);


    // --- Argument Parsing ---
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <PORT>" << std::endl;
        return 1;
    }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        std::cerr << "ERROR: Invalid port number." << std::endl;
        return 1;
    }

    // --- Server Setup ---
    ServerSockets server_sockets = setup_server_sockets(port);
    // setup_server_sockets calls error() and exits on failure

    // Required Log Message:
    std::cerr << "Server started on port " << port << std::endl;

    // --- State Initialization ---
    SubscribersMap subscribers; // Stores data for each client ID
    PollFds poll_fds;           // Stores file descriptors for poll()
    SocketToIdMap socket_to_id; // Maps active socket FD -> client ID

    initialize_poll_fds(poll_fds, server_sockets);

    // --- Main Event Loop ---
    bool server_running = true;
    while (server_running) {
        // Wait indefinitely for I/O events
        int poll_count = poll(poll_fds.data(), poll_fds.size(), -1); // -1 timeout = wait forever
        if (poll_count < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, simply restart poll
            } else {
                error("ERROR on poll"); // Fatal error
            }
        }

        // --- Event Handling ---
        // Iterate through the pollfds array to check for events.
        // We check fixed indices first (listeners, stdin), then clients.

        // Check Standard Input (Index 2)
        if (poll_fds[2].revents & POLLIN) {
            handle_stdin_command(server_running);
            if (!server_running) break; // Exit immediately if stdin command was 'exit'
        }

        // Check TCP Listener Socket (Index 0)
        if (poll_fds[0].revents & POLLIN) {
            handle_new_connection(server_sockets.tcp, poll_fds, subscribers, socket_to_id);
        }

        // Check UDP Socket (Index 1)
        if (poll_fds[1].revents & POLLIN) {
            handle_udp_message(server_sockets.udp, subscribers);
        }

        // Check Connected TCP Clients (Indices >= 3)
        // handle_client_activity iterates backwards and handles removals safely.
        handle_client_activity(poll_fds, subscribers, socket_to_id);

        // Reset revents fields for the next poll() call - ESSENTIAL
        for (auto& pfd_entry : poll_fds) {
            pfd_entry.revents = 0;
        }

    } // End while(server_running)

    // --- Server Shutdown ---
    // Close all remaining client sockets (indices >= 3)
    for (size_t i = 3; i < poll_fds.size(); ++i) {
        close(poll_fds[i].fd);
        // No need to update maps, server is exiting
    }

    // Close the main listening sockets
    close_server_sockets(server_sockets);

    // Optional: Log shutdown completion
    // std::cout << "Server shut down complete." << std::endl;

    return 0;
}


// --- Main Loop Helper Implementation ---

// Handles commands read from the server's standard input.
static void handle_stdin_command(bool& running_flag) {
    char stdin_buffer[BUFFER_SIZE];
    memset(stdin_buffer, 0, BUFFER_SIZE);

    if (fgets(stdin_buffer, BUFFER_SIZE - 1, stdin) != NULL) {
        // Remove trailing newline
        stdin_buffer[strcspn(stdin_buffer, "\n\r")] = 0;

        // Check for the "exit" command
        if (strcmp(stdin_buffer, "exit") == 0) {
            // No log message required by spec on exit command received.
            running_flag = false; // Signal the main loop to terminate
        } else if (strlen(stdin_buffer) > 0) {
             // Unknown command entered on server stdin. Ignore.
             // No spec log message required.
        }
    } else {
        // fgets returns NULL on EOF or error. Treat either as a signal to exit.
        // No spec log message required.
        running_flag = false;
    }
}