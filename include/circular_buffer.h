#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <vector>
#include <string> // Include for std::string
#include <sys/types.h> // For size_t
#include <vector>  // For peek_string

// Basic circular buffer for character data
template <typename T>
class CircularBuffer {
private:
    std::vector<T> buffer;
    size_t head;       // Index of the next write position
    size_t tail;       // Index of the next read position
    size_t count;      // Number of elements currently in the buffer
    size_t capacity;   // Total capacity of the buffer

public:
    // Constructor
    explicit CircularBuffer(size_t cap);

    // Disable copy and assignment
    CircularBuffer(const CircularBuffer&) = delete;
    CircularBuffer& operator=(const CircularBuffer&) = delete;

    // Basic operations
    bool write(const char* data, size_t len); // Returns false if buffer is full
    size_t read(char* data, size_t len);      // Returns bytes actually read
    ssize_t find(char delimiter);             // Returns offset from tail, or -1 if not found
    size_t peek(char* data, size_t offset, size_t len); // Returns bytes peeked
    std::vector<T> peek_bytes(size_t offset, size_t len); // Helper to peek into a vector
    std::string substr(size_t offset, size_t len); // Get a string representation without consuming
    void consume(size_t len);                 // Removes 'len' bytes from the front (tail)

    // Status
    size_t bytes_available() const;
    size_t space_available() const;
    bool empty() const;
    bool full() const;
    void reset(); // Clears the buffer state
    void clear() { reset(); } // Add alias for clarity if used elsewhere

};

#endif // CIRCULAR_BUFFER_H