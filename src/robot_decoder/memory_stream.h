#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

class MemoryReadStream {
public:
    MemoryReadStream(const uint8_t *data, size_t size) : _data(data), _size(size), _pos(0), _err(false) {}
    bool read(void *dst, size_t n) {
        if (_pos + n > _size) { _err = true; return false; }
        std::memcpy(dst, _data + _pos, n);
        _pos += n;
        return true;
    }
    bool seek(size_t off) {
        if (off > _size) { _err = true; return false; }
        _pos = off; return true;
    }
    size_t pos() const { return _pos; }
    const uint8_t *data() const { return _data; }
    size_t size() const { return _size; }
    bool err() const { return _err; }
private:
    const uint8_t *_data;
    size_t _size;
    size_t _pos;
    bool _err;
};
