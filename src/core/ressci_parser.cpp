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
#include <set>
#include <set>

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

// Suivi des méthodes de compression non supportées
std::set<uint8_t> RESSCIParser::s_unsupportedMethodsLogged;

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

RESSCIParser::RESSCIParser() : m_currentVolume(1), m_detectedFormat(ResMapFormat::FORMAT_UNKNOWN) {
}

RESSCIParser::~RESSCIParser() {
}

bool RESSCIParser::loadResMap(const std::string& path, uint8_t volumeNumber) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Erreur: impossible d'ouvrir " << path << std::endl;
        return false;
    }
    
    // Sauvegarder le numéro de volume pour l'indexation
    m_currentVolume = volumeNumber;
    
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
    
    // PARSING STYLE SCUMMVM : Table d'indirection + listes par type
    // Format SCI1/SCI1.1 : Header avec (type[1B] + offset[2B LE]) répété jusqu'à 0xFF
    // Puis pour chaque type : liste de (number[2B LE] + offset[3B ou 4B LE])
    
    std::map<uint8_t, int> typeCounts;
    std::map<uint8_t, std::pair<uint16_t, uint16_t>> typeTable; // type -> (offset, size)
    
    // ÉTAPE 1 : Lire la table d'indirection (header)
    size_t pos = 0;
    uint8_t prevType = 0;
    uint16_t prevOffset = 0;
    
    std::cout << "Parsing RESMAP (style ScummVM avec table d'indirection)..." << std::endl;
    
    // Table de conversion type index → ResourceType (comme ScummVM)
    // Index dans RESMAP (0-31) → ResourceType (0x80-0x95)
    static const uint8_t typeConversionTable[] = {
        0x88, // 0x00 → RT_VIEW
        0x87, // 0x01 → RT_PIC  
        0x80, // 0x02 → RT_SCRIPT
        0x83, // 0x03 → RT_TEXT
        0x81, // 0x04 → RT_SOUND
        0x82, // 0x05 → RT_MEMORY
        0x86, // 0x06 → RT_VOCAB
        0x84, // 0x07 → RT_FONT
        0x85, // 0x08 → RT_CURSOR
        0x89, // 0x09 → RT_PATCH
        0x8A, // 0x0A → RT_BITMAP
        0x8B, // 0x0B → RT_PALETTE
        0x8C, // 0x0C → RT_CDAUDIO
        0x8D, // 0x0D → RT_AUDIO
        0x8E, // 0x0E → RT_SYNC
        0x8F, // 0x0F → RT_MESSAGE
        0x90, // 0x10 → RT_CHUNK
        0x91, // 0x11 → RT_HEAP
        0x92, // 0x12 → RT_AUDIO36
        0x93, // 0x13 → RT_SYNC36
        0x94, // 0x14 → RT_CHUNK
        0x95, // 0x15 → RT_AUDIOMAP
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF  // 0x16-0x1F unused
    };
    
    // ÉTAPE 1a : Lire tous les (type, offset) y compris le terminateur
    std::vector<std::pair<uint8_t, uint16_t>> allEntries;
    
    while (pos + 3 <= m_resMapData.size()) {
        uint8_t typeRaw = m_resMapData[pos];
        uint8_t type = typeRaw & 0x1F;  // Masquer pour obtenir type réel
        uint16_t offset = readLE16(&m_resMapData[pos + 1]);
        
        pos += 3;
        
        allEntries.push_back({type, offset});
        
        // 0x1F (0xFF & 0x1F) = terminateur
        if (type == 0x1F) {
            std::cout << "  Terminateur trouvé à l'offset " << pos - 3 << std::endl;
            break;
        }
    }
    
    // ÉTAPE 1b : Calculer les tailles en utilisant l'offset suivant
    for (size_t i = 0; i < allEntries.size(); i++) {
        uint8_t type = allEntries[i].first;
        uint16_t offset = allEntries[i].second;
        
        if (type == 0x1F) break;  // Ignorer le terminateur
        
        // Calculer la taille jusqu'au prochain offset (ou fin du fichier)
        uint16_t size;
        if (i + 1 < allEntries.size()) {
            size = allEntries[i + 1].second - offset;
        } else {
            // Dernier type : utiliser la taille du fichier RESMAP
            size = m_resMapData.size() - offset;
        }
        
        typeTable[type] = std::make_pair(offset, size);
    }
    
    std::cout << "  Table d'indirection : " << typeTable.size() << " types trouvés" << std::endl;
    
    // ÉTAPE 2 : Détection automatique du format d'entrée (comme ScummVM)
    // SCI1.1/SCI2: 5 bytes (number[2B] + offset[3B])
    // SCI0/SCI1:   6 bytes (number[2B] + offset[4B])
    // Méthode ScummVM: tester si les tailles de sections sont divisibles par 5 ou 6
    
    ResMapFormat mapDetected = RES_FORMAT_UNKNOWN;
    
    // Parcourir les sections pour détecter le format
    for (const auto& [typeRaw, offsetSize] : typeTable) {
        uint16_t sizeBytes = offsetSize.second;
        
        if (sizeBytes == 0) continue;
        
        // ScummVM logic (resource.cpp ligne 1352-1356):
        // if ((directorySize % 5) && (directorySize % 6 == 0))
        //     mapDetected = kResVersionSci1Late;
        // if ((directorySize % 5 == 0) && (directorySize % 6))
        //     mapDetected = kResVersionSci11;
        
        bool divisibleBy5 = (sizeBytes % 5 == 0);
        bool divisibleBy6 = (sizeBytes % 6 == 0);
        
        if (!divisibleBy5 && divisibleBy6) {
            // Divisible par 6 mais pas 5 → SCI1 Late (6 bytes/entrée)
            if (mapDetected == RES_FORMAT_UNKNOWN || mapDetected == RES_FORMAT_SCI1_LATE) {
                mapDetected = RES_FORMAT_SCI1_LATE;
            }
        } else if (divisibleBy5 && !divisibleBy6) {
            // Divisible par 5 mais pas 6 → SCI1.1 (5 bytes/entrée)
            if (mapDetected == RES_FORMAT_UNKNOWN || mapDetected == RES_FORMAT_SCI11) {
                mapDetected = RES_FORMAT_SCI11;
            }
        }
        // Note: Si divisible par les deux (ex: 30 bytes), on ne peut pas décider avec cette section seule
    }
    
    // Par défaut, assumer SCI1 Late si pas de détection claire
    if (mapDetected == RES_FORMAT_UNKNOWN) {
        mapDetected = RES_FORMAT_SCI1_LATE;
    }
    
    bool isSCI11 = (mapDetected == RES_FORMAT_SCI11);
    size_t entrySize = isSCI11 ? 5 : 6;
    
    std::cout << "  Format détecté: " << (isSCI11 ? "SCI1.1 (5 bytes/entrée)" : "SCI1 Late (6 bytes/entrée)") << std::endl;
    
    for (const auto& [typeRaw, offsetSize] : typeTable) {
        uint16_t offset = offsetSize.first;
        uint16_t sizeBytes = offsetSize.second;
        
        if (offset >= m_resMapData.size()) continue;
        
        // Convertir type index (0x00-0x1F) vers ResourceType (0x80-0x95)
        if (typeRaw > 0x15) continue;  // Type invalide
        uint8_t type = typeConversionTable[typeRaw];
        if (type == 0xFF) continue;  // Type non utilisé
        
        ResourceType resType = static_cast<ResourceType>(type);
        
        // Calculer nombre d'entrées
        size_t numEntries = sizeBytes / entrySize;
        
        for (size_t i = 0; i < numEntries; i++) {
            size_t entryPos = offset + i * entrySize;
            
            if (entryPos + entrySize > m_resMapData.size()) break;
            
            // Lire number (2 bytes LE)
            uint16_t number = readLE16(&m_resMapData[entryPos]);
            
            // Lire offset
            uint32_t resOffset;
            if (isSCI11) {
                // SCI1.1/SCI2: 3 bytes (low word[2B] + high byte[1B]), peut avoir shift
                resOffset = m_resMapData[entryPos + 2] | 
                           (m_resMapData[entryPos + 3] << 8) | 
                           (m_resMapData[entryPos + 4] << 16);
                // Note: SCI1.1 utilise shift, SCI2 non (à implémenter si nécessaire)
            } else {
                // SCI0/SCI1: 4 bytes LE, pas de shift
                resOffset = readLE32(&m_resMapData[entryPos + 2]);
            }
            
            // Indexer la ressource
            auto key = std::make_pair(resType, static_cast<uint32_t>(number));
            m_resourceIndex[key] = resOffset;
            m_resourceVolumes[key] = m_currentVolume;
            
            typeCounts[type]++;
        }
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
    
    // Vérifier que l'offset est valide pour un header minimum
    if (info.offset + 13 > volumeData.size()) {  // SCI2.1 nécessite 13 octets
        std::cerr << "Offset invalide: " << info.offset << std::endl;
        return info;
    }
    
    const uint8_t* headerPtr = volumeData.data() + info.offset;
    size_t headerSize = 0;
    
    // Lire octet 0 et octets 1-2
    uint8_t resType = headerPtr[0];
    uint16_t resNumber = readLE16(headerPtr + 1);
    
    // Détecter le format RESSCI : SCI1.1 (6-10 bytes) ou SCI2.1 (13 bytes)
    // Phantasmagoria (SCI2.1) utilise RESMAP 6 bytes + RESSCI 13 bytes
    // Heuristique : Forcer SCI2.1 si le volume est > 50 Mo (CD-ROM game)
    bool forceSCI21 = (volumeData.size() > 50000000);
    
    // Lire les valeurs comme SCI2.1
    uint32_t compSize4 = readLE32(headerPtr + 3);
    uint32_t decompSize4 = readLE32(headerPtr + 7);
    uint16_t method16 = readLE16(headerPtr + 11);
    
    // SCI2.1 si forcé OU si les valeurs semblent valides
    bool validSizes = (compSize4 > 0 && compSize4 < volumeData.size() &&
                       decompSize4 > 0 && decompSize4 < volumeData.size() * 10);
    bool validMethod = (method16 == 0 || method16 == 1 || method16 == 2 || method16 == 3 ||
                        method16 == 4 || method16 == 18 || method16 == 19 || method16 == 20 ||
                        method16 == 32 || method16 < 256);
    
    bool isSCI21 = forceSCI21 || (validSizes && validMethod);
    
    if (isSCI21) {
        // FORMAT SCI2.1 : 13 bytes
        // 0: Type (1B)
        // 1-2: Number (2B LE)
        // 3-6: CompSize (4B LE)
        // 7-10: DecompSize (4B LE)
        // 11-12: Method (2B LE)
        headerSize = 13;
        info.compressedSize = compSize4;
        info.decompressedSize = decompSize4;
        info.method = static_cast<CompressionMethod>(method16);
        
        // DEBUG
        if (type == RT_SCRIPT || type == RT_HEAP) {
            std::cout << "  " << getResourceTypeName(type) << " #" << number 
                      << " (SCI2.1): method=0x" << std::hex << method16 << std::dec
                      << ", compSize=" << info.compressedSize 
                      << ", decompSize=" << info.decompressedSize << std::endl;
        }
    } else {
        // FORMAT SCI1.1 : 6-10 bytes (variable)
        // Octet 0: type + flag compression (bit 7)
        bool hasCompression = (resType & 0x80) != 0;
        resType = resType & 0x7F;
        
        // Octets 3-5: taille compressée (LE24)
        uint32_t size3bytes = headerPtr[3] | (headerPtr[4] << 8) | (headerPtr[5] << 16);
        info.compressedSize = size3bytes;
        info.decompressedSize = size3bytes;
        info.method = CM_NONE;
        headerSize = 6;
        
        // Si compression, lire méthode (1B) + decompSize (3B)
        if (hasCompression && info.offset + 10 <= volumeData.size()) {
            info.method = static_cast<CompressionMethod>(headerPtr[6]);
            info.decompressedSize = headerPtr[7] | (headerPtr[8] << 8) | (headerPtr[9] << 16);
            headerSize = 10;
        }
        
        // DEBUG
        if (type == RT_SCRIPT || type == RT_HEAP) {
            std::cout << "  " << getResourceTypeName(type) << " #" << number 
                      << " (SCI1.1): method=0x" << std::hex << (int)info.method << std::dec
                      << ", compSize=" << info.compressedSize 
                      << ", decompSize=" << info.decompressedSize << std::endl;
        }
    }
    
    // Lire les données compressées
    size_t dataOffset = info.offset + headerSize;
    size_t dataSize = info.compressedSize;
    
    // Validation stricte
    if (dataOffset >= volumeData.size() || dataSize == 0 || dataSize > volumeData.size() || 
        dataOffset + dataSize > volumeData.size()) {
        // Ne logger que si c'est proche d'être valide
        if (dataOffset < volumeData.size() && dataSize > 0 && dataSize < volumeData.size() * 2) {
            std::cerr << "Données invalides ignorées: offset=" << dataOffset 
                      << " size=" << dataSize << " total=" << volumeData.size() << std::endl;
        }
        return info;
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
            
        case CM_LZS:         // 0x20 - LZS pour Phantasmagoria scripts
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
            // Logger seulement une fois par méthode inconnue pour éviter le spam
            if (s_unsupportedMethodsLogged.find((uint8_t)method) == s_unsupportedMethodsLogged.end()) {
                std::cerr << "Méthode de compression non supportée: 0x" 
                         << std::hex << (int)method << std::dec << std::endl;
                s_unsupportedMethodsLogged.insert((uint8_t)method);
            }
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
    
    std::cout << "Analyse de " << scripts.size() << " scripts pour CALLK Robot (opcode 0x76)...\n";
    
    int scriptsWithRobot = 0;
    for (const auto& script : scripts) {
        auto coords = parseScriptForRobotCalls(script.data, script.number);
        if (!coords.empty()) {
            scriptsWithRobot++;
            std::cout << "  Script #" << script.number << ": " 
                     << coords.size() << " appel(s) kRobot trouvé(s)\n";
            for (const auto& c : coords) {
                std::cout << "    → Robot #" << c.robotId 
                         << " @ (" << c.x << ", " << c.y << ")"
                         << " [Pri:" << c.priority << "]\n";
            }
        }
        allCoords.insert(allCoords.end(), coords.begin(), coords.end());
    }
    
    std::cout << "\n✅ " << scriptsWithRobot << " script(s) avec appels kRobot\n";
    std::cout << "📊 Total: " << allCoords.size() << " coordonnée(s) Robot extraite(s)\n";
    
    return allCoords;
}

bool RESSCIParser::exportResourcesList(const std::string& outputPath) {
    std::ofstream file(outputPath);
    if (!file) {
        std::cerr << "Erreur: impossible de créer " << outputPath << std::endl;
        return false;
    }
    
    // En-tête
    file << "=================================================================\n";
    file << "LISTE DES RESSOURCES SIERRA SCI - RESMAP/RESSCI\n";
    file << "=================================================================\n";
    file << "Total ressources indexées: " << m_resourceIndex.size() << "\n";
    file << "Volumes RESSCI chargés: " << m_ressciData.size() << "\n";
    file << "=================================================================\n\n";
    
    // Compter par type
    std::map<ResourceType, int> countByType;
    for (const auto& entry : m_resourceIndex) {
        countByType[entry.first.first]++;
    }
    
    // Résumé par type
    file << "RÉSUMÉ PAR TYPE DE RESSOURCE:\n";
    file << "-----------------------------------------------------------------\n";
    for (const auto& count : countByType) {
        file << getResourceTypeName(count.first) 
             << " (0x" << std::hex << (int)count.first << std::dec << "): "
             << count.second << " ressource(s)\n";
    }
    file << "\n=================================================================\n\n";
    
    // Liste détaillée par type
    ResourceType currentType = RT_INVALID;
    for (const auto& entry : m_resourceIndex) {
        ResourceType type = entry.first.first;
        uint32_t number = entry.first.second;
        uint32_t offset = entry.second;
        
        // Nouvelle section de type
        if (type != currentType) {
            currentType = type;
            file << "\n-----------------------------------------------------------------\n";
            file << getResourceTypeName(type) << " (0x" << std::hex << (int)type << std::dec << ")\n";
            file << "-----------------------------------------------------------------\n";
        }
        
        // Trouver le volume
        uint8_t volume = 0;
        auto volIt = m_resourceVolumes.find(entry.first);
        if (volIt != m_resourceVolumes.end()) {
            volume = volIt->second;
        }
        
        file << "  " << number 
             << " -> Offset: " << offset 
             << " (0x" << std::hex << offset << std::dec << ")";
        
        if (volume > 0) {
            file << ", Volume: " << (int)volume;
        }
        
        file << "\n";
    }
    
    file << "\n=================================================================\n";
    file << "FIN DE LA LISTE\n";
    file << "=================================================================\n";
    
    std::cout << "Liste des ressources exportée: " << outputPath << std::endl;
    return true;
}

std::vector<RobotCoordinates> RESSCIParser::parseScriptForRobotCalls(
    const std::vector<uint8_t>& scriptData,
    uint32_t scriptId)
{
    std::vector<RobotCoordinates> coords;
    
    if (scriptData.size() < 20) return coords;
    
    // Opcodes SCI32 (format réel selon ScummVM)
    const uint8_t OP_PUSHI = 0x38;      // Push immediate 16-bit value
    const uint8_t OP_CALLK = 0x76;      // Call kernel (format: 0x76 kernelId argc)
    
    // Scanner le bytecode pour CALLK avec différents kernel IDs possibles pour Robot
    // Kernel ID peut varier selon version SCI, on cherche patterns connus
    for (size_t i = 0; i < scriptData.size() - 15; i++) {
        // Chercher CALLK (0x76)
        if (scriptData[i] != OP_CALLK) continue;
        
        // Vérifier qu'on a assez de bytes pour lire kernelId + argc
        if (i + 3 >= scriptData.size()) continue;
        
        // Format: 0x76 <kernelId> <argc>
        uint8_t kernelId = scriptData[i + 1];
        uint8_t argc = scriptData[i + 2];
        
        // Kernel IDs Robot observés dans Phantasmagoria: 57, 67, 74, 84
        // Filtrer pour réduire les faux positifs
        const std::set<uint8_t> robotKernelIds = {57, 67, 74, 84};
        if (robotKernelIds.find(kernelId) == robotKernelIds.end()) continue;
        
        // On cherche argc=4 à 6 (robotId, x, y, priority, [palette], [extra])
        if (argc < 4 || argc > 6) continue;
        
        // Chercher les PUSHI en arrière (arguments empilés avant CALLK)
        std::vector<int16_t> params;
        
        // Scanner jusqu'à 50 bytes en arrière
        for (int back = std::min<int>(i, 50); back > 0; back--) {
            size_t pos = i - back;
            
            if (scriptData[pos] == OP_PUSHI && pos + 3 <= scriptData.size()) {
                int16_t value = (int16_t)readLE16(&scriptData[pos + 1]);
                params.push_back(value);
                
                // On a assez de params
                if (params.size() >= argc) break;
            }
        }
        
        // Vérifier qu'on a au moins 3 params (robotId, x, y)
        if (params.size() < 3) continue;
        
        // Les params sont empilés dans l'ordre: robotId, x, y, priority...
        // Donc le premier PUSHI trouvé en arrière est le dernier argument
        // Il faut inverser pour avoir l'ordre correct
        std::reverse(params.begin(), params.end());
        
        // Debug: afficher tous les params pour comprendre l'ordre
        if (scriptId == 90 || scriptId == 91 || scriptId == 161 || scriptId == 162 || scriptId == 170 || scriptId == 260 ||
            scriptId == 1800 || scriptId == 22100) {
            std::cout << "      DEBUG params (reversed): ";
            for (size_t p = 0; p < params.size(); p++) {
                std::cout << params[p];
                if (p < params.size() - 1) std::cout << ", ";
            }
            std::cout << std::endl;
        }
        
        // Ordre des paramètres kRobotOpen: robotId, plane, priority, x, y, scale
        // params[0] = robotId
        // params[1] = plane (objet, pas une coordonnée)
        // params[2] = priority
        // params[3] = x ← coordonnée X
        // params[4] = y ← coordonnée Y
        // params[5] = scale (optionnel)
        
        if (params.size() < 5) continue;  // Besoin d'au moins robotId, plane, priority, x, y
        
        int16_t robotId = params[0];
        int16_t x = params[3];  // Position X (4ème paramètre)
        int16_t y = params[4];  // Position Y (5ème paramètre)
        int16_t priority = params[2];
        int16_t scale = (params.size() >= 6) ? params[5] : 128;
        
        // Canvas Phantasmagoria: 640×480 pixels (mais peut être 630×450 en pratique)
        // Accepter coordonnées négatives (robots hors écran)
        // Filtrer uniquement les valeurs plausibles
        if (robotId > 0 && robotId < 10000 &&
            x >= -100 && x <= 740 && y >= -100 && y <= 580) {
            
            RobotCoordinates rc;
            rc.robotId = robotId;
            rc.x = x;
            rc.y = y;
            rc.priority = priority;
            rc.scale = scale;
            rc.scriptId = scriptId;
            
            coords.push_back(rc);
            
            // Debug: afficher ce qu'on a trouvé
            std::cout << "    [Script " << scriptId << " offset 0x" << std::hex << i << std::dec 
                      << "] CALLK Robot argc=" << (int)argc
                      << " → Robot #" << robotId << " @ (" << x << ", " << y << ")"
                      << " priority=" << priority << " scale=" << scale << std::endl;
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
        case RT_CHUNK: return "Chunk";
        case RT_HEAP: return "Heap";
        case RT_AUDIO36: return "Audio36";
        case RT_SYNC36: return "Sync36";
        case RT_ROBOTDATA: return "RobotData";
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
        case CM_LZS: return "LZS/STACpack";
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
