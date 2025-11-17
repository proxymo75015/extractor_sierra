#pragma once
#include <cstdint>
int LZSDecompress(const uint8_t *in, uint32_t inSize, uint8_t *out, uint32_t outSize);
