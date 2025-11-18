#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>

int main() {
    FILE* f = fopen("test_output/90.RBT", "rb");
    if (!f) return 1;
    
    // Lire les positions audio depuis le header
    fseek(f, 6, SEEK_SET);
    uint16_t version;
    fread(&version, 2, 1, f);
    
    printf("Positions audio des 20 premières frames:\n");
    printf("Position (bytes) | Position / 2 | %% 4 | Incrément\n");
    
    fclose(f);
    return 0;
}
