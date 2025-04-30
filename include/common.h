#ifndef COMMON_H
#define COMMON_H

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sstream>
#include <errno.h>

// --- Constants ---
#define BUFFER_SIZE 1600    // Buffer size for receiving messages (used by both)
#define TOPIC_SIZE 50       // Maximum length of a topic name (used/validated by both)
#define MAX_CONTENT_SIZE 1500 // Maximum length of UDP message content (server parsing, subscriber info)
#define MAX_ID_SIZE 10      // Maximum length of a client ID (used/validated by both)

// --- Error Handling ---
// Prints error message based on errno and exits.
void error(const char *msg);

// --- Utility Functions (Potentially reusable) ---
// Helper function to send data reliably over a socket.
ssize_t send_all(int sockfd, const void *buf, size_t len, int flags);

#endif // COMMON_H