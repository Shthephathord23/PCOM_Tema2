#include "circular_buffer.h"
#include "common.h"
#include <cstdio>
#include <cstdlib>
#include <arpa/inet.h>
#include <iomanip>
#include <cmath>
#include <vector>
#include <limits>
#include <sstream>
#include <stdexcept>

static bool parse_arguments(int argc, char *argv[], std::string &client_id, std::string &server_ip, int &server_port);
static int setup_and_connect(const std::string &server_ip, int server_port);
static bool send_client_id(int client_socket, const std::string &client_id);
static void initialize_poll_fds(std::vector<struct pollfd> &poll_fds, int client_socket);
static void handle_user_input(int client_socket, bool &running);
static ssize_t receive_server_data(int client_socket, CircularBuffer<char> &server_buffer);
static std::string format_received_message(const std::string &sender_ip, uint16_t sender_port,
                                           const std::string &topic, uint8_t udp_type,
                                           const char *content_data, uint16_t content_len);
static void deserialize_and_process_message(CircularBuffer<char> &server_buffer);
static void handle_server_message(int client_socket, CircularBuffer<char> &server_buffer, bool &running);
static void subscriber_loop(int client_socket, std::vector<struct pollfd> &poll_fds);

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    std::string client_id;
    std::string server_ip;
    int server_port;

    if (!parse_arguments(argc, argv, client_id, server_ip, server_port))
    {
        return 1;
    }

    int client_socket = setup_and_connect(server_ip, server_port);

    if (!send_client_id(client_socket, client_id))
    {
        close(client_socket);
        return 1;
    }

    std::vector<struct pollfd> poll_fds(2);

    initialize_poll_fds(poll_fds, client_socket);
    subscriber_loop(client_socket, poll_fds);
    close(client_socket);

    return 0;
}

static bool parse_arguments(int argc, char *argv[], std::string &client_id, std::string &server_ip, int &server_port)
{
    if (argc != 4)
    {
        std::cerr << "Usage: " << argv[0] << " <ID_CLIENT> <IP_SERVER> <PORT_SERVER>" << std::endl;
        return false;
    }
    
    client_id = argv[1];
    
    if (client_id.length() > MAX_ID_SIZE)
    {
        std::cerr << "ERROR: Client ID too long (max " << MAX_ID_SIZE << " characters)." << std::endl;
        return false;
    }
    
    server_ip = argv[2];
    server_port = atoi(argv[3]);
    
    if (server_port <= 0 || server_port > 65535)
    {
        std::cerr << "ERROR: Invalid server port." << std::endl;
        return false;
    }
    
    return true;
}
static int setup_and_connect(const std::string &server_ip, int server_port)
{
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    
    if (client_socket < 0)
    {
        error("ERROR opening socket");
    }
    
    int flag = 1;
    
    if (setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) < 0)
    {
        perror("WARN: setsockopt TCP_NODELAY failed");
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    
    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0)
    {
        close(client_socket);
        error("ERROR invalid server IP address");
    }
    
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        close(client_socket);
        error("ERROR connecting to server");
    }
    
    return client_socket;
}
static bool send_client_id(int client_socket, const std::string &client_id)
{
    if (send_all(client_socket, client_id.c_str(), client_id.length() + 1, 0) < 0)
    {
        std::cerr << "ERROR sending client ID failed." << std::endl;
        return false;
    }
    
    return true;
}
static void initialize_poll_fds(std::vector<struct pollfd> &poll_fds, int client_socket)
{
    poll_fds[0].fd = STDIN_FILENO;
    poll_fds[0].events = POLLIN;
    poll_fds[0].revents = 0;
    poll_fds[1].fd = client_socket;
    poll_fds[1].events = POLLIN;
    poll_fds[1].revents = 0;
}

static void subscriber_loop(int client_socket, std::vector<struct pollfd> &poll_fds)
{
    CircularBuffer<char> server_buffer(CIRCULAR_BUFFER_SIZE);
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

        if (poll_fds[0].revents & POLLIN)
        {
            handle_user_input(client_socket, running);
        }

        bool disconnected = false;
        if (poll_fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            disconnected = true;
        }

        if (!disconnected && (poll_fds[1].revents & POLLIN))
        {
            handle_server_message(client_socket, server_buffer, running);
        }

        else if (disconnected)
        {
            handle_server_message(client_socket, server_buffer, running);
            if (running)
            {
                std::cerr << "ERROR: Server connection error/hangup." << std::endl;
                running = false;
            }
        }

        for (auto &pfd : poll_fds)
        {
            pfd.revents = 0;
        }
    }
}


static void handle_user_input(int client_socket, bool &running)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    if (fgets(buffer, BUFFER_SIZE - 1, stdin) == NULL)
    {
        if (feof(stdin))
        {
            running = false;
        }
        else
        {
            perror("ERROR reading from stdin");
            running = false;
        }
        return;
    }

    buffer[strcspn(buffer, "\n")] = 0;
    std::string input_line(buffer);

    if (input_line.empty())
    {
        return;
    }

    std::stringstream ss(input_line);
    std::string command_verb;
    ss >> command_verb;

    if (command_verb == "exit")
    {
        running = false;
    }

    else if (command_verb == "subscribe")
    {
        std::string topic;
        int sf_val = 0;
        
        if (ss >> topic && ss.peek() == EOF)
        {
            if (topic.length() > TOPIC_SIZE)
            {
                std::cerr << "ERROR: Topic too long (max " << TOPIC_SIZE << " characters)." << std::endl;
            }
            else
            {
                std::string cmd = "subscribe " + topic + " " + std::to_string(sf_val) + "\n";
                if (send_all(client_socket, cmd.c_str(), cmd.size(), 0) < 0)
                {
                    running = false;
                }
                else
                {
                    std::cout << "Subscribed to topic." << std::endl;
                }
            }
        }

        else
        {
            std::cerr << "Usage: subscribe <topic>" << std::endl;

            if (!ss.eof())
            {
                ss.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            }
        }
    }

    else if (command_verb == "unsubscribe")
    {
        std::string topic;
        if (ss >> topic && ss.eof())
        {
            if (topic.length() > TOPIC_SIZE)
            {
                std::cerr << "ERROR: Topic too long (max " << TOPIC_SIZE << " characters)." << std::endl;
            }
            else
            {
                std::string cmd = "unsubscribe " + topic + "\n";
                if (send_all(client_socket, cmd.c_str(), cmd.size(), 0) < 0)
                {
                    running = false;
                }
                else
                {
                    std::cout << "Unsubscribed from topic." << std::endl;
                }
            }
        }
        else
        {
            std::cerr << "Usage: unsubscribe <topic>" << std::endl;
        }
    }

    else
    {
        std::cerr << "Unknown command. Available: subscribe, unsubscribe, exit." << std::endl;
    }
}

static ssize_t receive_server_data(int client_socket, CircularBuffer<char> &buffer_to_fill)
{
    char recv_tmp_buffer[BUFFER_SIZE];
    memset(recv_tmp_buffer, 0, BUFFER_SIZE);
    ssize_t bytes_received = recv(client_socket, recv_tmp_buffer, BUFFER_SIZE, 0);

    if (bytes_received > 0)
    {
        if (!buffer_to_fill.write(recv_tmp_buffer, bytes_received))
        {
            std::cerr << "ERROR: Subscriber buffer overflow. Server data potentially lost. Disconnecting." << std::endl;

            return -2;
        }
    }

    else if (bytes_received < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return 0;
        }
        if (errno != ECONNRESET && errno != EPIPE && errno != EINTR)
        {
            perror("ERROR receiving from server");
        }
    }

    return bytes_received;
}

static std::string format_received_message(const std::string &sender_ip, uint16_t sender_port,
                                           const std::string &topic, uint8_t udp_type,
                                           const char *content_data, uint16_t content_len)
{
    std::stringstream result_ss;
    result_ss << sender_ip << ":" << sender_port << " - ";
    result_ss << topic << " - ";
    switch (udp_type)
    {
    case 0:
    {
        if (content_len < 5)
        {
            result_ss << "INT - INVALID DATA";
            break;
        }
        uint8_t sign = content_data[0];
        uint32_t net_val;
        memcpy(&net_val, content_data + 1, 4);
        int val = ntohl(net_val);
        if (sign == 1)
        {
            val = -val;
        }
        else if (sign != 0)
        {
            result_ss << "INT - INVALID SIGN";
            break;
        }
        result_ss << "INT - " << val;
        break;
    }
    case 1:
    {
        if (content_len < 2)
        {
            result_ss << "SHORT_REAL - INVALID DATA";
            break;
        }
        uint16_t net_val;
        memcpy(&net_val, content_data, 2);
        float val = ntohs(net_val) / 100.0f;
        if (val == static_cast<int>(val))
        {
            result_ss << "SHORT_REAL - " << static_cast<int>(val);
        }
        else
        {
            result_ss << "SHORT_REAL - " << std::fixed << std::setprecision(2) << val;
            result_ss.unsetf(std::ios_base::floatfield);
        }
        break;
    }
    case 2:
    {
        if (content_len < 6)
        {
            result_ss << "FLOAT - INVALID DATA";
            break;
        }
        uint8_t sign = content_data[0];
        uint32_t net_val;
        memcpy(&net_val, content_data + 1, 4);
        uint8_t power = content_data[5];
        double val = ntohl(net_val);
        double p10 = 1.0;
        for (int p = 0; p < power; ++p)
        {
            p10 /= 10.0;
        }
        val *= p10;
        if (sign == 1)
        {
            val = -val;
        }
        else if (sign != 0)
        {
            result_ss << "FLOAT - INVALID SIGN";
            break;
        }
        std::stringstream ss_float;
        ss_float << std::fixed << std::setprecision(power) << val;
        std::string fs = ss_float.str();
        if (power > 0)
        {
            size_t dp = fs.find('.');
            if (dp != std::string::npos)
            {
                size_t lnz = fs.find_last_not_of('0');
                if (lnz == dp)
                {
                    fs.erase(dp);
                }
                else if (lnz > dp)
                {
                    fs.erase(lnz + 1);
                }
            }
        }
        else
        {
            if (val == static_cast<long long>(val))
            {
                fs = std::to_string(static_cast<long long>(val));
            }
        }
        result_ss << "FLOAT - " << fs;
        break;
    }
    case 3:
    {
        std::string str(content_data, content_len);
        result_ss << "STRING - " << str;
        break;
    }
    default:
        result_ss << "UNKNOWN TYPE (" << (int)udp_type << ")";
    }
    return result_ss.str();
}

static void deserialize_and_process_message(CircularBuffer<char> &data_buffer)
{
    const size_t length_prefix_size = sizeof(uint32_t);
    while (true)
    {
        if (data_buffer.bytes_available() < length_prefix_size)
        {
            break;
        }
        
        uint32_t net_total_msg_len;
        data_buffer.peek(reinterpret_cast<char *>(&net_total_msg_len), 0, length_prefix_size);
        uint32_t total_payload_len = ntohl(net_total_msg_len);

        if (total_payload_len == 0 || total_payload_len > 4 * BUFFER_SIZE)
        {
            std::cerr << "ERROR: Invalid payload length: " << total_payload_len << ". Clearing buffer." << std::endl;
            data_buffer.reset();
            break;
        }

        size_t total_packet_len = length_prefix_size + total_payload_len;

        if (data_buffer.bytes_available() < total_packet_len)
        {
            break;
        }

        std::vector<char> full_packet_data(total_packet_len);
        size_t actual_read = data_buffer.read(full_packet_data.data(), total_packet_len);

        if (actual_read != total_packet_len)
        {
            std::cerr << "ERROR: Failed reading full packet. Expected " << total_packet_len << " got " << actual_read << ". Resetting buffer." << std::endl;
            data_buffer.reset();
            break;
        }

        const char *payload_data_ptr = full_packet_data.data() + length_prefix_size;
        size_t current_offset = 0;
        std::string sender_ip_str = "INVALID_IP";
        uint16_t sender_port = 0;
        std::string topic;
        uint8_t udp_type = 255;
        uint16_t content_len = 0;
        const char *content_data_ptr = nullptr;

        try
        {
            if (current_offset + sizeof(uint32_t) > total_payload_len)
            {
                throw std::runtime_error("Payload too small for IP");
            }
            uint32_t net_ip;
            memcpy(&net_ip, payload_data_ptr + current_offset, sizeof(uint32_t));
            current_offset += sizeof(uint32_t);
            struct in_addr ip_addr;
            ip_addr.s_addr = net_ip;
            char ip_buffer[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &ip_addr, ip_buffer, INET_ADDRSTRLEN))
            {
                sender_ip_str = ip_buffer;
            }

            if (current_offset + sizeof(uint16_t) > total_payload_len)
            {
                throw std::runtime_error("Payload too small for Port");
            }
            uint16_t net_port;
            memcpy(&net_port, payload_data_ptr + current_offset, sizeof(uint16_t));
            sender_port = ntohs(net_port);
            current_offset += sizeof(uint16_t);

            if (current_offset + sizeof(uint8_t) > total_payload_len)
            {
                throw std::runtime_error("Payload too small for Topic Len");
            }
            uint8_t topic_len;
            memcpy(&topic_len, payload_data_ptr + current_offset, sizeof(uint8_t));
            current_offset += sizeof(uint8_t);
            if (topic_len > total_payload_len - current_offset)
            {
                throw std::runtime_error("Topic length exceeds remaining payload");
            }
            topic.assign(payload_data_ptr + current_offset, topic_len);
            current_offset += topic_len;

            if (current_offset + sizeof(uint8_t) > total_payload_len)
            {
                throw std::runtime_error("Payload too small for UDP Type");
            }
            memcpy(&udp_type, payload_data_ptr + current_offset, sizeof(uint8_t));
            current_offset += sizeof(uint8_t);

            if (current_offset + sizeof(uint16_t) > total_payload_len)
            {
                throw std::runtime_error("Payload too small for Content Len");
            }
            uint16_t net_content_len;
            memcpy(&net_content_len, payload_data_ptr + current_offset, sizeof(uint16_t));
            content_len = ntohs(net_content_len);
            current_offset += sizeof(uint16_t);
            if (content_len > total_payload_len - current_offset)
            {
                throw std::runtime_error("Content length exceeds remaining payload");
            }
            content_data_ptr = payload_data_ptr + current_offset;

            std::string formatted_output = format_received_message(sender_ip_str, sender_port, topic, udp_type, content_data_ptr, content_len);
            std::cout << formatted_output << std::endl;
        }
        catch (const std::runtime_error &e)
        {
            std::cerr << "ERROR: Deserialization failed - " << e.what() << ". Skipping packet." << std::endl;
        }
    }
}

static void handle_server_message(int client_socket, CircularBuffer<char> &server_data_buffer, bool &running)
{
    ssize_t bytes_received = receive_server_data(client_socket, server_data_buffer);
    if (bytes_received < 0)
    {
        running = false;
        return;
    }
    if (bytes_received == 0 && server_data_buffer.empty())
    {
        running = false;
        return;
    }
    deserialize_and_process_message(server_data_buffer);
}
