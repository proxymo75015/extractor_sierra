#include <cstdio>
#include <cstdint>
#include <vector>

// Fonction simple pour lire les positions audio absolues d'un fichier RBT
int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <rbt_file>\n", argv[0]);
        return 1;
    }
    
    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        printf("Cannot open %s\n", argv[1]);
        return 1;
    }
    
    // Lire l'en-tête (simplifiié - on suppose version 5)
    fseek(f, 2, SEEK_SET); // Skip version
    
    // Lire frames count
    uint16_t numFrames;
    fread(&numFrames, 2, 1, f);
    
    // Lire audioBlockSize
    uint16_t audioBlockSize;
    fread(&audioBlockSize, 2, 1, f);
    
    // Lire hasAudio
    fseek(f, 12, SEEK_SET);
    uint16_t hasAudio;
    fread(&hasAudio, 2, 1, f);
    
    if (!hasAudio) {
        printf("No audio in this file\n");
        fclose(f);
        return 0;
    }
    
    printf("Frames: %u, AudioBlockSize: %u, HasAudio: %u\n", numFrames, audioBlockSize, hasAudio);
    
    // Lire les primers
    fseek(f, 2046, SEEK_SET);
    uint32_t evenPrimerSize, oddPrimerSize;
    fread(&evenPrimerSize, 4, 1, f);
    fread(&oddPrimerSize, 4, 1, f);
    
    printf("Primers: EVEN=%u ODD=%u Total=%u samples\n", evenPrimerSize, oddPrimerSize, evenPrimerSize + oddPrimerSize);
    
    // Calculer l'offset où commencent les records
    uint32_t recordOffset = 2048 + 1200 + 14 + evenPrimerSize + oddPrimerSize;
    
    // Lire videoSizes et packetSizes
    std::vector<uint32_t> videoSizes(numFrames);
    std::vector<uint32_t> packetSizes(numFrames);
    std::vector<uint32_t> recordPositions(numFrames);
    
    fseek(f, recordOffset, SEEK_SET);
    fread(videoSizes.data(), 4, numFrames, f);
    fread(packetSizes.data(), 4, numFrames, f);
    
    // Calculer les positions des records
    uint32_t currentPos = recordOffset + 8 * numFrames;
    for (int i = 0; i < numFrames; i++) {
        recordPositions[i] = currentPos;
        currentPos += packetSizes[i];
    }
    
    printf("\n=== First 10 Frames Audio Positions ===\n");
    printf("Frame | VideoSize | PacketSize | RecordPos | AudioPos | AbsolutePos | BlockSize | Channel\n");
    printf("------|-----------|------------|-----------|----------|-------------|-----------|--------\n");
    
    for (int i = 0; i < (numFrames < 10 ? numFrames : 10); i++) {
        if (packetSizes[i] == 0 || videoSizes[i] == 0) continue;
        
        // Position de l'en-tête audio
        uint32_t audioHeaderPos = recordPositions[i] + videoSizes[i];
        fseek(f, audioHeaderPos, SEEK_SET);
        
        int32_t audioAbsolutePosition;
        int32_t audioDataSize;
        fread(&audioAbsolutePosition, 4, 1, f);
        fread(&audioDataSize, 4, 1, f);
        
        const char* channel = (audioAbsolutePosition % 2 == 0) ? "EVEN" : "ODD";
        
        printf("%5d | %9u | %10u | %9u | %8u | %11d | %9d | %s\n",
               i, videoSizes[i], packetSizes[i], recordPositions[i], audioHeaderPos,
               audioAbsolutePosition, audioDataSize, channel);
    }
    
    fclose(f);
    return 0;
}
