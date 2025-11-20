#pragma once
#include <cstdint>
#include <cstddef>
#include "utils/memstream.h"

class DecompressorLZS {
public:
    // Unpack reads compressed data from stream (exactly compSize bytes) and writes decompSize bytes to dst.
    // Returns 0 on success, non-zero on failure.
    int unpack(Common::MemoryReadStream *stream, uint8_t *dst, uint32_t compSize, uint32_t decompSize);
};
