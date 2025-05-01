#ifndef SERVER_STATE_H
#define SERVER_STATE_H

#include "server.h" // Includes Subscriber struct, map, set etc. needed below
#include <poll.h>
#include <vector>
#include <map>
#include <string>

// Type definitions for clarity and easier passing
using PollFds = std::vector<struct pollfd>;
using SubscribersMap = std::map<std::string, Subscriber>;
using SocketToIdMap = std::map<int, std::string>;

// Structure to hold the main server state (optional, but can group maps)
// Decided against using this struct to avoid potential confusion with "global state"
// We will pass the maps directly.

#endif // SERVER_STATE_H