#include "common.h"
#include <cstdio>  // For perror
#include <cstdlib> // For exit

void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// Implementation of send_all
ssize_t send_all(int sockfd, const void *buf, size_t len, int flags) {
    size_t total = 0;
    const char *ptr = (const char*) buf;
    while(total < len) {
        ssize_t bytes_sent = send(sockfd, ptr + total, len - total, flags);
        if(bytes_sent < 0) {
            // If interrupted by signal, try again
            if (errno == EINTR) continue;
            // Other errors are fatal for this send operation
            perror("send_all failed"); // Print error
            return -1; // Indicate error
        }
        if (bytes_sent == 0) {
             // Socket closed or error? Should not happen with blocking sockets unless len was 0.
             // This indicates an issue, return bytes sent so far.
             std::cerr << "WARN: send returned 0" << std::endl;
             return total;
        }
        total += bytes_sent;
    }
    return total; // Success, return total bytes sent (should equal len)
}
