#pragma once
#include "types.h"
#include <cstdio>
#include <cstddef>

// Minimal subset of ScummVM stream interfaces required by memstream.
class ReadStream {
public:
    virtual ~ReadStream() {}
    virtual uint32 read(void *dataPtr, uint32 dataSize) = 0;
};

class SeekableReadStream : public ReadStream {
public:
    virtual ~SeekableReadStream() {}
    virtual bool seek(int64 offs, int whence = SEEK_SET) = 0;
    virtual int64 pos() const = 0;
    virtual int64 size() const = 0;
};

class SeekableReadStreamEndian : public SeekableReadStream {
public:
    SeekableReadStreamEndian(bool bigEndian=false) : _big(bigEndian) {}
    bool isBigEndian() const { return _big; }
protected:
    bool _big;
};
