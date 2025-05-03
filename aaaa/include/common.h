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
#include <netinet/tcp.h> 


#define BUFFER_SIZE 1600
#define CIRCULAR_BUFFER_SIZE (4 * BUFFER_SIZE)
#define TOPIC_SIZE 50
#define MAX_CONTENT_SIZE 1500
#define MAX_ID_SIZE 10

void error(const char *msg);

ssize_t send_all(int sockfd, const void *buf, size_t len, int flags);

#endif // COMMON_H