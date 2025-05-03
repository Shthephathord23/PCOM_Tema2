#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <vector>
#include <string>
#include <sys/types.h>
#include <vector>

template <typename T>
class CircularBuffer
{
private:
    std::vector<T> buffer;
    size_t head;
    size_t tail;
    size_t count;
    size_t capacity;

public:
    explicit CircularBuffer(size_t cap);

    CircularBuffer(const CircularBuffer &) = delete;
    CircularBuffer &operator=(const CircularBuffer &) = delete;

    bool write(const char *data, size_t len);
    size_t read(char *data, size_t len);
    ssize_t find(char delimiter);
    size_t peek(char *data, size_t offset, size_t len);
    std::vector<T> peek_bytes(size_t offset, size_t len);
    std::string substr(size_t offset, size_t len);
    void consume(size_t len);

    size_t bytes_available() const;
    size_t space_available() const;
    bool empty() const;
    bool full() const;
    void reset();
    void clear() { reset(); }
};

#endif // CIRCULAR_BUFFER_H