#ifndef SERVER_H
#define SERVER_H

#include "common.h" // Include shared definitions
#include "circular_buffer.h" // <<< Include Circular Buffer
#include <map>
#include <set>
#include <vector>
#include <string>
#include <netinet/tcp.h> // For TCP_NODELAY
#include <cmath>         // For pow in parseMessage
#include <iomanip>       // For setprecision in parseMessage
#include <netdb.h>       // Potentially needed for gethostbyname etc. (though not used directly now)
#include <climits>       // Potentially needed for limits (though not used directly now)


// --- Server Specific Constants ---
#define MAX_CLIENTS 100     // Maximum concurrent TCP clients

// --- Structures ---

// Structure to represent a TCP client (subscriber)
struct Subscriber {
    int socket = -1; // Initialize socket to invalid
    char id[MAX_ID_SIZE + 1];
    std::map<std::string, bool> topics;  // Map: Topic Pattern -> SF flag
    std::vector<std::string> stored_messages; // For store-and-forward
    bool connected = false;
    // std::string command_buffer; // <<< REMOVED
    CircularBuffer<char> command_buffer; // <<< ADDED: Buffer for incoming commands

    // <<< ADDED: Constructor to initialize circular buffer
    Subscriber() : command_buffer(2 * BUFFER_SIZE) {} // Initialize with common buffer size
};

// Structure for holding parsed UDP message data
struct UdpMessage {
    char topic[TOPIC_SIZE + 1]; // Null terminated topic
    uint8_t type;
    char content[MAX_CONTENT_SIZE + 1]; // Raw content + potential null terminator space
    struct sockaddr_in sender_addr;
    int content_len; // Actual length of content received

    std::string parseMessage();
};

// --- Function Declarations ---

// Function to match a topic against a pattern (with wildcards '+' and '*')
bool topicMatches(const std::string& topic, const std::string& pattern);

// Function to parse UDP message content and format it into a string for subscribers
std::string parseMessage(const UdpMessage& msg);

#endif // SERVER_H