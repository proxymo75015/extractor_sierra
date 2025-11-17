#pragma once
#include <cstdint>
void deDPCM16Mono(int16_t *out, const uint8_t *in, uint32_t numBytes, int16_t &sample);
