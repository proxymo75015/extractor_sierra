// Décompresseur et extracteur de scripts SCI32 depuis RESSCI
// Utilise l'implémentation LZS de ScummVM (formats/lzs.cpp)

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include "formats/lzs.h"

struct ResourceHeader {
    uint8_t type;
    uint16_t number;
    uint32_t compressedSize;
    uint32_t decompressedSize;
    uint16_t compressionMethod;
} __attribute__((packed));

int main(int argc, char* argv[]) {
    std::cout << "🔓 LZS Decompressor - Extraction des scripts SCI32\n";
    std::cout << "===================================================\n\n";
    
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <RESSCI.00X> <script_number>\n\n";
        std::cout << "Exemples:\n";
        std::cout << "  " << argv[0] << " Resource/RESSCI.001 902\n";
        std::cout << "  " << argv[0] << " Resource/RESSCI.001 13400\n";
        return 1;
    }
    
    std::string ressciPath = argv[1];
    int targetScriptNum = std::atoi(argv[2]);
    
    // Charger RESSCI
    std::ifstream file(ressciPath, std::ios::binary);
    if (!file) {
        std::cerr << "❌ Impossible d'ouvrir " << ressciPath << std::endl;
        return 1;
    }
    
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> ressciData(fileSize);
    file.read(reinterpret_cast<char*>(ressciData.data()), fileSize);
    file.close();
    
    std::cout << "✅ RESSCI chargé: " << fileSize << " bytes\n\n";
    
    // Scanner pour trouver le script
    size_t offset = 0;
    bool found = false;
    
    while (offset < fileSize - sizeof(ResourceHeader)) {
        ResourceHeader header;
        std::memcpy(&header, ressciData.data() + offset, sizeof(header));
        
        // Vérifier si c'est un script valide
        if (header.type == 0x02 && 
            header.compressedSize > 0 && header.compressedSize < 1000000 &&
            header.decompressedSize > 0 && header.decompressedSize < 1000000) {
            
            if (header.number == targetScriptNum) {
                std::cout << "✅ Script #" << header.number << " trouvé à offset 0x" 
                          << std::hex << offset << std::dec << "\n";
                std::cout << "   Compressed: " << header.compressedSize << " bytes\n";
                std::cout << "   Decompressed: " << header.decompressedSize << " bytes\n";
                std::cout << "   Method: " << header.compressionMethod;
                
                std::vector<uint8_t> decompressed;
                bool success = false;
                
                if (header.compressionMethod == 0) {
                    std::cout << " (Uncompressed)\n\n";
                    
                    // Copie directe
                    const uint8_t* rawData = ressciData.data() + offset + sizeof(ResourceHeader);
                    decompressed.assign(rawData, rawData + header.compressedSize);
                    success = true;
                    
                } else if (header.compressionMethod == 32) {
                    std::cout << " (STACpack/LZS)\n\n";
                    
                    // Décompresser avec l'implémentation LZS existante
                    const uint8_t* compressedData = ressciData.data() + offset + sizeof(ResourceHeader);
                    decompressed.resize(header.decompressedSize);
                    
                    std::cout << "🔄 Décompression LZS...\n";
                    
                    int result = LZSDecompress(compressedData, header.compressedSize,
                                              decompressed.data(), header.decompressedSize);
                    
                    if (result == 0) {
                        success = true;
                    } else {
                        std::cerr << "❌ Échec de décompression\n";
                    }
                } else {
                    std::cout << "\n⚠️  Méthode de compression " << header.compressionMethod << " non supportée\n";
                    std::cout << "     Seul STACpack/LZS (32) et Uncompressed (0) sont implémentés\n";
                }
                
                if (success) {
                    std::cout << "✅ Extraction réussie: " << decompressed.size() << " bytes\n\n";
                    
                    // Sauvegarder
                    std::string outPath = "script_" + std::to_string(header.number) + "_decompressed.bin";
                    std::ofstream out(outPath, std::ios::binary);
                    out.write(reinterpret_cast<const char*>(decompressed.data()), decompressed.size());
                    out.close();
                    
                    std::cout << "💾 Sauvegardé dans: " << outPath << "\n\n";
                    
                    // Afficher le début en hexdump
                    std::cout << "📄 Hexdump (premiers 128 bytes):\n";
                    for (size_t i = 0; i < std::min<size_t>(128, decompressed.size()); i++) {
                        if (i % 16 == 0) printf("%04zx: ", i);
                        printf("%02x ", decompressed[i]);
                        if (i % 16 == 15) printf("\n");
                    }
                    std::cout << "\n";
                    
                    // Chercher les opcodes Robot (CALLK 0x23 pour Phantasmagoria/SCI2.1)
                    // Dans SCI32, CALLK est 0x46, suivi du numéro de kernel
                    std::cout << "🔍 Recherche des appels Robot() dans le bytecode...\n";
                    std::cout << "   Pattern: CALLK (0x46) + Robot (0x23)\n\n";
                    int robotCalls = 0;
                    
                    for (size_t i = 0; i < decompressed.size() - 2; i++) {
                        if (decompressed[i] == 0x46 && decompressed[i+1] == 0x23) {
                            std::cout << "   ✅ CALLK Robot trouvé à offset 0x" << std::hex << i << std::dec << "\n";
                            
                            // Afficher contexte (40 bytes avant et après)
                            printf("      Contexte (40 bytes avant/après):\n      ");
                            for (int j = -40; j <= 40 && i+j < decompressed.size(); j++) {
                                int pos = static_cast<int>(i) + j;
                                if (pos >= 0 && pos < static_cast<int>(decompressed.size())) {
                                    if (j == 0) printf("\n   -> ");
                                    printf(" %02x", decompressed[pos]);
                                    if (j == 1) printf(" <-\n      ");
                                }
                            }
                            printf("\n\n");
                            robotCalls++;
                        }
                    }
                    
                    if (robotCalls == 0) {
                        std::cout << "   ❌ Aucun appel Robot() trouvé\n";
                    } else {
                        std::cout << "✅ Total: " << robotCalls << " appel(s) Robot() trouvé(s)\n";
                    }
                }
                
                found = true;
                break;
            }
            
            // Passer au prochain script
            offset += sizeof(ResourceHeader) + header.compressedSize;
        } else {
            offset++;
        }
    }
    
    if (!found) {
        std::cerr << "❌ Script #" << targetScriptNum << " non trouvé\n";
        return 1;
    }
    
    return 0;
}
