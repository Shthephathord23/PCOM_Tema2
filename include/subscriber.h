#ifndef SUBSCRIBER_H
#define SUBSCRIBER_H

#include "common.h" // Include shared definitions
#include <netinet/tcp.h> // For TCP_NODELAY
#include <netdb.h>       // For gethostbyname etc. (though inet_pton is used)

// No subscriber-specific structures or constants needed in the header for now

// Function declarations (send_all is now in common.h/cpp)
// void example_subscriber_function(); // If needed later

#endif // SUBSCRIBER_H
