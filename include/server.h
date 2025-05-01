#ifndef SERVER_H
#define SERVER_H

#include "common.h"          // Include shared definitions
#include "circular_buffer.h" // <<< Include Circular Buffer
#include <map>
#include <set>
#include <vector>
#include <string>
#include <netinet/tcp.h> // For TCP_NODELAY
#include <cmath>         // For pow in parseMessage (REMOVED - formatting moved)
#include <iomanip>       // For setprecision in parseMessage (REMOVED - formatting moved)
#include <netdb.h>       // Potentially needed for gethostbyname etc. (though not used directly now)
#include <climits>       // Potentially needed for limits (though not used directly now)

// --- Server Specific Constants ---
#define MAX_CLIENTS 100 // Maximum concurrent TCP clients

// --- Structures ---

// Structure to represent a TCP client (subscriber)
struct Subscriber
{
    int socket = -1; // Initialize socket to invalid
    char id[MAX_ID_SIZE + 1];
    std::map<std::string, bool> topics; // Map: Topic Pattern -> SF flag
    // <<< MODIFIED: Store raw serialized message packets
    std::vector<std::vector<char>> stored_messages; // For store-and-forward
    bool connected = false;
    CircularBuffer<char> command_buffer; // Buffer for incoming commands

    // Constructor to initialize circular buffer
    Subscriber() : command_buffer(2 * BUFFER_SIZE) {} // Initialize with common buffer size
};

// Structure for holding parsed UDP message data
struct UdpMessage
{
    char topic[TOPIC_SIZE + 1]; // Null terminated topic
    uint8_t type;
    char content[MAX_CONTENT_SIZE + 1]; // Raw content + potential null terminator space
    struct sockaddr_in sender_addr;
    int content_len; // Actual length of content received
};

// --- Function Declarations ---

// Function to match a topic against a pattern (with wildcards '+' and '*')
bool topicMatches(const std::string &topic, const std::string &pattern);

#endif // SERVER_H