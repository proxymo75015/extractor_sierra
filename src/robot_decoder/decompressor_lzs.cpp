#include "decompressor_lzs.h"
#include <vector>
#include <cstdlib>
#include "lzs.h"

int DecompressorLZS::unpack(Common::MemoryReadStream *stream, uint8_t *dst, uint32_t compSize, uint32_t decompSize) {
    if (!stream) return -1;
    std::vector<uint8_t> comp(compSize);
    if (stream->read(comp.data(), compSize) != compSize) return -1;
    int rc = LZSDecompress(comp.data(), compSize, dst, decompSize);
    return rc;
}
