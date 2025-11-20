#pragma once
#include <cstdint>
#include <vector>

void deDPCM16Mono(int16_t *out, const uint8_t *in, uint32_t numBytes, int16_t &sample);

// Helper class for easier DPCM decoding
class DPCMDecoder {
public:
    static void decodeDPCM16Mono(const uint8_t *in, int dataSize, std::vector<int16_t> &samples) {
        samples.resize(dataSize);
        int16_t carry = 0;
        deDPCM16Mono(samples.data(), in, dataSize, carry);
    }
};
