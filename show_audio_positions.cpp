#include <cstdio>
#include <cstdint>
#include <vector>
#include <fstream>

int main() {
    // Lire le fichier de diagnostic audio
    std::ifstream f("rbt_audio_diagnostic.txt");
    if (!f.is_open()) {
        fprintf(stderr, "Erreur: fichier diagnostic non trouvé\n");
        return 1;
    }
    
    int frame, audioPos, audioSize;
    int evenCount = 0, oddCount = 0;
    
    printf("Frame | AudioPos  | Size | Canal\n");
    printf("------|-----------|------|-------\n");
    
    while (f >> frame >> audioPos >> audioSize) {
        int8_t bufferIndex = (audioPos % 4) ? 1 : 0;
        const char* channel = bufferIndex ? "ODD (R)" : "EVEN (L)";
        
        if (bufferIndex) oddCount++;
        else evenCount++;
        
        if (frame < 10 || frame % 10 == 0) {
            printf("%5d | %9d | %4d | %s\n", frame, audioPos, audioSize, channel);
        }
    }
    
    printf("\n");
    printf("Total EVEN (LEFT):  %d packets\n", evenCount);
    printf("Total ODD (RIGHT):  %d packets\n", oddCount);
    
    return 0;
}
