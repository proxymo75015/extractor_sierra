/**
 * Extracteur de positions Robot depuis scripts SCI
 * Basé sur l'architecture ScummVM
 * 
 * Parse les scripts SCI2.1 pour trouver les appels kRobot(0, robotId, plane, priority, x, y)
 * et extraire les coordonnées X/Y passées en paramètres
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <map>
#include <string>
#include <algorithm>

// Déclaration de la fonction LZS (depuis lzs.cpp)
int LZSDecompress(const uint8_t *in, uint32_t inSize, uint8_t *out, uint32_t outSize);

// Structure pour un appel Robot trouvé
struct RobotCall {
    uint16_t scriptId;
    uint32_t offset;
    uint16_t robotId;
    int16_t x;
    int16_t y;
    int16_t priority;
    int16_t scale;
};

// Lecture little-endian
static uint16_t readUint16LE(const uint8_t* data) {
    return data[0] | (data[1] << 8);
}

static int16_t readSint16LE(const uint8_t* data) {
    return (int16_t)(data[0] | (data[1] << 8));
}

static uint32_t readUint32LE(const uint8_t* data) {
    return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

/**
 * Parse un script SCI pour trouver les appels kRobot
 * Basé sur ScummVM engines/sci/engine/script.cpp
 */
std::vector<RobotCall> parseScriptForRobotCalls(uint16_t scriptId, const uint8_t* scriptData, size_t scriptSize) {
    std::vector<RobotCall> calls;
    
    if (scriptSize < 10) return calls;
    
    // Opcodes SCI2.1 (selon ScummVM kernel_tables.h et script.cpp)
    const uint8_t OP_PUSHI = 0x38;      // Push immediate value
    const uint8_t OP_PUSH = 0x39;       // Push variable
    const uint8_t OP_CALLK = 0x42;      // Call kernel function
    const uint8_t OP_CALLK_LONG = 0x43; // Call kernel with more args
    
    // kRobot kernel ID en SCI2.1 (kernel_tables.h)
    const uint16_t KROBOT_KERNEL_ID = 0x7B; // 123 decimal
    
    // Subop IDs pour kRobot
    const int16_t KROBOT_OPEN = 0;
    
    // Scanner le bytecode
    size_t i = 0;
    while (i < scriptSize - 20) {
        uint8_t opcode = scriptData[i];
        
        // Chercher les séquences d'appel kRobot
        // Pattern typique en SCI2.1:
        // pushi 0           ; subop = kRobotOpen
        // pushi <robotId>   ; robot number
        // pushi <plane>     ; plane object
        // pushi <priority>  ; priority
        // pushi <x>         ; X position
        // pushi <y>         ; Y position
        // [pushi <scale>]   ; optional scale
        // callk 0x7B, argc  ; kRobot with argc arguments
        
        if (opcode == OP_CALLK || opcode == OP_CALLK_LONG) {
            if (i + 4 > scriptSize) break;
            
            uint16_t kernelId = readUint16LE(&scriptData[i + 1]);
            uint8_t argc = scriptData[i + 3];
            
            if (kernelId == KROBOT_KERNEL_ID && argc >= 5) {
                // Remonter pour trouver les arguments (pushi values)
                std::vector<int16_t> args;
                size_t searchPos = i;
                
                // Chercher jusqu'à argc arguments pushi en remontant
                for (int argIdx = 0; argIdx < argc && searchPos > 0 && searchPos > i - 100; argIdx++) {
                    searchPos--;
                    
                    // Chercher le pushi précédent
                    while (searchPos > 0 && searchPos > i - 100) {
                        if (scriptData[searchPos] == OP_PUSHI) {
                            if (searchPos + 3 <= scriptSize) {
                                int16_t value = readSint16LE(&scriptData[searchPos + 1]);
                                args.insert(args.begin(), value);
                                searchPos += 2; // Avancer au début de l'instruction
                                break;
                            }
                        }
                        searchPos--;
                    }
                }
                
                // Vérifier si c'est bien un kRobotOpen avec les bons arguments
                if (args.size() >= 5) {
                    int16_t subop = args[0];
                    int16_t robotId = args[1];
                    // args[2] = plane (ignoré)
                    int16_t priority = args.size() > 2 ? args[2] : 0;
                    int16_t x = args.size() > 3 ? args[3] : 0;
                    int16_t y = args.size() > 4 ? args[4] : 0;
                    int16_t scale = args.size() > 5 ? args[5] : 128;
                    
                    if (subop == KROBOT_OPEN && robotId > 0 && robotId < 10000) {
                        RobotCall call;
                        call.scriptId = scriptId;
                        call.offset = i;
                        call.robotId = robotId;
                        call.x = x;
                        call.y = y;
                        call.priority = priority;
                        call.scale = scale;
                        calls.push_back(call);
                    }
                }
            }
        }
        
        i++;
    }
    
    return calls;
}

/**
 * Extrait et parse tous les scripts depuis RESSCI
 */
int main(int argc, char** argv) {
    printf("=== EXTRACTEUR DE POSITIONS ROBOT DEPUIS SCRIPTS SCI ===\n\n");
    
    // Map pour stocker les positions par robot ID
    std::map<uint16_t, std::vector<RobotCall>> robotPositions;
    
    // Lire RESMAP et RESSCI
    const char* resmapPath = "RESMAP.001";
    FILE* resmapFile = fopen(resmapPath, "rb");
    if (!resmapFile) {
        fprintf(stderr, "Erreur: impossible d'ouvrir %s\n", resmapPath);
        return 1;
    }
    
    fseek(resmapFile, 0, SEEK_END);
    size_t resmapSize = ftell(resmapFile);
    fseek(resmapFile, 0, SEEK_SET);
    
    uint8_t* resmapData = new uint8_t[resmapSize];
    if (fread(resmapData, 1, resmapSize, resmapFile) != resmapSize) {
        fprintf(stderr, "Erreur de lecture RESMAP\n");
        delete[] resmapData;
        fclose(resmapFile);
        return 1;
    }
    fclose(resmapFile);
    
    printf("RESMAP chargé: %zu bytes\n", resmapSize);
    
    // Scanner tous les RESSCI
    for (int volNum = 1; volNum <= 10; volNum++) {
        char ressciPath[256];
        snprintf(ressciPath, sizeof(ressciPath), "RESSCI.%03d", volNum);
        
        FILE* ressciFile = fopen(ressciPath, "rb");
        if (!ressciFile) {
            if (volNum == 1) {
                fprintf(stderr, "Erreur: impossible d'ouvrir %s\n", ressciPath);
                delete[] resmapData;
                return 1;
            }
            break; // Plus de volumes
        }
        
        fseek(ressciFile, 0, SEEK_END);
        size_t ressciSize = ftell(ressciFile);
        fseek(ressciFile, 0, SEEK_SET);
        
        uint8_t* ressciData = new uint8_t[ressciSize];
        if (fread(ressciData, 1, ressciSize, ressciFile) != ressciSize) {
            fprintf(stderr, "Erreur de lecture RESSCI\n");
            delete[] ressciData;
            fclose(ressciFile);
            continue;
        }
        fclose(ressciFile);
        
        printf("RESSCI.%03d chargé: %zu bytes\n", volNum, ressciSize);
        
        // Parser RESMAP (format SCI2.1: 7 bytes par entrée)
        size_t numEntries = resmapSize / 7;
        
        for (size_t i = 0; i < numEntries; i++) {
            size_t entryOffset = i * 7;
            if (entryOffset + 7 > resmapSize) break;
            
            uint32_t resOffset = readUint32LE(&resmapData[entryOffset]);
            uint16_t resId = readUint16LE(&resmapData[entryOffset + 4]);
            uint8_t volId = resmapData[entryOffset + 6];
            
            // Extraire type et numéro de ressource
            uint8_t resType = (resId >> 11) & 0x1F;
            uint16_t resNumber = resId & 0x7FF;
            
            // Type 1 = Script
            if (resType == 1 && volId == volNum) {
                if (resOffset + 4 >= ressciSize) continue;
                
                // Lire la taille de la ressource
                uint16_t resSize = readUint16LE(&ressciData[resOffset]);
                uint16_t compSize = readUint16LE(&ressciData[resOffset + 2]);
                
                if (resOffset + resSize > ressciSize) continue;
                
                const uint8_t* scriptData = &ressciData[resOffset + 4];
                size_t scriptSize = resSize - 4;
                
                // Décompresser si nécessaire (compression LZS)
                uint8_t* decompressedData = nullptr;
                if (compSize > 0 && compSize != resSize) {
                    decompressedData = new uint8_t[compSize];
                    int result = LZSDecompress(scriptData, scriptSize, decompressedData, compSize);
                    
                    if (result == 0) {
                        scriptData = decompressedData;
                        scriptSize = compSize;
                    }
                }
                
                // Parser le script
                auto calls = parseScriptForRobotCalls(resNumber, scriptData, scriptSize);
                
                if (!calls.empty()) {
                    printf("\nScript %d: %zu appels kRobot trouvés\n", resNumber, calls.size());
                    
                    for (const auto& call : calls) {
                        printf("  Robot %d: X=%d, Y=%d (priority=%d, scale=%d)\n",
                               call.robotId, call.x, call.y, call.priority, call.scale);
                        
                        robotPositions[call.robotId].push_back(call);
                    }
                }
                
                if (decompressedData) {
                    delete[] decompressedData;
                }
            }
        }
        
        delete[] ressciData;
    }
    
    delete[] resmapData;
    
    // Résumé des positions trouvées
    printf("\n=== RÉSUMÉ DES POSITIONS ROBOT ===\n\n");
    
    if (robotPositions.empty()) {
        printf("Aucun appel kRobot trouvé dans les scripts.\n");
        printf("\nREMARQUE: Les coordonnées peuvent être calculées dynamiquement\n");
        printf("via des variables ou des propriétés d'objets. Dans ce cas,\n");
        printf("il faudrait émuler le moteur SCI pour obtenir les vraies valeurs.\n");
    } else {
        for (const auto& pair : robotPositions) {
            uint16_t robotId = pair.first;
            const auto& calls = pair.second;
            
            printf("Robot %d: %zu appels trouvés\n", robotId, calls.size());
            
            // Grouper par position unique
            std::map<std::pair<int16_t, int16_t>, int> positionCounts;
            for (const auto& call : calls) {
                positionCounts[{call.x, call.y}]++;
            }
            
            printf("  Positions uniques:\n");
            for (const auto& posPair : positionCounts) {
                printf("    X=%d, Y=%d (utilisé %d fois)\n",
                       posPair.first.first, posPair.first.second, posPair.second);
            }
        }
        
        // Écrire le fichier de configuration
        FILE* outFile = fopen("robot_positions_extracted.txt", "w");
        if (outFile) {
            fprintf(outFile, "# Robot Position Configuration\n");
            fprintf(outFile, "# Extrait automatiquement des scripts SCI\n");
            fprintf(outFile, "# Format: robot_id X Y\n\n");
            
            for (const auto& pair : robotPositions) {
                uint16_t robotId = pair.first;
                const auto& calls = pair.second;
                
                // Utiliser la position la plus fréquente
                std::map<std::pair<int16_t, int16_t>, int> positionCounts;
                for (const auto& call : calls) {
                    positionCounts[{call.x, call.y}]++;
                }
                
                // Trouver la position la plus commune
                std::pair<int16_t, int16_t> mostCommonPos = {0, 0};
                int maxCount = 0;
                for (const auto& posPair : positionCounts) {
                    if (posPair.second > maxCount) {
                        maxCount = posPair.second;
                        mostCommonPos = posPair.first;
                    }
                }
                
                if (maxCount > 0) {
                    fprintf(outFile, "%d %d %d\n",
                            robotId, mostCommonPos.first, mostCommonPos.second);
                }
            }
            
            fclose(outFile);
            printf("\nFichier robot_positions_extracted.txt créé.\n");
        }
    }
    
    return 0;
}
