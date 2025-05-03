#include "circular_buffer.h"
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <vector>

template <typename T>
CircularBuffer<T>::CircularBuffer(size_t cap)
    : buffer(cap), head(0), tail(0), count(0), capacity(cap)
{
    if (cap == 0)
    {
        throw std::invalid_argument("CircularBuffer capacity cannot be zero.");
    }
}

template <typename T>
bool CircularBuffer<T>::write(const char *data, size_t len)
{
    if (len == 0)
    {
        return true;
    }
    if (len > space_available())
    {
        return false;
    }

    count += len;

    size_t part1_len = std::min(len, capacity - head);
    memcpy(buffer.data() + head, data, part1_len);
    head = (head + part1_len) % capacity;

    if (part1_len < len)
    {
        size_t part2_len = len - part1_len;
        memcpy(buffer.data() + head, data + part1_len, part2_len);
        head = (head + part2_len) % capacity;
    }

    return true;
}

template <typename T>
size_t CircularBuffer<T>::read(char *data, size_t len)
{
    if (len == 0)
    {
        return 0;
    }

    size_t read_len = std::min(len, count);
    if (read_len == 0)
    {
        return 0;
    }

    size_t part1_len = std::min(read_len, capacity - tail);
    memcpy(data, buffer.data() + tail, part1_len);
    tail = (tail + part1_len) % capacity;
    count -= part1_len;

    if (part1_len < read_len)
    {
        size_t part2_len = read_len - part1_len;
        memcpy(data + part1_len, buffer.data() + tail, part2_len);
        tail = (tail + part2_len) % capacity;
        count -= part2_len;
    }

    return read_len;
}

template <typename T>
ssize_t CircularBuffer<T>::find(char delimiter)
{
    if (count == 0)
    {
        return -1;
    }

    size_t current_pos = tail;
    for (size_t i = 0; i < count; ++i)
    {
        if (buffer[current_pos] == delimiter)
        {
            return static_cast<ssize_t>(i);
        }
        current_pos = (current_pos + 1) % capacity;
    }

    return -1;
}

template <typename T>
size_t CircularBuffer<T>::peek(char *data, size_t offset, size_t len)
{
    if (len == 0 || offset >= count)
    {
        return 0;
    }

    size_t peek_len = std::min(len, count - offset);
    if (peek_len == 0)
    {
        return 0;
    }

    size_t start_pos = (tail + offset) % capacity;

    size_t part1_len = std::min(peek_len, capacity - start_pos);
    memcpy(data, buffer.data() + start_pos, part1_len);

    if (part1_len < peek_len)
    {
        size_t part2_len = peek_len - part1_len;
        memcpy(data + part1_len, buffer.data(), part2_len);
    }

    return peek_len;
}

template <typename T>
std::vector<T> CircularBuffer<T>::peek_bytes(size_t offset, size_t len)
{
    std::vector<T> result;
    if (len == 0 || offset >= count)
    {
        return result;
    }

    size_t peek_len = std::min(len, count - offset);
    if (peek_len == 0)
    {
        return result;
    }

    result.resize(peek_len);
    peek(result.data(), offset, peek_len);
    return result;
}

template <typename T>
std::string CircularBuffer<T>::substr(size_t offset, size_t len)
{
    if (offset >= count || len == 0)
    {
        return "";
    }

    size_t actual_len = std::min(len, count - offset);
    if (actual_len == 0)
    {
        return "";
    }

    std::vector<T> temp_buffer = peek_bytes(offset, actual_len);

    return std::string(temp_buffer.begin(), temp_buffer.end());
}

template <typename T>
void CircularBuffer<T>::consume(size_t len)
{
    size_t consume_len = std::min(len, count);
    if (consume_len == 0)
    {
        return;
    }

    tail = (tail + consume_len) % capacity;
    count -= consume_len;
}

template <typename T>
size_t CircularBuffer<T>::bytes_available() const
{
    return count;
}

template <typename T>
size_t CircularBuffer<T>::space_available() const
{
    return capacity - count;
}

template <typename T>
bool CircularBuffer<T>::empty() const
{
    return count == 0;
}

template <typename T>
bool CircularBuffer<T>::full() const
{
    return count == capacity;
}

template <typename T>
void CircularBuffer<T>::reset()
{
    head = 0;
    tail = 0;
    count = 0;
}

template class CircularBuffer<char>;
