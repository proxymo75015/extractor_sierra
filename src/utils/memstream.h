#pragma once
#include "stream.h"
#include "types.h"
#include <cstring>

namespace Common {

class MemoryReadStream : public SeekableReadStream {
private:
    const byte *_ptr;
    uint32 _size;
    uint32 _pos;
    bool _eos;
public:
    MemoryReadStream(const byte *dataPtr, uint32 dataSize) : _ptr(dataPtr), _size(dataSize), _pos(0), _eos(false) {}
    uint32 read(void *dataPtr, uint32 dataSize) override {
        if (_pos + dataSize > _size) {
            dataSize = (_pos < _size) ? (_size - _pos) : 0;
            _eos = true;
        }
        if (dataSize > 0) memcpy(dataPtr, _ptr + _pos, dataSize);
        _pos += dataSize;
        return dataSize;
    }
    bool eos() const { return _eos; }
    void clearErr() { _eos = false; }
    int64 pos() const override { return _pos; }
    int64 size() const override { return _size; }
    bool seek(int64 offs, int whence = SEEK_SET) override {
        switch (whence) {
            case SEEK_SET: _pos = (uint32)offs; break;
            case SEEK_CUR: _pos = (uint32)(_pos + offs); break;
            case SEEK_END: _pos = (uint32)(_size + offs); break;
            default: return false;
        }
        if (_pos > _size) { _pos = _size; _eos = true; }
        return true;
    }
};

class MemoryReadStreamEndian : public MemoryReadStream, public SeekableReadStreamEndian {
public:
    MemoryReadStreamEndian(const byte *buf, uint32 len, bool bigEndian) : MemoryReadStream(buf, len), SeekableReadStreamEndian(bigEndian) {}
    // Int16 read helper intentionally omitted; not required for current decoder.
};

} // namespace Common
