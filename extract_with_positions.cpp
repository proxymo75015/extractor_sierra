// Programme simple pour extraire et afficher les positions audio
#include <fstream>
#include <iostream>
#include <cstdint>
#include <cstring>
#include <vector>

#pragma pack(push, 1)
struct FrameHeader {
    uint32_t size;
    uint16_t type;
    uint16_t unknown;
};

struct ChunkHeader {
    uint16_t type;
    uint32_t size;
    uint16_t unknown;
};
#pragma pack(pop)

int main() {
    std::ifstream file("ScummVM/rbt/91.RBT", std::ios::binary);
    if (!file) {
        std::cerr << "Erreur ouverture fichier\n";
        return 1;
    }
    
    // Skip SOL header (probablement 22 bytes basé sur offset 45056)
    file.seekg(45056);
    
    std::cout << "Frame | AudioPos  | Size | Canal\n";
    std::cout << "------|-----------|------|-------\n";
    
    int evenCount = 0, oddCount = 0;
    
    for (int frameNum = 0; frameNum < 90; frameNum++) {
        FrameHeader fh;
        file.read(reinterpret_cast<char*>(&fh), 8);
        
        if (!file) break;
        
        int64_t frameEnd = file.tellg() + (int64_t)fh.size - 8;
        
        // Chercher le chunk audio (type 2)
        while (file.tellg() < frameEnd) {
            ChunkHeader ch;
            file.read(reinterpret_cast<char*>(&ch), 8);
            
            if (ch.type == 2) {  // Audio chunk
                int32_t audioPos;
                uint16_t audioSize;
                file.read(reinterpret_cast<char*>(&audioPos), 4);
                file.read(reinterpret_cast<char*>(&audioSize), 2);
                
                int8_t bufferIndex = (audioPos % 4) ? 1 : 0;
                const char* channel = bufferIndex ? "ODD (R)" : "EVEN (L)";
                
                if (bufferIndex) oddCount++;
                else evenCount++;
                
                std::cout << frameNum << " | " << audioPos << " | " 
                         << audioSize << " | " << channel << "\n";
                
                // Skip rest of audio data
                file.seekg(ch.size - 6, std::ios::cur);
                break;
            } else {
                file.seekg(ch.size, std::ios::cur);
            }
        }
        
        file.seekg(frameEnd);
    }
    
    std::cout << "\nTotal EVEN (LEFT):  " << evenCount << " packets\n";
    std::cout << "Total ODD (RIGHT):  " << oddCount << " packets\n";
    
    return 0;
}
