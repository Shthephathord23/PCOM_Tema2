#ifndef SERVER_H
#define SERVER_H

#include "common.h"
#include "circular_buffer.h"
#include <map>
#include <set>
#include <vector>
#include <string>
#include <netinet/tcp.h>
#include <cmath>
#include <iomanip>
#include <netdb.h>
#include <climits>

#define MAX_CLIENTS 100

struct Subscriber
{
    int socket = -1;
    char id[MAX_ID_SIZE + 1];
    std::map<std::string, bool> topics;
    std::vector<std::vector<char>> stored_messages;
    bool connected = false;
    CircularBuffer<char> command_buffer;

    Subscriber() : command_buffer(CIRCULAR_BUFFER_SIZE) {}
};

struct UdpMessage
{
    char topic[TOPIC_SIZE + 1];
    uint8_t type;
    char content[MAX_CONTENT_SIZE + 1];
    struct sockaddr_in sender_addr;
    int content_len;
};

#endif // SERVER_H