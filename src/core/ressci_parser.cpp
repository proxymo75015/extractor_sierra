/**
 * @file ressci_parser.cpp
 * @brief Implémentation du parser RESSCI/RESMAP avec détection automatique
 * 
 * Ce parser supporte automatiquement :
 * - Format 6 octets : type[1B] + number[2B LE] + offset[3B LE]
 *   Utilisé par variantes, démos et versions modifiées de Phantasmagoria
 * 
 * - Format 9 octets : type[1B] + number[4B LE] + offset[4B LE]
 *   Format SCI32 standard utilisé par les CD originaux de Phantasmagoria
 * 
 * La détection est effectuée automatiquement en analysant les 100 premières
 * entrées avec des critères de validation adaptés à chaque format.
 * 
 * @see docs/RESMAP_FORMAT_DETECTION.md
 * @author Extractor Sierra Project
 * @date 2025
 */

#include "ressci_parser.h"
#include "../formats/lzs.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>

namespace SCI {

// Noms des types de ressources
static const char* s_resourceTypeNames[] = {
    "View", "Pic", "Script", "Text", "Sound", "Memory", "Vocab", "Font",
    "Cursor", "Patch", "Bitmap", "Palette", "CdAudio", "Audio", "Sync",
    "Message", "Map", "Heap", "Audio36", "Sync36", "Chunk", "AudioMap"
};

// Noms des méthodes de compression
static const char* s_compressionMethodNames[] = {
    "None", "RLE Simple", "RLE Advanced", "Huffman", "LZ-Bit", "None (alias)",
    "RLE+Huffman", "LZ Advanced", "Unknown", "STACpack"
};

// Helpers pour lecture little-endian
static inline uint16_t readLE16(const uint8_t* data) {
    return data[0] | (data[1] << 8);
}

static inline uint32_t readLE32(const uint8_t* data) {
    return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

static inline int16_t readSLE16(const uint8_t* data) {
    return static_cast<int16_t>(readLE16(data));
}

RESSCIParser::RESSCIParser() {
}

RESSCIParser::~RESSCIParser() {
}

bool RESSCIParser::loadResMap(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Erreur: impossible d'ouvrir " << path << std::endl;
        return false;
    }
    
    // Lire tout le fichier
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    m_resMapData.resize(size);
    file.read(reinterpret_cast<char*>(m_resMapData.data()), size);
    
    std::cout << "RESMAP chargé: " << size << " octets" << std::endl;
    
    return parseResMapHeader();
}

/**
 * @brief Détecte automatiquement le format du RESMAP (6 ou 9 octets)
 * 
 * Analyse les 100 premières entrées avec chaque format candidat et valide
 * leur cohérence selon des critères spécifiques :
 * 
 * Format 6 octets (SCI1.1/variantes) :
 *   - Offset < 150 MB
 *   - Number < 65536 (uint16)
 *   - Validation permissive (tous types acceptés)
 * 
 * Format 9 octets (SCI32 standard) :
 *   - Type dans plage SCI (0x80-0x95)
 *   - Offset < 150 MB
 *   - Number < 100000
 *   - Offsets généralement croissants
 * 
 * @return FORMAT_6_BYTES, FORMAT_9_BYTES ou FORMAT_UNKNOWN
 */
RESSCIParser::ResMapFormat RESSCIParser::detectFormat() const {
    if (m_resMapData.size() < 6) return ResMapFormat::FORMAT_UNKNOWN;
    
    // Test format 6 octets  
    size_t numEntries6 = m_resMapData.size() / 6;
    int valid6 = 0;
    uint32_t lastOffset6 = 0;
    
    for (size_t i = 0; i < std::min(numEntries6, size_t(100)); i++) {
        const ResMapEntry6* entry = reinterpret_cast<const ResMapEntry6*>(&m_resMapData[i * 6]);
        uint8_t type = entry->type;
        uint16_t number = entry->resourceNumber;
        uint32_t offset = entry->getOffset();
        
        // Validation très permissive pour format 6 octets (variantes):
        // - Offset raisonnable (< 150 MB)
        // - Number raisonnable (< 65536, c'est un uint16)
        // - Offset généralement croissant (mais accepte offset=0 pour placeholders)
        if (offset < 150000000 && number < 65536) {
            valid6++;
            if (offset > 0 && offset > lastOffset6) lastOffset6 = offset;
        }
    }
    
    // Test format 9 octets
    size_t numEntries9 = m_resMapData.size() / 9;
    int valid9 = 0;
    uint32_t lastOffset9 = 0;
    
    for (size_t i = 0; i < std::min(numEntries9, size_t(100)); i++) {
        const ResMapEntry9* entry = reinterpret_cast<const ResMapEntry9*>(&m_resMapData[i * 9]);
        uint8_t type = entry->type;
        uint32_t offset = entry->offset;
        uint32_t number = entry->resourceNumber;
        
        // Pour format 9 octets: plus strict car c'est le format officiel
        if ((type >= 0x80 && type <= 0x95) && 
            offset < 150000000 && 
            number < 100000 &&
            (offset >= lastOffset9 || offset == 0)) {
            valid9++;
            if (offset > lastOffset9) lastOffset9 = offset;
        }
    }
    
    // Décision: au moins 50% des 100 premières entrées doivent être valides
    size_t samplesChecked = std::min(size_t(100), std::max(numEntries6, numEntries9));
    float threshold6 = 0.5f * samplesChecked;
    float threshold9 = 0.5f * samplesChecked;
    
    std::cout << "  [Détection] 6 octets: " << valid6 << "/" << std::min(numEntries6, size_t(100)) 
              << " valides, 9 octets: " << valid9 << "/" << std::min(numEntries9, size_t(100)) << " valides" << std::endl;
    
    if (valid6 >= threshold6 && valid6 > valid9) {
        return ResMapFormat::FORMAT_6_BYTES;
    } else if (valid9 >= threshold9) {
        return ResMapFormat::FORMAT_9_BYTES;
    }
    
    return ResMapFormat::FORMAT_UNKNOWN;
}

/**
 * @brief Parse le header RESMAP avec détection automatique du format
 * 
 * Cette méthode :
 * 1. Détecte automatiquement le format (6 ou 9 octets)
 * 2. Parse toutes les entrées selon le format détecté
 * 3. Filtre les entrées invalides (types hors plage SCI)
 * 4. Construit l'index des ressources (type, number) -> offset
 * 
 * Format 6 octets : 1920 entrées max (11520 octets)
 * Format 9 octets : 12847+ entrées (Phantasmagoria CD1)
 * 
 * @return true si au moins une ressource valide a été indexée
 */
bool RESSCIParser::parseResMapHeader() {
    if (m_resMapData.size() < 6) {
        std::cerr << "RESMAP trop petit" << std::endl;
        return false;
    }
    
    // Détection automatique du format
    ResMapFormat format = detectFormat();
    
    std::map<uint8_t, int> typeCounts;
    
    if (format == ResMapFormat::FORMAT_6_BYTES) {
        // Format 6 octets: type[1B] + number[2B LE] + offset[3B LE]
        size_t numEntries = m_resMapData.size() / 6;
        std::cout << "✓ Format détecté: 6 octets (SCI1.1/variante)" << std::endl;
        std::cout << "  Nombre d'entrées: " << numEntries << std::endl;
        
        for (size_t i = 0; i < numEntries; i++) {
            size_t offset = i * 6;
            if (offset + 6 > m_resMapData.size()) break;
            
            const ResMapEntry6* entry = reinterpret_cast<const ResMapEntry6*>(&m_resMapData[offset]);
            
            uint8_t type = entry->type;
            uint16_t number = entry->resourceNumber;
            uint32_t resOffset = entry->getOffset();
            
            // Filtrer les types invalides (fin de table, etc.)
            if (type < 0x80 || type > 0x95) continue;
            
            ResourceType resType = static_cast<ResourceType>(type);
            auto key = std::make_pair(resType, static_cast<uint32_t>(number));
            m_resourceIndex[key] = resOffset;
            m_resourceVolumes[key] = 1;
            
            typeCounts[type]++;
        }
        
    } else if (format == ResMapFormat::FORMAT_9_BYTES) {
        // Format 9 octets: type[1B] + number[4B LE] + offset[4B LE]
        size_t numEntries = m_resMapData.size() / 9;
        std::cout << "✓ Format détecté: 9 octets (SCI32 standard)" << std::endl;
        std::cout << "  Nombre d'entrées: " << numEntries << std::endl;
        
        for (size_t i = 0; i < numEntries; i++) {
            size_t offset = i * 9;
            if (offset + 9 > m_resMapData.size()) break;
            
            const ResMapEntry9* entry = reinterpret_cast<const ResMapEntry9*>(&m_resMapData[offset]);
            
            uint8_t type = entry->type;
            uint32_t number = entry->resourceNumber;
            uint32_t resOffset = entry->offset;
            
            if (type < 0x80 || type > 0x95) continue;
            
            ResourceType resType = static_cast<ResourceType>(type);
            auto key = std::make_pair(resType, number);
            m_resourceIndex[key] = resOffset;
            m_resourceVolumes[key] = 1;
            
            typeCounts[type]++;
        }
        
    } else {
        std::cerr << "❌ Format RESMAP non reconnu (ni 6 ni 9 octets valides)" << std::endl;
        return false;
    }
    
    std::cout << "  Types de ressources trouvés: " << typeCounts.size() << std::endl;
    
    for (const auto& [type, count] : typeCounts) {
        std::cout << "    " << getResourceTypeName(static_cast<ResourceType>(type))
                 << " (0x" << std::hex << (int)type << std::dec << "): "
                 << count << " ressources" << std::endl;
    }
    
    std::cout << "  Total ressources indexées: " << m_resourceIndex.size() << std::endl;
    
    return !m_resourceIndex.empty();
}

bool RESSCIParser::loadRessci(const std::string& path, uint8_t volumeNumber) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Erreur: impossible d'ouvrir " << path << std::endl;
        return false;
    }
    
    // Lire tout le fichier
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    auto& volumeData = m_ressciData[volumeNumber];
    volumeData.resize(size);
    file.read(reinterpret_cast<char*>(volumeData.data()), size);
    
    std::cout << "RESSCI volume " << (int)volumeNumber << " chargé: " 
              << size << " octets" << std::endl;
    
    return true;
}

ResourceInfo RESSCIParser::extractResource(ResourceType type, uint32_t number) {
    ResourceInfo info;
    info.type = type;
    info.number = number;
    info.offset = 0;
    info.compressedSize = 0;
    info.decompressedSize = 0;
    info.method = CM_NONE;
    info.volume = 1;
    
    // Chercher dans l'index
    auto key = std::make_pair(type, number);
    auto it = m_resourceIndex.find(key);
    if (it == m_resourceIndex.end()) {
        std::cerr << "Ressource " << getResourceTypeName(type) 
                  << " #" << number << " non trouvée" << std::endl;
        return info;
    }
    
    info.offset = it->second;
    
    // Trouver le volume
    auto volIt = m_resourceVolumes.find(key);
    if (volIt != m_resourceVolumes.end()) {
        info.volume = volIt->second;
    }
    
    // Vérifier que le volume est chargé
    auto dataIt = m_ressciData.find(info.volume);
    if (dataIt == m_ressciData.end()) {
        std::cerr << "Volume " << (int)info.volume << " non chargé" << std::endl;
        return info;
    }
    
    const auto& volumeData = dataIt->second;
    
    // Vérifier que l'offset est valide
    if (info.offset + sizeof(ResourceHeader) > volumeData.size()) {
        std::cerr << "Offset invalide: " << info.offset << std::endl;
        return info;
    }
    
    // Lire le header de la ressource (SCI32 = 14 octets)
    ResourceHeader header;
    std::memcpy(&header, volumeData.data() + info.offset, sizeof(header));
    
    info.compressedSize = header.compressedSize;
    info.decompressedSize = header.decompressedSize;
    info.method = static_cast<CompressionMethod>(header.method);
    
    // Lire les données compressées (compressedSize = taille exacte des données)
    size_t dataOffset = info.offset + sizeof(ResourceHeader);  // 14 octets
    size_t dataSize = info.compressedSize;
    
    if (dataOffset + dataSize > volumeData.size()) {
        std::cerr << "Données trop grandes: offset=" << dataOffset 
                  << " size=" << dataSize << " total=" << volumeData.size() << std::endl;
        dataSize = volumeData.size() - dataOffset;
    }
    
    std::vector<uint8_t> compressedData(volumeData.begin() + dataOffset,
                                        volumeData.begin() + dataOffset + dataSize);
    
    // Décompresser
    info.data = decompress(compressedData, info.method, info.decompressedSize);
    
    return info;
}

std::vector<ResourceInfo> RESSCIParser::extractAllResourcesOfType(ResourceType type) {
    std::vector<ResourceInfo> resources;
    
    for (const auto& [key, offset] : m_resourceIndex) {
        if (key.first == type) {
            ResourceInfo info = extractResource(type, key.second);
            if (!info.data.empty()) {
                resources.push_back(info);
            }
        }
    }
    
    return resources;
}

std::vector<uint8_t> RESSCIParser::decompress(
    const std::vector<uint8_t>& compressed,
    CompressionMethod method,
    uint32_t decompressedSize)
{
    switch (method) {
        case CM_NONE:
        case CM_NONE_ALIAS:
            return compressed;
            
        case CM_RLE_SIMPLE:
        case CM_RLE_ADV:
        case CM_RLE_0x34:
            return decompressRLE(compressed, decompressedSize);
            
        case CM_HUFFMAN:
        case CM_HUFFMAN_V56:
            return decompressHuffman(compressed, decompressedSize);
            
        case CM_LZ_BIT:
        case CM_LZ_ADV:
        case CM_LZSS_31:
        case CM_UNKNOWN:
            return decompressLZ(compressed, decompressedSize);
            
        case CM_STACPACK:
        case CM_STACPACK_OLD:
            return decompressSTACpack(compressed, decompressedSize);
            
        case CM_RLE_HUFF:
            // Décompresser d'abord avec Huffman, puis RLE
            {
                auto temp = decompressHuffman(compressed, decompressedSize * 2);
                return decompressRLE(temp, decompressedSize);
            }
            
        default:
            std::cerr << "Méthode de compression non supportée: 0x" 
                     << std::hex << (int)method << std::dec << std::endl;
            return std::vector<uint8_t>();
    }
}

std::vector<uint8_t> RESSCIParser::decompressRLE(
    const std::vector<uint8_t>& data,
    uint32_t decompSize)
{
    std::vector<uint8_t> result;
    result.reserve(decompSize);
    
    size_t pos = 0;
    while (pos < data.size() && result.size() < decompSize) {
        uint8_t code = data[pos++];
        
        if (code & 0x80) {
            // Run: répéter le prochain octet (code & 0x7F) fois
            if (pos >= data.size()) break;
            
            uint8_t value = data[pos++];
            int count = code & 0x7F;
            
            for (int i = 0; i < count && result.size() < decompSize; i++) {
                result.push_back(value);
            }
        } else {
            // Literal: copier les (code) octets suivants
            int count = code;
            
            for (int i = 0; i < count && pos < data.size() && result.size() < decompSize; i++) {
                result.push_back(data[pos++]);
            }
        }
    }
    
    return result;
}

std::vector<uint8_t> RESSCIParser::decompressHuffman(
    const std::vector<uint8_t>& data,
    uint32_t decompSize)
{
    // Implémentation simplifiée de Huffman
    // Pour une implémentation complète, voir ScummVM engines/sci/util.cpp
    
    std::vector<uint8_t> result;
    result.reserve(decompSize);
    
    // TODO: Implémenter l'arbre Huffman complet
    // Pour l'instant, retourner les données telles quelles
    std::cerr << "AVERTISSEMENT: Décompression Huffman non implémentée complètement" << std::endl;
    
    return data;
}

std::vector<uint8_t> RESSCIParser::decompressLZ(
    const std::vector<uint8_t>& data,
    uint32_t decompSize)
{
    // Implémentation LZSS-like pour SCI
    std::vector<uint8_t> result;
    result.reserve(decompSize);
    
    size_t pos = 0;
    while (pos < data.size() && result.size() < decompSize) {
        uint8_t code = data[pos++];
        
        for (int bit = 0; bit < 8 && result.size() < decompSize; bit++) {
            if (pos >= data.size()) break;
            
            if (code & (1 << bit)) {
                // Literal
                result.push_back(data[pos++]);
            } else {
                // LZ reference: [offset(12 bits), length(4 bits)]
                if (pos + 1 >= data.size()) break;
                
                uint16_t ref = readLE16(&data[pos]);
                pos += 2;
                
                int offset = ref >> 4;
                int length = (ref & 0x0F) + 3;
                
                // Copier depuis le dictionnaire
                for (int i = 0; i < length && result.size() < decompSize; i++) {
                    if (offset <= result.size()) {
                        result.push_back(result[result.size() - offset]);
                    }
                }
            }
        }
    }
    
    return result;
}

std::vector<uint8_t> RESSCIParser::decompressSTACpack(
    const std::vector<uint8_t>& data,
    uint32_t decompSize)
{
    // STACpack est l'algorithme LZS utilisé par Phantasmagoria
    // On utilise la fonction LZSDecompress existante
    
    std::vector<uint8_t> result(decompSize);
    
    int ret = LZSDecompress(data.data(), data.size(), result.data(), decompSize);
    
    if (ret < 0) {
        std::cerr << "Erreur LZS decompression: " << ret << std::endl;
        return std::vector<uint8_t>();
    }
    
    return result;
}

std::vector<RobotCoordinates> RESSCIParser::extractRobotCoordinates() {
    std::vector<RobotCoordinates> allCoords;
    
    // Extraire tous les scripts
    auto scripts = extractAllResourcesOfType(RT_SCRIPT);
    
    std::cout << "\nAnalyse de " << scripts.size() << " scripts pour coordonnées Robot..." << std::endl;
    
    for (const auto& script : scripts) {
        auto coords = parseScriptForRobotCalls(script.data, script.number);
        allCoords.insert(allCoords.end(), coords.begin(), coords.end());
    }
    
    // Extraire tous les HEAPs
    auto heaps = extractAllResourcesOfType(RT_HEAP);
    
    std::cout << "Analyse de " << heaps.size() << " HEAPs pour coordonnées Robot..." << std::endl;
    
    for (const auto& heap : heaps) {
        auto coords = parseHeapForCoordinates(heap.data, heap.number);
        allCoords.insert(allCoords.end(), coords.begin(), coords.end());
    }
    
    return allCoords;
}

std::vector<RobotCoordinates> RESSCIParser::parseScriptForRobotCalls(
    const std::vector<uint8_t>& scriptData,
    uint32_t scriptId)
{
    std::vector<RobotCoordinates> coords;
    
    if (scriptData.size() < 20) return coords;
    
    // Opcodes SCI2.1
    const uint8_t OP_PUSHI = 0x38;       // Push immediate value
    const uint8_t OP_CALLK = 0x42;       // Call kernel function
    const uint8_t OP_CALLK_LONG = 0x43;  // Call kernel with more args
    
    // kRobot kernel ID en SCI2.1
    const uint16_t KROBOT_KERNEL_ID = 0x7B; // 123 decimal
    
    // Scanner le bytecode
    for (size_t i = 0; i < scriptData.size() - 20; i++) {
        uint8_t opcode = scriptData[i];
        
        if (opcode == OP_CALLK || opcode == OP_CALLK_LONG) {
            if (i + 4 > scriptData.size()) break;
            
            uint16_t kernelId = readLE16(&scriptData[i + 1]);
            uint8_t argc = scriptData[i + 3];
            
            if (kernelId == KROBOT_KERNEL_ID && argc >= 5) {
                // Remonter pour trouver les arguments (pushi values)
                std::vector<int16_t> args;
                size_t searchPos = (i > 100) ? i - 100 : 0;
                
                // Chercher les pushi avant le callk
                for (size_t j = i; j > searchPos && args.size() < argc; j--) {
                    if (scriptData[j] == OP_PUSHI && j + 3 <= scriptData.size()) {
                        int16_t value = readSLE16(&scriptData[j + 1]);
                        args.insert(args.begin(), value);
                    }
                }
                
                // Vérifier si c'est un kRobotOpen avec coordonnées
                if (args.size() >= 5) {
                    int16_t subop = args[0];
                    int16_t robotId = args[1];
                    int16_t x = args.size() > 3 ? args[3] : 0;
                    int16_t y = args.size() > 4 ? args[4] : 0;
                    int16_t priority = args.size() > 2 ? args[2] : 0;
                    int16_t scale = args.size() > 5 ? args[5] : 128;
                    
                    // subop 0 = kRobotOpen
                    if (subop == 0 && robotId > 0 && robotId < 10000) {
                        // Vérifier que les coordonnées sont plausibles
                        if (x >= 0 && x <= 630 && y >= 0 && y <= 450) {
                            RobotCoordinates rc;
                            rc.robotId = robotId;
                            rc.x = x;
                            rc.y = y;
                            rc.priority = priority;
                            rc.scale = scale;
                            rc.scriptId = scriptId;
                            coords.push_back(rc);
                        }
                    }
                }
            }
        }
    }
    
    return coords;
}

std::vector<RobotCoordinates> RESSCIParser::parseHeapForCoordinates(
    const std::vector<uint8_t>& heapData,
    uint32_t scriptId)
{
    std::vector<RobotCoordinates> coords;
    
    if (heapData.size() < 20) return coords;
    
    // Dans le HEAP, chercher des paires de valeurs qui pourraient être des coordonnées
    // Résolution Phantasmagoria: 630x450
    
    for (size_t i = 0; i < heapData.size() - 10; i += 2) {
        if (i + 4 > heapData.size()) break;
        
        int16_t val1 = readSLE16(&heapData[i]);
        int16_t val2 = readSLE16(&heapData[i + 2]);
        
        // Vérifier si ce sont des coordonnées plausibles
        if (val1 >= 0 && val1 <= 630 && val2 >= 0 && val2 <= 450) {
            // Chercher un Robot ID à proximité
            // Format typique: [robotId(2), ..., x(2), y(2)]
            
            for (size_t j = (i > 20 ? i - 20 : 0); j < i; j += 2) {
                if (j + 2 > heapData.size()) break;
                
                int16_t potentialId = readSLE16(&heapData[j]);
                
                // Les Robot IDs sont généralement entre 1 et 10000
                if (potentialId > 0 && potentialId < 10000) {
                    // Vérifier qu'il n'y a pas de valeurs aberrantes entre
                    bool valid = true;
                    for (size_t k = j + 2; k < i; k += 2) {
                        int16_t intermediate = readSLE16(&heapData[k]);
                        // Les valeurs intermédiaires doivent être raisonnables
                        if (intermediate < -1000 || intermediate > 10000) {
                            valid = false;
                            break;
                        }
                    }
                    
                    if (valid) {
                        RobotCoordinates rc;
                        rc.robotId = potentialId;
                        rc.x = val1;
                        rc.y = val2;
                        rc.priority = 0;
                        rc.scale = 128;
                        rc.scriptId = scriptId;
                        coords.push_back(rc);
                        break;
                    }
                }
            }
        }
    }
    
    return coords;
}

const char* RESSCIParser::getResourceTypeName(ResourceType type) {
    switch (type) {
        case RT_VIEW: return "View";
        case RT_PIC: return "Pic";
        case RT_SCRIPT: return "Script";
        case RT_TEXT: return "Text";
        case RT_SOUND: return "Sound";
        case RT_MEMORY: return "Memory";
        case RT_VOCAB: return "Vocab";
        case RT_FONT: return "Font";
        case RT_CURSOR: return "Cursor";
        case RT_PATCH: return "Patch";
        case RT_BITMAP: return "Bitmap";
        case RT_PALETTE: return "Palette";
        case RT_AUDIO: return "Audio";
        case RT_SYNC: return "Sync";
        case RT_MESSAGE: return "Message";
        case RT_MAP: return "Map";
        case RT_HEAP: return "Heap";
        case RT_AUDIO36: return "Audio36";
        case RT_SYNC36: return "Sync36";
        case RT_CHUNK: return "Chunk";
        case RT_AUDIOMAP: return "AudioMap";
        default: return "Unknown";
    }
}

const char* RESSCIParser::getCompressionMethodName(CompressionMethod method) {
    switch (method) {
        case CM_NONE: return "None";
        case CM_RLE_SIMPLE: return "RLE Simple";
        case CM_RLE_ADV: return "RLE Advanced";
        case CM_HUFFMAN: return "Huffman";
        case CM_LZ_BIT: return "LZ-Bit";
        case CM_RLE_HUFF: return "RLE+Huffman";
        case CM_LZ_ADV: return "LZ Advanced";
        case CM_HUFFMAN_20: return "Huffman 0x20";
        case CM_LZSS_31: return "LZSS 0x31";
        case CM_RLE_0x34: return "RLE 0x34";
        case CM_HUFFMAN_V56: return "Huffman V56";
        case CM_DPCM_3C: return "DPCM Audio";
        case CM_STACPACK_OLD: return "STACpack Old";
        case CM_STACPACK: return "STACpack/LZS (0x7B)";
        default: return "Unknown";
    }
}

} // namespace SCI
