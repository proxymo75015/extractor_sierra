/**
 * SCI Script Analyzer - Extrait les appels Robot() depuis les fichiers RESSCI
 * Analyse les scripts SCI32 pour trouver les coordonnées x,y des vidéos Robot
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <map>
#include <iomanip>
#include <algorithm>
#include "formats/lzs.h"  // Pour décompresser STACpack/LZS

// Structure pour une ressource dans RESMAP
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <map>

// ===== STRUCTURES SCI32 (d'après SCI Companion) =====

// Format RESMAP.00X : débute par une table de pre-entries
#pragma pack(push, 1)
struct RESOURCEMAPPREENTRY_SCI1 {
    uint8_t  bType;      // 0x80-0x91 pour type ressource, 0xff = terminateur
    uint16_t wOffset;    // Offset absolu dans RESMAP vers les entrées de ce type
};

// SCI1.1 (Phantasmagoria) : entrées de 5 octets
struct RESOURCEMAPENTRY_SCI1_1 {
    uint16_t wNumber;     // Numéro de ressource (0 à 65535)
    uint16_t offsetLow;   // 16 bits bas de l'offset
    uint8_t  offsetHigh;  // 8 bits hauts de l'offset
    
    uint32_t GetOffset() const {
        return ((offsetLow | (offsetHigh << 16)) << 1); // Offset * 2 (alignement WORD)
    }
};

// Header de ressource dans RESSCI.00X (SCI2.1 - 13 bytes)
// Référence: ScummVM engines/sci/resource/resource.cpp:readResourceInfo()
struct RESOURCEHEADER_SCI2 {
    uint8_t  iType;           // Type de ressource (1 byte)
    uint16_t wNumber;         // Numéro de ressource (2 bytes)
    uint32_t cbCompressed;    // Taille compressée (4 bytes)
    uint32_t cbDecompressed;  // Taille décompressée (4 bytes)
    uint16_t iMethod;         // Méthode de compression (2 bytes) - 32 = STACpack/LZS
};

#pragma pack(pop)

// Types de ressources SCI
enum ResourceType {
    RT_VIEW = 0x00,
    RT_PIC = 0x01,
    RT_SCRIPT = 0x02,
    RT_TEXT = 0x03,
    RT_SOUND = 0x04,
    RT_MEMORY = 0x05,
    RT_VOCAB = 0x06,
    RT_FONT = 0x07,
    RT_CURSOR = 0x08,
    RT_PATCH = 0x09,
    RT_BITMAP = 0x0A,
    RT_PALETTE = 0x0B,
    RT_CDAUDIO = 0x0C,
    RT_AUDIO = 0x0D,
    RT_SYNC = 0x0E,
    RT_MESSAGE = 0x0F,
    RT_MAP = 0x10,
    RT_HEAP = 0x11
};

struct ResourceEntry {
    uint8_t type;       // Type de ressource (0x02 = Script)
    uint16_t number;    // Numéro de ressource
    uint32_t offset;    // Offset dans RESSCI
    uint32_t compressedSize;
    uint32_t decompressedSize;
    uint16_t compressionMethod;
};

// Opcodes SCI32 pertinents (d'après ScummVM engines/sci/engine/script.h)
const uint8_t OP_PUSHI = 0x38;      // Push immediate 16-bit
const uint8_t OP_PUSH_BYTE = 0x39;  // Push immediate 8-bit
const uint8_t OP_CALLK = 0x42;      // Call kernel function (peut être 0x42 ou 0x43)
const uint8_t OP_CALLK_ALT = 0x43;  // Call kernel function (variante)

// Index du kernel Robot dans Phantasmagoria (SCI2.1 Early)
// D'après ScummVM engines/sci/engine/kernel_tables.h
const uint16_t KERNEL_ROBOT = 0x0023;  // Position 35 (0x23) dans la table kernel

class SCIResourceManager {
private:
    std::map<uint16_t, ResourceEntry> scripts;
    std::map<uint16_t, ResourceEntry> heaps;
    std::vector<uint8_t> ressciData;
    
public:
    bool loadRESMAP(const std::string& resmapPath) {
        std::ifstream file(resmapPath, std::ios::binary);
        if (!file) {
            std::cerr << "Impossible d'ouvrir " << resmapPath << std::endl;
            return false;
        }
        
        // 1. Lire la table de pre-entries (lookup table)
        std::map<uint8_t, uint16_t> typeOffsets;
        
        while (file.good()) {
            RESOURCEMAPPREENTRY_SCI1 preEntry;
            file.read(reinterpret_cast<char*>(&preEntry), sizeof(preEntry));
            
            if (preEntry.bType == 0xFF) {
                break;  // Terminateur
            }
            
            typeOffsets[preEntry.bType] = preEntry.wOffset;
        }
        
        std::cout << "📋 Types de ressources trouvés: " << typeOffsets.size() << std::endl;
        
        // 2. Pour chaque type, lire ses entrées
        for (const auto& [typeCode, offset] : typeOffsets) {
            uint8_t actualType = typeCode & 0x7F;  // Retirer le bit 0x80
            
            if (actualType != RT_SCRIPT && actualType != RT_HEAP) {
                continue;  // On ne lit que les scripts et heaps
            }
            
            // Aller à l'offset de ce type
            file.clear();
            file.seekg(offset, std::ios::beg);
            
            // Calculer le nombre d'entrées
            auto it = typeOffsets.upper_bound(typeCode);
            uint16_t endOffset = (it != typeOffsets.end()) ? it->second : 12000;
            
            uint32_t numEntries = (endOffset - offset) / sizeof(RESOURCEMAPENTRY_SCI1_1);
            if (numEntries > 1000) numEntries = 100;  // Sécurité
            
            std::cout << "  Type 0x" << std::hex << (int)actualType << std::dec 
                      << " (" << (actualType == RT_SCRIPT ? "Script" : "Heap") << "): " 
                      << numEntries << " entrées" << std::endl;
            
            // Lire toutes les entrées de ce type
            for (uint32_t i = 0; i < numEntries && file.good(); i++) {
                RESOURCEMAPENTRY_SCI1_1 mapEntry;
                file.read(reinterpret_cast<char*>(&mapEntry), sizeof(mapEntry));
                
                if (file.gcount() != sizeof(mapEntry)) break;
                
                ResourceEntry res;
                res.type = actualType;
                res.number = mapEntry.wNumber;
                res.offset = mapEntry.GetOffset();
                res.compressedSize = 0;
                res.decompressedSize = 0;
                res.compressionMethod = 0;
                
                if (actualType == RT_SCRIPT) {
                    scripts[res.number] = res;
                } else {
                    heaps[res.number] = res;
                }
            }
        }
        
        std::cout << "✅ Scripts: " << scripts.size() << ", Heaps: " << heaps.size() << std::endl;
        return !scripts.empty();
    }
    
    bool loadRESSCI(const std::string& ressciPath) {
        std::ifstream file(ressciPath, std::ios::binary);
        if (!file) {
            std::cerr << "Impossible d'ouvrir " << ressciPath << std::endl;
            return false;
        }
        
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        ressciData.resize(size);
        file.read(reinterpret_cast<char*>(ressciData.data()), size);
        
        std::cout << "RESSCI chargé: " << size << " octets" << std::endl;
        
        // Lire les headers pour chaque script
        for (auto& [num, entry] : scripts) {
            if (entry.offset >= size) continue;
            
            RESOURCEHEADER_SCI2 header;
            std::memcpy(&header, ressciData.data() + entry.offset, sizeof(header));
            
            entry.compressedSize = header.cbCompressed;
            entry.decompressedSize = header.cbDecompressed;
            entry.compressionMethod = header.iMethod;
        }
        
        return true;
    }
    
    void findRobotCalls(uint16_t robotNumber) {
        std::cout << "\n🔍 Recherche Robot(" << robotNumber << ") dans les scripts décompressés\n";
        std::cout << "========================================================================\n\n";
        
        int scriptCount = 0, skippedCount = 0, decompressErrors = 0;
        bool found = false;
        
        for (const auto& [scriptNum, entry] : scripts) {
            if (entry.offset >= ressciData.size()) {
                skippedCount++;
                continue;
            }
            
            // Lire le header
            RESOURCEHEADER_SCI2 header;
            std::memcpy(&header, ressciData.data() + entry.offset, sizeof(header));
            
            uint32_t dataStart = entry.offset + sizeof(header);
            uint32_t dataEnd = dataStart + header.cbCompressed;
            
            if (dataEnd > ressciData.size()) {
                skippedCount++;
                continue;
            }
            
            scriptCount++;
            
            // Décompresser le script si nécessaire
            std::vector<uint8_t> decompressedData;
            const uint8_t* data = nullptr;
            size_t dataSize = 0;
            
            if (header.iMethod == 0) {
                // Pas de compression
                data = ressciData.data() + dataStart;
                dataSize = header.cbCompressed;
            } else if (header.iMethod == 32) {
                // STACpack/LZS
                decompressedData.resize(header.cbDecompressed);
                const uint8_t* compressedData = ressciData.data() + dataStart;
                
                int result = LZSDecompress(compressedData, header.cbCompressed,
                                          decompressedData.data(), header.cbDecompressed);
                
                if (result == 0) {
                    data = decompressedData.data();
                    dataSize = header.cbDecompressed;
                } else {
                    decompressErrors++;
                    continue;
                }
            } else {
                // Méthode de compression inconnue
                skippedCount++;
                continue;
            }
            
            if (!data || dataSize < 10 || dataSize > 1000000) continue;
            
            // Scanner pour CALLK KERNEL_ROBOT
            // Pattern: (0x42 ou 0x43) + 0x23 0x00 (little-endian WORD)
            for (size_t i = 0; i < dataSize - 10; i++) {
                if ((data[i] == OP_CALLK || data[i] == OP_CALLK_ALT) && 
                    i + 2 < dataSize) {
                    // Lire le numéro de kernel (WORD little-endian)
                    uint16_t kernelNum = data[i + 1] | (data[i + 2] << 8);
                    
                    if (kernelNum == KERNEL_ROBOT) {
                        // Chercher PUSHI en arrière
                        std::vector<uint16_t> params;
                        
                        for (int back = std::min<int>(i, 50); back > 0; back--) {
                            size_t pos = i - back;
                            
                            if (data[pos] == OP_PUSHI && pos + 2 < dataSize) {
                                uint16_t val = data[pos + 1] | (data[pos + 2] << 8);
                                params.push_back(val);
                                
                                if (val == robotNumber) {
                                    // Trouvé ! Chercher x, y dans les paramètres suivants
                                    std::cout << "✅ Script " << scriptNum << " offset 0x" 
                                              << std::hex << (dataStart + i) << std::dec 
                                              << " Robot(" << robotNumber << ")" << std::endl;
                                    std::cout << "   Paramètres trouvés:";
                                    for (uint16_t p : params) {
                                        std::cout << " " << p;
                                    }
                                    std::cout << std::endl;
                                    
                                    // Afficher contexte
                                    std::cout << "   Bytecode:";
                                    for (int ctx = -10; ctx < 10; ctx++) {
                                        int pos = static_cast<int>(i) + ctx;
                                        if (pos >= 0 && pos < static_cast<int>(dataSize)) {
                                            printf(" %02X", data[pos]);
                                        }
                                    }
                                    std::cout << std::endl << std::endl;
                                    
                                    found = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        
        std::cout << "\n📊 Scripts analysés: " << scriptCount << " / " << scripts.size() 
                  << " (ignorés: " << skippedCount << ", erreurs décompression: " << decompressErrors << ")\n";
        
        if (!found) {
            std::cout << "❌ Aucun appel à Robot(" << robotNumber << ") trouvé\n";
        }
    }
    
    void findAllRobotCalls() {
        std::cout << "\n🔍 Recherche de TOUS les appels Robot() dans les scripts\n";
        std::cout << "========================================================================\n\n";
        
        std::map<uint16_t, std::vector<std::pair<uint16_t, uint16_t>>> robotCalls;
        int totalScripts = 0, decompressErrors = 0;
        
        for (const auto& [scriptNum, entry] : scripts) {
            uint32_t offset = entry.offset;
            
            if (offset >= ressciData.size()) continue;
            
            RESOURCEHEADER_SCI2 header;
            std::memcpy(&header, ressciData.data() + offset, sizeof(header));
            
            uint32_t dataStart = offset + sizeof(header);
            uint32_t dataEnd = dataStart + header.cbCompressed;
            
            if (dataEnd > ressciData.size()) continue;
            
            // Décompresser le script
            std::vector<uint8_t> decompressedData;
            const uint8_t* data = nullptr;
            size_t dataSize = 0;
            
            if (header.iMethod == 0) {
                data = ressciData.data() + dataStart;
                dataSize = header.cbCompressed;
            } else if (header.iMethod == 32) {
                decompressedData.resize(header.cbDecompressed);
                const uint8_t* compressedData = ressciData.data() + dataStart;
                
                int result = LZSDecompress(compressedData, header.cbCompressed,
                                          decompressedData.data(), header.cbDecompressed);
                
                if (result == 0) {
                    data = decompressedData.data();
                    dataSize = header.cbDecompressed;
                } else {
                    decompressErrors++;
                    continue;
                }
            } else {
                continue;
            }
            
            if (!data || dataSize < 20) continue;
            totalScripts++;
            
            for (size_t i = 0; i < dataSize - 20; i++) {
                if ((data[i] == OP_CALLK || data[i] == OP_CALLK_ALT) && i + 2 < dataSize) {
                    uint16_t kernelNum = data[i + 1] | (data[i + 2] << 8);
                    
                    if (kernelNum == KERNEL_ROBOT) {
                        std::vector<uint16_t> params;
                        for (int j = std::min<int>(i, 100); j > 0; j--) {
                            size_t pos = i - j;
                            if (data[pos] == OP_PUSHI && pos + 2 < dataSize) {
                                uint16_t value = data[pos + 1] | (data[pos + 2] << 8);
                                if (value < 10000) params.push_back(value);
                            }
                        }
                        
                        if (params.size() >= 6) {
                            uint16_t robotNum = params[params.size() - 1];
                            uint16_t x = params[params.size() - 4];
                            uint16_t y = params[params.size() - 5];
                            
                            if (robotNum < 10000) {
                                robotCalls[robotNum].push_back({x, y});
                            }
                        }
                    }
                }
            }
        }
        
        std::cout << "📊 Scripts analysés: " << totalScripts << " / " << scripts.size()
                  << " (erreurs décompression: " << decompressErrors << ")\n";
        std::cout << "📊 Robots trouvés: " << robotCalls.size() << "\n\n";
        
        if (robotCalls.empty()) {
            std::cout << "❌ Aucun appel Robot() trouvé dans les scripts décompressés\n";
            std::cout << "   Cela peut signifier que les coordonnées sont calculées dynamiquement\n";
            std::cout << "   ou stockées dans des propriétés d'objets plutôt que des constantes.\n";
        } else {
            for (const auto& entry : robotCalls) {
                std::cout << "Robot " << std::setw(4) << entry.first << ": ";
                for (const auto& coords : entry.second) {
                    std::cout << "(" << coords.first << "," << coords.second << ") ";
                }
                std::cout << "\n";
            }
        }
    }
};  // Fin de la classe SCIResourceManager

int main(int argc, char* argv[]) {
    std::cout << "🔬 SCI Script Analyzer - Extracteur de coordonnées Robot\n";
    std::cout << "==========================================================\n\n";
    
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <RESMAP.00X> <RESSCI.00X> [robot_number]\n";
        std::cout << "\nExemples:\n";
        std::cout << "  " << argv[0] << " Resource/RESMAP.001 Resource/RESSCI.001 1000\n";
        std::cout << "  " << argv[0] << " Resource/RESMAP.002 Resource/RESSCI.002\n";
        return 1;
    }
    
    std::string resmapPath = argv[1];
    std::string ressciPath = argv[2];
    
    SCIResourceManager manager;
    
    if (!manager.loadRESMAP(resmapPath)) {
        return 1;
    }
    
    if (!manager.loadRESSCI(ressciPath)) {
        return 1;
    }
    
    if (argc >= 4) {
        uint16_t robotNumber = std::atoi(argv[3]);
        manager.findRobotCalls(robotNumber);
    } else {
        manager.findAllRobotCalls();
    }
    
    return 0;
}
