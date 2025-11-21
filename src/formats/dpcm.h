#pragma once
#include <cstdint>
#include <vector>

/**
 * Décompression DPCM16 mono (Sierra SOL/Robot audio format)
 * 
 * Format d'entrée:
 * - Chaque octet encode un delta signé via tableDPCM16
 * - Bit 7: signe (0=+, 1=-)
 * - Bits 0-6: index table (0-127)
 * 
 * @param out      Buffer de sortie (samples 16-bit signés)
 * @param in       Buffer d'entrée (deltas 8-bit)
 * @param numBytes Nombre d'octets à décompresser
 * @param sample   Valeur initiale et état (modifiée par la fonction)
 *                 Typiquement 0 au début d'un nouveau paquet
 * 
 * Important: Pour Robot audio, les 8 premiers samples de chaque paquet
 * sont du "runway" DPCM et doivent être ignorés lors de l'écriture finale.
 * 
 * Références:
 * - DPCM16_DECODER_DOCUMENTATION.md
 * - FORMAT_RBT_DOCUMENTATION.md (section Audio)
 * - SOL_FILE_FORMAT_DOCUMENTATION.md
 */
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
