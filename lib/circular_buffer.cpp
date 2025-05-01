#include "circular_buffer.h"
#include <cstring>   // For memcpy
#include <algorithm> // For std::min
#include <stdexcept> // For invalid_argument
#include <vector>    // Include vector for substr implementation

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
        return true; // Nothing to write
    }
    if (len > space_available())
    {
        return false; // Not enough space
    }

    count += len; // Update count first

    // Write in possibly two parts (due to wrap-around)
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

    size_t read_len = std::min(len, count); // Can only read what's available
    if (read_len == 0)
    {
        return 0; // Nothing available
    }

    // Read in possibly two parts
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

// Returns the offset from the current tail, or -1 if not found
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
            return static_cast<ssize_t>(i); // Found at offset i from tail
        }
        current_pos = (current_pos + 1) % capacity;
    }

    return -1; // Not found
}

// Peeks 'len' bytes starting 'offset' bytes from the tail, returns bytes peeked
template <typename T>
size_t CircularBuffer<T>::peek(char *data, size_t offset, size_t len)
{
    if (len == 0 || offset >= count)
    {
        return 0;
    }

    size_t peek_len = std::min(len, count - offset); // Adjust len to what's actually available after offset
    if (peek_len == 0)
    {
        return 0;
    }

    size_t start_pos = (tail + offset) % capacity;

    // Peek in possibly two parts
    size_t part1_len = std::min(peek_len, capacity - start_pos);
    memcpy(data, buffer.data() + start_pos, part1_len);

    if (part1_len < peek_len)
    {
        size_t part2_len = peek_len - part1_len;
        memcpy(data + part1_len, buffer.data(), part2_len); // Start from beginning of buffer data
    }

    return peek_len;
}

// Helper to peek into a vector
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
    peek(result.data(), offset, peek_len); // Use the existing peek function
    return result;
}

// Creates a std::string from a portion of the buffer without consuming data
template <typename T>
std::string CircularBuffer<T>::substr(size_t offset, size_t len)
{
    if (offset >= count || len == 0)
    {
        return ""; // Invalid offset or zero length requested
    }

    // Adjust length to not exceed available bytes after offset
    size_t actual_len = std::min(len, count - offset);
    if (actual_len == 0)
    {
        return "";
    }

    std::vector<T> temp_buffer = peek_bytes(offset, actual_len);
    // Construct string directly from vector iterators for efficiency
    return std::string(temp_buffer.begin(), temp_buffer.end());
}

template <typename T>
void CircularBuffer<T>::consume(size_t len)
{
    size_t consume_len = std::min(len, count); // Cannot consume more than available
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
    // buffer content doesn't need clearing, it will be overwritten
}

template class CircularBuffer<char>;
