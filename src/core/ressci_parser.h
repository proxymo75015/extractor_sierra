/**
 * @file ressci_parser.h
 * @brief Parser universel pour fichiers RESSCI/RESMAP de Sierra SCI
 * 
 * Supporte la détection automatique de formats :
 * - Format 6 octets (SCI1.1 / variantes et démos)
 * - Format 9 octets (SCI32 standard - Phantasmagoria CD original)
 * 
 * Basé sur les spécifications ScummVM et SCI Wiki
 * Testé sur Phantasmagoria CD1-7 et variantes
 * 
 * @see docs/RESMAP_FORMAT_DETECTION.md pour la documentation complète
 * @author Extractor Sierra Project
 * @date 2025
 */

#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <memory>

namespace SCI {

/**
 * @brief Types de ressources SCI (0x80-0x91)
 */
enum ResourceType : uint8_t {
    RT_VIEW       = 0x88,  // Sprites/animations (V56)
    RT_PIC        = 0x87,  // Arrière-plans (P56)
    RT_SCRIPT     = 0x80,  // Code bytecode compilé
    RT_TEXT       = 0x83,  // Textes
    RT_SOUND      = 0x81,  // Sons MIDI ou PCM
    RT_MEMORY     = 0x82,  // Sync audio/vidéo
    RT_VOCAB      = 0x86,  // Dictionnaires
    RT_FONT       = 0x84,  // Polices
    RT_CURSOR     = 0x85,  // Curseurs
    RT_PATCH      = 0x89,  // Patches
    RT_BITMAP     = 0x8A,  // Bitmaps
    RT_PALETTE    = 0x8B,  // Palettes VGA 256 couleurs
    RT_CDAUDIO    = 0x8C,  // Audio CD
    RT_AUDIO      = 0x8D,  // Audio général (MIDI/PCM)
    RT_SYNC       = 0x8E,  // Synchronisation
    RT_MESSAGE    = 0x8F,  // Messages/dialogues (aussi appelé Robot dans certaines versions)
    RT_CHUNK      = 0x90,  // Chunks/métadonnées (contient coordonnées Robot X/Y)
    RT_HEAP       = 0x91,  // Données heap pour scripts
    RT_AUDIO36    = 0x92,  // Audio compressé SCI1.1
    RT_SYNC36     = 0x93,  // Sync pour audio36
    RT_ROBOTDATA  = 0x94,  // Données Robot supplémentaires
    RT_AUDIOMAP   = 0x95,  // Index audio
    RT_INVALID    = 0xFF
};

/**
 * @brief Méthodes de compression SCI
 */
enum CompressionMethod : uint8_t {
    CM_NONE       = 0x00,  // Pas de compression
    CM_RLE_SIMPLE = 0x03,  // RLE basique
    CM_RLE_ADV    = 0x04,  // RLE avec offsets
    CM_HUFFMAN    = 0x05,  // Codage Huffman
    CM_LZ_BIT     = 0x06,  // LZ77-like avec bits
    CM_NONE_ALIAS = 0x08,  // Alias pour 0x00
    CM_RLE_HUFF   = 0x0A,  // RLE + Huffman hybride
    CM_LZ_ADV     = 0x0C,  // LZSS-like
    CM_UNKNOWN    = 0x0D,  // Variante LZ non documentée
    CM_STACPACK_OLD = 0x0E,  // STACpack old
    CM_LZS        = 0x20,  // LZS/STACpack pour SCI32 (Phantasmagoria scripts)
    CM_LZSS_31    = 0x31,  // LZSS variant
    CM_RLE_0x34   = 0x34,  // RLE pour images
    CM_HUFFMAN_V56= 0x38,  // Huffman pour V56 (View)
    CM_DPCM_3C    = 0x3C,  // DPCM pour audio
    CM_STACPACK   = 0x7B,  // STACpack (RFC 1974) - SCI32, utilisé par Phantasmagoria
};

/**
 * @brief Entrée RESMAP format 6 octets (SCI1.1 / variantes)
 * Format: type[1B] + number[2B LE] + offset[3B LE]
 * Utilisé par certaines versions/démos de Phantasmagoria
 */
#pragma pack(push, 1)
struct ResMapEntry6 {
    uint8_t  type;            // Type de ressource (0x80-0x91)
    uint16_t resourceNumber;  // Numéro de ressource (2 octets, LE, < 65536)
    uint8_t  offset_bytes[3]; // Offset sur 3 octets (LE, max 16 MB)
    
    uint32_t getOffset() const {
        return offset_bytes[0] | (offset_bytes[1] << 8) | (offset_bytes[2] << 16);
    }
};
#pragma pack(pop)

/**
 * @brief Entrée RESMAP format 9 octets (SCI32 standard - Phantasmagoria CD original)
 * Format: type[1B] + number[4B LE] + offset[4B LE]
 * Les entrées sont triées par type (croissant), puis par numéro (croissant)
 * TESTÉ sur Phantasmagoria CD1-7 : 12847 entrées pour CD1
 */
#pragma pack(push, 1)
struct ResMapEntry9 {
    uint8_t  type;            // Type de ressource (0x80-0x91, 1 octet)
    uint32_t resourceNumber;  // Numéro de ressource (4 octets, little-endian)
    uint32_t offset;          // Offset absolu dans RESSCI.00X (4 octets, LE)
};
#pragma pack(pop)

/**
 * @brief Header de ressource dans RESSCI.00X (14 octets pour SCI32)
 */
#pragma pack(push, 1)
struct ResourceHeader {
    uint8_t  type;            // Type de ressource (0x80-0x91)
    uint32_t number;          // Numéro de ressource (uint32)
    uint32_t compressedSize;  // Taille des données compressées SEULEMENT (après header)
    uint32_t decompressedSize;// Taille décompressée
    uint8_t  method;          // Méthode de compression
    
    // Note: Les données commencent immédiatement après ce header
    // compressedSize = taille exacte des données compressées uniquement
};
#pragma pack(pop)

/**
 * @brief Informations sur une ressource extraite
 */
struct ResourceInfo {
    ResourceType type;
    uint32_t number;          // SCI32 utilise uint32 pour les numéros
    uint32_t offset;          // Offset dans le fichier RESSCI
    uint32_t compressedSize;
    uint32_t decompressedSize;
    CompressionMethod method;
    uint8_t volume;           // Numéro de CD/volume (1-7 pour Phantasmagoria)
    std::vector<uint8_t> data;// Données décompressées
};

/**
 * @brief Coordonnées x,y extraites pour un Robot
 */
struct RobotCoordinates {
    uint32_t robotId;         // SCI32 utilise uint32
    int16_t x;
    int16_t y;
    int16_t priority;
    int16_t scale;
    uint32_t scriptId;        // Script d'où proviennent les coordonnées
};

/**
 * @brief Parser pour fichiers RESSCI/RESMAP
 */
class RESSCIParser {
public:
    /**
     * @brief Format du fichier RESMAP détecté
     */
    enum ResMapFormat {
        RES_FORMAT_UNKNOWN = 0,   // Format non reconnu
        RES_FORMAT_SCI1_LATE = 1, // SCI1 Late (6 bytes: number[2B] + offset[4B])
        RES_FORMAT_SCI11 = 2,     // SCI1.1 (5 bytes: number[2B] + offset[3B])
        FORMAT_6_BYTES = RES_FORMAT_SCI1_LATE,  // Alias pour compatibilité
        FORMAT_9_BYTES = 3,       // SCI32 (9 bytes - non utilisé dans ce code)
        FORMAT_UNKNOWN = RES_FORMAT_UNKNOWN    // Alias
    };
    
    RESSCIParser();
    ~RESSCIParser();
    
    /**
     * @brief Charge un fichier RESMAP.00X
     * @param path Chemin vers RESMAP.00X
     * @param volumeNumber Numéro du volume (1-7) pour association correcte avec RESSCI
     * @return true si succès
     */
    bool loadResMap(const std::string& path, uint8_t volumeNumber = 1);
    
    /**
     * @brief Détecte automatiquement le format du RESMAP chargé
     * @return Format détecté (6 ou 9 octets)
     */
    ResMapFormat detectFormat() const;
    
    /**
     * @brief Charge un fichier RESSCI.00X
     * @param path Chemin vers RESSCI.00X
     * @param volumeNumber Numéro du volume (1-7)
     * @return true si succès
     */
    bool loadRessci(const std::string& path, uint8_t volumeNumber);
    
    /**
     * @brief Extrait une ressource par type et numéro
     * @param type Type de ressource
     * @param number Numéro de ressource
     * @return Informations sur la ressource (data vide si non trouvée)
     */
    ResourceInfo extractResource(ResourceType type, uint32_t number);
    
    /**
     * @brief Extrait toutes les ressources d'un type donné
     * @param type Type de ressource à extraire
     * @return Vecteur de ressources
     */
    std::vector<ResourceInfo> extractAllResourcesOfType(ResourceType type);
    
    /**
     * @brief Extrait les coordonnées Robot depuis les scripts
     * @return Vecteur de coordonnées trouvées
     */
    std::vector<RobotCoordinates> extractRobotCoordinates();
    
    /**
     * @brief Exporte la liste de toutes les ressources indexées dans un fichier texte
     * @param outputPath Chemin du fichier de sortie
     * @return true si succès
     */
    bool exportResourcesList(const std::string& outputPath);
    
    /**
     * @brief Obtient toutes les ressources indexées
     * @return Map des ressources (type, number) -> offset
     */
    const std::map<std::pair<ResourceType, uint32_t>, uint32_t>& getResourceIndex() const {
        return m_resourceIndex;
    }
    
    /**
     * @brief Décompresse des données selon la méthode spécifiée
     * @param compressed Données compressées
     * @param method Méthode de compression
     * @param decompressedSize Taille attendue après décompression
     * @return Données décompressées
     */
    static std::vector<uint8_t> decompress(
        const std::vector<uint8_t>& compressed,
        CompressionMethod method,
        uint32_t decompressedSize
    );
    
    /**
     * @brief Obtient le nom d'un type de ressource
     * @param type Type de ressource
     * @return Nom du type
     */
    static const char* getResourceTypeName(ResourceType type);
    
    /**
     * @brief Obtient le nom d'une méthode de compression
     * @param method Méthode de compression
     * @return Nom de la méthode
     */
    static const char* getCompressionMethodName(CompressionMethod method);

private:
    /**
     * @brief Parse le header RESMAP et construit l'index
     * @return true si succès
     */
    bool parseResMapHeader();
    
    /**
     * @brief Parse un script pour trouver les appels kRobot
     * @param scriptData Données du script décompressé
     * @param scriptId ID du script
     * @return Coordonnées trouvées dans ce script
     */
    std::vector<RobotCoordinates> parseScriptForRobotCalls(
        const std::vector<uint8_t>& scriptData,
        uint32_t scriptId
    );
    
    /**
     * @brief Parse la section HEAP d'un script pour trouver les propriétés d'objets
     * @param heapData Données HEAP décompressées
     * @param scriptId ID du script
     * @return Coordonnées trouvées dans le HEAP
     */
    std::vector<RobotCoordinates> parseHeapForCoordinates(
        const std::vector<uint8_t>& heapData,
        uint32_t scriptId
    );
    
    // Méthodes de décompression spécifiques
    static std::vector<uint8_t> decompressRLE(const std::vector<uint8_t>& data, uint32_t decompSize);
    static std::vector<uint8_t> decompressHuffman(const std::vector<uint8_t>& data, uint32_t decompSize);
    static std::vector<uint8_t> decompressLZ(const std::vector<uint8_t>& data, uint32_t decompSize);
    static std::vector<uint8_t> decompressSTACpack(const std::vector<uint8_t>& data, uint32_t decompSize);
    
private:
    // Données RESMAP chargées
    std::vector<uint8_t> m_resMapData;
    
    // Numéro de volume courant lors du parsing RESMAP
    uint8_t m_currentVolume;
    
    // Format RESMAP détecté (6 ou 9 octets)
    ResMapFormat m_detectedFormat;
    
    // Index: (type, number) -> offset dans RESSCI (SCI32 utilise uint32)
    std::map<std::pair<ResourceType, uint32_t>, uint32_t> m_resourceIndex;
    
    // Données RESSCI chargées par volume
    std::map<uint8_t, std::vector<uint8_t>> m_ressciData;
    
    // Mapping: (type, number) -> volume
    std::map<std::pair<ResourceType, uint32_t>, uint8_t> m_resourceVolumes;
    
    // Suivi des méthodes de compression non supportées (pour éviter le spam)
    static std::set<uint8_t> s_unsupportedMethodsLogged;
};

} // namespace SCI
