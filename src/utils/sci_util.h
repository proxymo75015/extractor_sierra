#ifndef ROBOT_SCI_UTIL_H
#define ROBOT_SCI_UTIL_H

#include <cstdint>

namespace SciHelpers {
    void setUseBigEndian(bool b);
    bool getUseBigEndian();

    void setPlatformMacintosh(bool b);
    bool isPlatformMacintosh();

    uint16_t READ_SCI11ENDIAN_UINT16(const void *ptr);
    uint32_t READ_SCI11ENDIAN_UINT32(const void *ptr);
}

#endif // ROBOT_SCI_UTIL_H
