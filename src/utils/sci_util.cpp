#include "sci_util.h"
#include <cstdint>

namespace {
    static bool g_useBig = false;
    static bool g_platformMac = false;
}

namespace SciHelpers {

void setUseBigEndian(bool b) { g_useBig = b; }
bool getUseBigEndian() { return g_useBig; }

void setPlatformMacintosh(bool b) { g_platformMac = b; }
bool isPlatformMacintosh() { return g_platformMac; }

uint16_t READ_SCI11ENDIAN_UINT16(const void *ptr) {
    const uint8_t *p = (const uint8_t*)ptr;
    // ScummVM behaviour: fields in SCI1.1+ are big-endian on Macintosh builds,
    // otherwise little-endian. We mimic that using the platform flag.
    if (g_platformMac) return (uint16_t)p[0] << 8 | p[1];
    return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

uint32_t READ_SCI11ENDIAN_UINT32(const void *ptr) {
    const uint8_t *p = (const uint8_t*)ptr;
    if (g_platformMac) return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

} // namespace
