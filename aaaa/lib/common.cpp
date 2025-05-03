#include "common.h"
#include <cstdio>
#include <cstdlib>

void error(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

ssize_t send_all(int sockfd, const void *buf, size_t len, int flags)
{
    size_t total = 0;
    const char *ptr = (const char *)buf;
    while (total < len)
    {
        ssize_t bytes_sent = send(sockfd, ptr + total, len - total, flags);
        if (bytes_sent < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            perror("send_all failed");
            return -1;
        }
        if (bytes_sent == 0)
        {
            std::cerr << "WARN: send returned 0" << std::endl;
            return total;
        }
        total += bytes_sent;
    }
    return total;
}
