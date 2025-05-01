#include "server.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <vector>
#include <arpa/inet.h>

using PollFds = std::vector<struct pollfd>;
using SubscribersMap = std::map<std::string, Subscriber>;
using SocketToIdMap = std::map<int, std::string>;


struct ServerSockets
{
    int tcp = -1;
    int udp = -1;
};

static ServerSockets setup_server_sockets(int port);
static void close_server_sockets(const ServerSockets &sockets);
static void initialize_poll_fds(PollFds &poll_fds, const ServerSockets &sockets);
static void handle_stdin(bool &running);
static void handle_new_connection(int listener_socket, PollFds &poll_fds, SubscribersMap &subscribers, SocketToIdMap &socket_to_id);
static void handle_udp_message(int udp_socket, SubscribersMap &subscribers);
static void handle_client_activity(PollFds &poll_fds, SubscribersMap &subscribers, SocketToIdMap &socket_to_id);
static bool receive_client_id(int client_socket, std::string &client_id_str);
static void handle_reconnection(Subscriber &sub, int new_socket, const struct sockaddr_in &client_addr, PollFds &poll_fds, SocketToIdMap &socket_to_id);
static void handle_new_client(const std::string &client_id, int client_socket, const struct sockaddr_in &client_addr, PollFds &poll_fds, SubscribersMap &subscribers, SocketToIdMap &socket_to_id);
static void send_stored_messages(Subscriber &sub);
static void handle_client_disconnection(int client_socket, size_t poll_index, const std::string &client_id, PollFds &poll_fds, SubscribersMap &subscribers, SocketToIdMap &socket_to_id);
static bool process_commands_from_buffer(Subscriber &sub);
static void parse_and_execute_command(Subscriber &sub, const std::string &command_line);
static bool parse_udp_datagram(const char *buffer, int bytes_received, UdpMessage &udp_msg);
static std::vector<char> serialize_forward_message(const UdpMessage &msg);
static void distribute_udp_message(const UdpMessage &msg, const std::vector<char> &serialized_packet, SubscribersMap &subscribers);
bool topicMatches(const std::string &topic, const std::string &pattern);

bool topicMatches(const std::string &topic, const std::string &pattern)
{
    std::vector<std::string> t_segs, p_segs;
    std::string segment;
    std::stringstream ss_t(topic);
    while (getline(ss_t, segment, '/'))
    {
        t_segs.push_back(segment);
    }
    std::stringstream ss_p(pattern);
    while (getline(ss_p, segment, '/'))
    {
        p_segs.push_back(segment);
    }

    size_t N = t_segs.size();
    size_t M = p_segs.size();
    std::vector<bool> prev_dp(M + 1, false);
    std::vector<bool> curr_dp(M + 1, false);
    prev_dp[0] = true;
    for (size_t j = 1; j <= M; ++j)
    {
        if (p_segs[j - 1] == "*")
        {
            prev_dp[j] = prev_dp[j - 1];
        }
    }

    for (size_t i = 1; i <= N; ++i)
    {
        curr_dp[0] = false;
        for (size_t j = 1; j <= M; ++j)
        {
            const std::string &p_seg = p_segs[j - 1];
            const std::string &t_seg = t_segs[i - 1];
            if (p_seg == "+")
            {
                curr_dp[j] = prev_dp[j - 1];
            }
            else if (p_seg == "*")
            {
                curr_dp[j] = curr_dp[j - 1] || prev_dp[j];
            }
            else
            {
                curr_dp[j] = (p_seg == t_seg) && prev_dp[j - 1];
            }
        }
        prev_dp = curr_dp;
    }
    return prev_dp[M];
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <PORT>" << std::endl;
        return 1;
    }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535)
    {
        std::cerr << "ERROR: Invalid port number." << std::endl;
        return 1;
    }

    ServerSockets sockets = setup_server_sockets(port);
    if (sockets.tcp < 0 || sockets.udp < 0)
    {
        return 1;
    }
    if (listen(sockets.tcp, MAX_CLIENTS) < 0)
    {
        close_server_sockets(sockets);
        error("ERROR on listen");
    }
    std::cerr << "Server started on port " << port << std::endl;

    SubscribersMap subscribers;
    PollFds poll_fds;
    SocketToIdMap socket_to_id;
    initialize_poll_fds(poll_fds, sockets);

    bool running = true;
    while (running)
    {
        int poll_count = poll(poll_fds.data(), poll_fds.size(), -1);
        if (poll_count < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            error("ERROR on poll");
        }

        if (poll_fds.size() > 2 && poll_fds[2].revents & POLLIN)
        {
            handle_stdin(running);
            if (!running)
            {
                break;
            }
        }
        if (poll_fds.size() > 0 && poll_fds[0].revents & POLLIN)
        {
            handle_new_connection(sockets.tcp, poll_fds, subscribers, socket_to_id);
        }
        if (poll_fds.size() > 1 && poll_fds[1].revents & POLLIN)
        {
            handle_udp_message(sockets.udp, subscribers);
        }
        handle_client_activity(poll_fds, subscribers, socket_to_id);

        for (auto &pfd_entry : poll_fds)
        {
            pfd_entry.revents = 0;
        }
    }
    close_server_sockets(sockets);
    return 0;
}

static ServerSockets setup_server_sockets(int port)
{
    ServerSockets sockets = {-1, -1};
    int enable = 1;
    struct sockaddr_in server_addr;
    sockets.tcp = socket(AF_INET, SOCK_STREAM, 0);
    if (sockets.tcp < 0)
    {
        error("ERROR opening TCP socket");
    }
    if (setsockopt(sockets.tcp, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    {
        close(sockets.tcp);
        error("ERROR setting SO_REUSEADDR on TCP");
    }
    sockets.udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockets.udp < 0)
    {
        close(sockets.tcp);
        error("ERROR opening UDP socket");
    }
    if (setsockopt(sockets.udp, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    {
        close(sockets.tcp);
        close(sockets.udp);
        error("ERROR setting SO_REUSEADDR on UDP");
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sockets.tcp, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        close(sockets.tcp);
        close(sockets.udp);
        error("ERROR binding TCP socket");
    }
    if (bind(sockets.udp, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        close(sockets.tcp);
        close(sockets.udp);
        error("ERROR binding UDP socket");
    }
    return sockets;
}
static void close_server_sockets(const ServerSockets &sockets)
{
    if (sockets.tcp >= 0)
    {
        close(sockets.tcp);
    }
    if (sockets.udp >= 0)
    {
        close(sockets.udp);
    }
}

static void initialize_poll_fds(PollFds &poll_fds, const ServerSockets &sockets)
{
    poll_fds.clear();
    poll_fds.push_back({sockets.tcp, POLLIN, 0});  // [0] TCP listener
    poll_fds.push_back({sockets.udp, POLLIN, 0});  // [1] UDP socket
    poll_fds.push_back({STDIN_FILENO, POLLIN, 0}); // [2] Standard input
}

// --- Main Loop Event Handlers ---
static void handle_stdin(bool &running)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    if (fgets(buffer, BUFFER_SIZE - 1, stdin) != NULL)
    {
        buffer[strcspn(buffer, "\n")] = 0;
        if (strcmp(buffer, "exit") == 0)
        {
            running = false;
        }
    }
    else
    {
        running = false;
    }
}

static void handle_new_connection(int listener_socket, PollFds &poll_fds, SubscribersMap &subscribers, SocketToIdMap &socket_to_id)
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_socket = accept(listener_socket, (struct sockaddr *)&client_addr, &client_len);
    if (client_socket < 0)
    {
        perror("WARN: accept failed");
        return;
    }
    int flag = 1;
    if (setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag,
                   sizeof(int)) < 0)
    {
        perror("WARN: setsockopt TCP_NODELAY failed");
    }

    std::string client_id_str;
    if (!receive_client_id(client_socket, client_id_str))
    {
        close(client_socket);
        return;
    }

    auto it = subscribers.find(client_id_str);
    if (it != subscribers.end())
    {
        if (it->second.connected)
        {
            std::cout << "Client " << client_id_str << " already connected." << std::endl;
            fflush(stdout);
            close(client_socket);
        }
        else
        {
            handle_reconnection(it->second, client_socket, client_addr, poll_fds, socket_to_id);
        }
    }
    else
    {
        handle_new_client(client_id_str, client_socket, client_addr, poll_fds, subscribers, socket_to_id);
    }
}

static void handle_udp_message(int udp_socket, SubscribersMap &subscribers)
{
    char buffer[BUFFER_SIZE];
    UdpMessage udp_msg;
    struct sockaddr_in udp_sender_addr;
    socklen_t udp_sender_len = sizeof(udp_sender_addr);
    memset(buffer, 0, BUFFER_SIZE);
    int bytes_received = recvfrom(udp_socket, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&udp_sender_addr, &udp_sender_len);
    if (bytes_received <= 0)
    {
        if (bytes_received < 0)
        {
            perror("WARN: recvfrom UDP failed");
        }
        return;
    }

    if (!parse_udp_datagram(buffer, bytes_received, udp_msg))
    {
        return;
    }
    udp_msg.sender_addr = udp_sender_addr;

    std::vector<char> serialized_packet = serialize_forward_message(udp_msg);
    distribute_udp_message(udp_msg, serialized_packet, subscribers);
}

static void handle_client_activity(PollFds &poll_fds, SubscribersMap &subscribers, SocketToIdMap &socket_to_id)
{
    char recv_tmp_buffer[BUFFER_SIZE];
    for (int i = poll_fds.size() - 1; i >= 3; --i)
    {
        if (i >= (int)poll_fds.size())
        {
            continue;
        }
        struct pollfd &pfd = poll_fds[i];
        int client_socket = pfd.fd;
        if (pfd.revents == 0)
        {
            continue;
        }

        auto id_it = socket_to_id.find(client_socket);
        if (id_it == socket_to_id.end())
        {
            close(client_socket);
            poll_fds.erase(poll_fds.begin() + i);
            continue;
        }
        std::string client_id_str = id_it->second;
        auto sub_it = subscribers.find(client_id_str);
        if (sub_it == subscribers.end())
        {
            close(client_socket);
            socket_to_id.erase(id_it);
            poll_fds.erase(poll_fds.begin() + i);
            continue;
        }
        Subscriber &sub = sub_it->second;

        bool client_disconnected = false;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            if (sub.connected)
            {
                std::cout << "Client " << client_id_str << " disconnected (poll error/hup)." << std::endl;
                fflush(stdout);
            }
            client_disconnected = true;
        }
        else if (pfd.revents & POLLIN)
        {
            memset(recv_tmp_buffer, 0, BUFFER_SIZE);
            int bytes_received = recv(client_socket, recv_tmp_buffer, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0)
            {
                if (bytes_received < 0 && errno != ECONNRESET && errno != EINTR &&
                    errno != EPIPE)
                {
                    perror("WARN: recv from client failed");
                }
                if (sub.connected)
                {
                    std::cout << "Client " << client_id_str << " disconnected." << std::endl;
                    fflush(stdout);
                }
                client_disconnected = true;
            }
            else
            {
                if (!sub.command_buffer.write(recv_tmp_buffer, bytes_received))
                {
                    std::cerr << "ERROR: Client " << client_id_str << " command buffer overflow. Disconnecting." << std::endl;
                    fflush(stderr);
                    client_disconnected = true;
                }
                else if (!process_commands_from_buffer(sub))
                {
                    std::cerr << "ERROR: Client " << client_id_str << " failed processing commands. Disconnecting." << std::endl;
                    fflush(stderr);
                    client_disconnected = true;
                }
            }
        }

        if (client_disconnected)
        {
            handle_client_disconnection(client_socket, i, client_id_str, poll_fds, subscribers, socket_to_id);
        }
    }
}

static bool receive_client_id(int client_socket, std::string &client_id_str)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    int bytes_received = recv(client_socket, buffer, MAX_ID_SIZE + 1, 0);
    if (bytes_received <= 0)
    {
        return false;
    }
    buffer[std::min(bytes_received, MAX_ID_SIZE)] = '\0';
    if (strcspn(buffer, "\n\r\0") != strlen(buffer))
    {
        return false;
    }
    if (strlen(buffer) > MAX_ID_SIZE)
    {
        return false;
    }
    client_id_str = buffer;
    return true;
}

static void handle_reconnection(Subscriber &sub, int new_socket, const struct sockaddr_in &client_addr, PollFds &poll_fds, SocketToIdMap &socket_to_id)
{
    char client_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
    std::cout << "New client " << sub.id << " connected from " << client_ip_str << ":" << ntohs(client_addr.sin_port) << "." << std::endl;
    fflush(stdout);
    sub.socket = new_socket;
    sub.connected = true;
    sub.command_buffer.reset();
    poll_fds.push_back({new_socket, POLLIN, 0});
    socket_to_id[new_socket] = sub.id;
    send_stored_messages(sub);
    sub.stored_messages.clear();
}

static void handle_new_client(const std::string &client_id, int client_socket, const struct sockaddr_in &client_addr, PollFds &poll_fds, SubscribersMap &subscribers, SocketToIdMap &socket_to_id)
{
    char client_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
    std::cout << "New client " << client_id << " connected from " << client_ip_str << ":" << ntohs(client_addr.sin_port) << "." << std::endl;
    fflush(stdout);
    Subscriber &new_sub = subscribers[client_id];
    strncpy(new_sub.id, client_id.c_str(), MAX_ID_SIZE);
    new_sub.id[MAX_ID_SIZE] = '\0';
    new_sub.socket = client_socket;
    new_sub.connected = true;
    poll_fds.push_back({client_socket, POLLIN, 0});
    socket_to_id[client_socket] = client_id;
}

static void send_stored_messages(Subscriber &sub)
{
    for (const std::vector<char> &stored_packet : sub.stored_messages)
    {
        ssize_t sent = send_all(sub.socket, stored_packet.data(), stored_packet.size(), MSG_NOSIGNAL);
        if (sent < 0 || (size_t)sent != stored_packet.size())
        {
            if (errno != EPIPE && errno != ECONNRESET)
            {
                perror("WARN: send stored message failed during reconnect");
            }
            break;
        }
    }
}

static void handle_client_disconnection(int client_socket, size_t poll_index, const std::string &client_id, PollFds &poll_fds, SubscribersMap &subscribers, SocketToIdMap &socket_to_id)
{
    close(client_socket);
    auto sub_it = subscribers.find(client_id);
    if (sub_it != subscribers.end())
    {
        sub_it->second.connected = false;
        sub_it->second.socket = -1;
        sub_it->second.command_buffer.reset();
    }
    socket_to_id.erase(client_socket);

    if (poll_index < poll_fds.size() && poll_fds[poll_index].fd == client_socket)
    {
        poll_fds.erase(poll_fds.begin() + poll_index);
    }
    else
    {
        auto pfd_it = std::find_if(poll_fds.begin() + 3, poll_fds.end(), [client_socket](const struct pollfd &p)
                                   { return p.fd == client_socket; });
        if (pfd_it != poll_fds.end())
        {
            poll_fds.erase(pfd_it);
        }
    }
}

static bool process_commands_from_buffer(Subscriber &sub)
{
    ssize_t newline_offset;
    while ((newline_offset = sub.command_buffer.find('\n')) >= 0)
    {
        std::string command_line = sub.command_buffer.substr(0, newline_offset);
        sub.command_buffer.consume(newline_offset + 1);
        command_line.erase(0, command_line.find_first_not_of(" \t\r\n"));
        command_line.erase(command_line.find_last_not_of(" \t\r\n") + 1);
        if (!command_line.empty())
        {
            parse_and_execute_command(sub, command_line);
        }
    }
    return true;
}

static void parse_and_execute_command(Subscriber &sub, const std::string &command_line)
{
    std::stringstream ss(command_line);
    std::string command_verb;
    ss >> command_verb;

    if (command_verb == "subscribe")
    {
        std::string topic;
        int sf = -1;
        if (ss >> topic >> sf && (sf == 0 || sf == 1) && ss.peek() == EOF)
        {
            if (topic.length() <= TOPIC_SIZE)
            {
                sub.topics[topic] = (sf == 1);
            }
            else
            {
                std::cerr << "ERROR: Topic too long (max " << TOPIC_SIZE << " characters)." << std::endl;
            }
        }
    }

    else if (command_verb == "unsubscribe")
    {
        std::string topic;
        if (ss >> topic && ss.peek() == EOF)
        {
            if (topic.length() <= TOPIC_SIZE)
            {
                sub.topics.erase(topic);
            }
            else
            {
                std::cerr << "ERROR: Topic too long (max " << TOPIC_SIZE << " characters)." << std::endl;
            }
        }
    }

    else
    {
        std::cerr << "ERROR: Unknown command." << std::endl;
    }
}

static bool parse_udp_datagram(const char *buffer, int bytes_received, UdpMessage &udp_msg)
{
    memset(&udp_msg, 0, sizeof(UdpMessage));
    int topic_len_to_copy = std::min(bytes_received, TOPIC_SIZE);
    memcpy(udp_msg.topic, buffer, topic_len_to_copy);
    udp_msg.topic[std::min(topic_len_to_copy, TOPIC_SIZE)] = '\0';

    if (bytes_received < (TOPIC_SIZE + 1))
    {
        return false;
    }
    udp_msg.type = (uint8_t)buffer[TOPIC_SIZE];

    udp_msg.content_len = bytes_received - (TOPIC_SIZE + 1);
    if (udp_msg.content_len < 0)
    {
        udp_msg.content_len = 0;
    }

    if (udp_msg.content_len > 0)
    {
        int content_len_to_copy = std::min(udp_msg.content_len, MAX_CONTENT_SIZE);
        memcpy(udp_msg.content, buffer + TOPIC_SIZE + 1, content_len_to_copy);
        udp_msg.content[content_len_to_copy] = '\0';
        udp_msg.content_len = content_len_to_copy;
    }
    else
    {
        udp_msg.content[0] = '\0';
    }
    return true;
}

// Serializes UDP message info into a binary packet for TCP forwarding
// Format: [Total Payload Len (NBO uint32_t)] [Sender IP (NBO uint32_t)] [Sender Port (NBO uint16_t)]
//         [Topic Len (uint8_t)] [Topic (char*)] [UDP Type (uint8_t)]
//         [Content Len (NBO uint16_t)] [Content (char*)]
static std::vector<char> serialize_forward_message(const UdpMessage &msg)
{
    std::vector<char> payload_buffer;
    payload_buffer.reserve(sizeof(uint32_t) + sizeof(uint16_t) + 1 + TOPIC_SIZE + 1 + sizeof(uint16_t) + MAX_CONTENT_SIZE + 10);

    // 1. Sender IP (Network Byte Order - Already NBO from recvfrom)
    uint32_t net_ip = msg.sender_addr.sin_addr.s_addr;
    const char *ip_bytes = reinterpret_cast<const char *>(&net_ip);
    payload_buffer.insert(payload_buffer.end(), ip_bytes, ip_bytes + sizeof(net_ip));

    // 2. Sender Port (Network Byte Order - Already NBO from recvfrom)
    uint16_t net_port = msg.sender_addr.sin_port;
    const char *port_bytes = reinterpret_cast<const char *>(&net_port);
    payload_buffer.insert(payload_buffer.end(), port_bytes, port_bytes + sizeof(net_port));

    // 3. Topic Length & Topic
    size_t topic_actual_len = strnlen(msg.topic, TOPIC_SIZE);
    uint8_t topic_len_byte = static_cast<uint8_t>(topic_actual_len);
    payload_buffer.push_back(topic_len_byte);
    payload_buffer.insert(payload_buffer.end(), msg.topic, msg.topic + topic_len_byte);

    // 4. UDP Type
    payload_buffer.push_back(msg.type);

    // 5. Content Length & Content (Network Byte Order for length)
    uint16_t content_len_16 = static_cast<uint16_t>(msg.content_len);
    uint16_t net_content_len = htons(content_len_16); // Convert length to NBO
    const char *content_len_bytes = reinterpret_cast<const char *>(&net_content_len);
    payload_buffer.insert(payload_buffer.end(), content_len_bytes, content_len_bytes + sizeof(net_content_len));
    if (msg.content_len > 0)
    {
        payload_buffer.insert(payload_buffer.end(), msg.content, msg.content + msg.content_len);
    }

    // 6. Prepend total payload length (Network Byte Order)
    std::vector<char> final_packet;
    uint32_t total_payload_len_32 = static_cast<uint32_t>(payload_buffer.size());
    uint32_t net_total_payload_len = htonl(total_payload_len_32); // Convert total length to NBO
    const char *total_len_bytes = reinterpret_cast<const char *>(&net_total_payload_len);
    // Reserve space for length prefix + payload
    final_packet.reserve(sizeof(net_total_payload_len) + payload_buffer.size());
    // Insert length prefix first
    final_packet.insert(final_packet.end(), total_len_bytes, total_len_bytes + sizeof(net_total_payload_len));
    // Insert payload after length prefix
    final_packet.insert(final_packet.end(), payload_buffer.begin(), payload_buffer.end());

    return final_packet;
}

static void distribute_udp_message(const UdpMessage &msg, const std::vector<char> &serialized_packet, SubscribersMap &subscribers)
{
    std::string topic_str(msg.topic);
    for (auto &pair : subscribers)
    {
        Subscriber &sub = pair.second;
        for (const auto &topic_pair : sub.topics)
        {
            const std::string &pattern = topic_pair.first;
            bool sf_enabled = topic_pair.second;
            if (topicMatches(topic_str, pattern))
            {
                if (sub.connected)
                {
                    ssize_t sent = send_all(sub.socket, serialized_packet.data(), serialized_packet.size(), MSG_NOSIGNAL);
                    if (sent < 0 || (size_t)sent != serialized_packet.size())
                    {
                        if (errno != EPIPE && errno != ECONNRESET)
                        {
                            perror("WARN: send_all to subscriber failed");
                        }
                    }
                }
                else if (sf_enabled)
                {
                    sub.stored_messages.push_back(serialized_packet);
                }
                break;
            }
        }
    }
}