/**
 * @file rbt_coordinates_parser.cpp
 * @brief Parser pour extraire les coordonnées X/Y depuis les fichiers RBT
 * 
 * Format Robot Animation v5 (Phantasmagoria SCI2.1):
 *   - Signature: 16 00 "SOL" 00
 *   - Header global (60 bytes)
 *   - Coordonnées X/Y PAR FRAME dans header vidéo (offsets 0x0C-0x0F)
 * 
 * Compile: g++ -std=c++17 rbt_coordinates_parser.cpp -o rbt_parser
 * Usage: ./rbt_parser <fichier.RBT>
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <iomanip>
#include <string>
#include <algorithm>
#include <filesystem>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;

struct FrameCoords {
    uint16_t frameIndex;
    uint16_t x, y;
    uint16_t width, height;
    uint8_t scaling;
};

struct RBTInfo {
    std::string filename;
    uint16_t robotId;
    uint16_t frameCount;
    uint16_t resolutionX, resolutionY;
    uint16_t framerate;
    bool hasAudio;
    bool hasPalette;
    std::vector<FrameCoords> frames;
};

inline uint16_t readLE16(const std::vector<u8>& data, size_t pos) {
    return data[pos] | (data[pos + 1] << 8);
}

inline uint32_t readLE32(const std::vector<u8>& data, size_t pos) {
    return data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16) | (data[pos + 3] << 24);
}

RBTInfo parseRBTFile(const std::string& rbtPath) {
    RBTInfo info;
    info.filename = std::filesystem::path(rbtPath).filename().string();
    
    // Extraire l'ID du nom de fichier (ex: "90.RBT" -> 90)
    std::string idStr = info.filename.substr(0, info.filename.find('.'));
    info.robotId = static_cast<uint16_t>(std::stoi(idStr));
    
    std::ifstream f(rbtPath, std::ios::binary);
    if (!f) {
        std::cerr << "❌ Erreur: fichier non trouvé: " << rbtPath << std::endl;
        return info;
    }

    std::vector<u8> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();

    // Vérifier signature SOL
    if (data.size() < 60) {
        std::cerr << "❌ Fichier trop petit: " << data.size() << " bytes\n";
        return info;
    }
    
    if (data[0] != 0x16 || data[2] != 'S' || data[3] != 'O' || data[4] != 'L') {
        std::cerr << "❌ Signature invalide (attendu: 16 00 'SOL')\n";
        std::cerr << "   Trouvé: " << std::hex << std::setw(2) << std::setfill('0')
                 << (int)data[0] << " " << (int)data[1] << " "
                 << (char)data[2] << (char)data[3] << (char)data[4] << std::dec << "\n";
        return info;
    }

    // Parser header global
    uint16_t version = readLE16(data, 0x06);
    info.frameCount = readLE16(data, 0x0E);
    uint16_t paletteChunkSize = readLE16(data, 0x10);
    info.resolutionX = readLE16(data, 0x14);
    info.resolutionY = readLE16(data, 0x16);
    info.hasPalette = (data[0x18] == 1);
    info.hasAudio = (data[0x19] == 1);
    info.framerate = readLE16(data, 0x1C);

    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ANALYSE FICHIER RBT: " << std::setw(35) << std::left << info.filename << " ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";
    
    std::cout << "📊 Informations Générales:\n";
    std::cout << "  Robot ID:       " << info.robotId << "\n";
    std::cout << "  Version:        " << version << "\n";
    std::cout << "  Frames:         " << info.frameCount << "\n";
    std::cout << "  Résolution:     " << info.resolutionX << "×" << info.resolutionY << "\n";
    std::cout << "  Framerate:      " << info.framerate << " FPS\n";
    std::cout << "  Audio:          " << (info.hasAudio ? "Oui" : "Non") << "\n";
    std::cout << "  Palette:        " << (info.hasPalette ? "Oui" : "Non") << "\n";

    // Calculer position des frames
    // Header (60B) + Palette chunk + Tables de tailles (2 tables × frameCount × 2B) + Unknown (1536B) + Padding (2048B align)
    size_t pos = 60 + paletteChunkSize + (info.frameCount * 4) + 1536;
    
    // Alignement sur 2048 bytes (secteur CD)
    if (pos % 0x800 != 0) {
        pos += (0x800 - (pos % 0x800));
    }

    std::cout << "\n📍 Coordonnées par Frame:\n";
    std::cout << "  (Header vidéo commence @ 0x" << std::hex << pos << std::dec << ")\n\n";

    // Parser chaque frame
    for (uint16_t i = 0; i < info.frameCount && pos + 24 <= data.size(); ++i) {
        FrameCoords frame;
        frame.frameIndex = i;
        frame.scaling = data[pos + 3];
        frame.width = readLE16(data, pos + 4);
        frame.height = readLE16(data, pos + 6);
        frame.x = readLE16(data, pos + 0x0C);
        frame.y = readLE16(data, pos + 0x0E);
        
        info.frames.push_back(frame);

        std::cout << "  Frame " << std::setw(3) << i 
                 << ": X=" << std::setw(4) << frame.x 
                 << ", Y=" << std::setw(4) << frame.y
                 << "  [" << frame.width << "×" << frame.height 
                 << ", scale=" << (int)frame.scaling << "%]\n";

        // Calculer taille pour sauter à la frame suivante
        uint16_t videoCompSize = readLE16(data, pos + 0x10);
        uint16_t numFragments = readLE16(data, pos + 0x12);
        
        // Sauter header vidéo (24B)
        pos += 24;
        
        // Sauter fragments vidéo
        for (uint16_t j = 0; j < numFragments && pos + 10 <= data.size(); ++j) {
            uint32_t fragSize = readLE32(data, pos);
            pos += 10 + fragSize;  // Header fragment (10B) + data
        }
        
        // Sauter audio si présent
        if (info.hasAudio && pos + 8 <= data.size()) {
            uint32_t audioSize = readLE32(data, pos + 4);
            pos += 8 + audioSize;
        }
    }

    return info;
}

int main(int argc, char** argv) {
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  EXTRACTEUR COORDONNÉES RBT - PHANTASMAGORIA                ║\n";
    std::cout << "║  Parser Robot Animation v5 (SCI2.1)                         ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";

    if (argc < 2) {
        std::cout << "\nUsage: " << argv[0] << " <fichier.RBT> [fichier2.RBT ...]\n";
        std::cout << "   ou: " << argv[0] << " <répertoire_RBT>\n\n";
        std::cout << "Exemples:\n";
        std::cout << "  " << argv[0] << " RBT/90.RBT\n";
        std::cout << "  " << argv[0] << " RBT/\n";
        std::cout << "  " << argv[0] << " RBT/*.RBT\n\n";
        return 1;
    }

    std::vector<std::string> rbtFiles;
    
    // Si c'est un répertoire, lister tous les .RBT
    if (std::filesystem::is_directory(argv[1])) {
        for (const auto& entry : std::filesystem::directory_iterator(argv[1])) {
            if (entry.path().extension() == ".RBT" || entry.path().extension() == ".rbt") {
                rbtFiles.push_back(entry.path().string());
            }
        }
        std::sort(rbtFiles.begin(), rbtFiles.end());
    } else {
        // Sinon, utiliser les fichiers fournis
        for (int i = 1; i < argc; ++i) {
            rbtFiles.push_back(argv[i]);
        }
    }

    if (rbtFiles.empty()) {
        std::cerr << "❌ Aucun fichier RBT trouvé.\n";
        return 1;
    }

    std::cout << "\n📂 " << rbtFiles.size() << " fichier(s) RBT à analyser\n";

    // Parser tous les fichiers
    std::vector<RBTInfo> allInfos;
    for (const auto& rbtPath : rbtFiles) {
        auto info = parseRBTFile(rbtPath);
        if (!info.frames.empty()) {
            allInfos.push_back(info);
        }
    }

    // Générer fichier de sortie JSON
    std::ofstream json("robot_coordinates_from_rbt.json");
    json << "{\n";
    json << "  \"format\": \"Phantasmagoria Robot Animation v5\",\n";
    json << "  \"extraction_date\": \"" << __DATE__ << " " << __TIME__ << "\",\n";
    json << "  \"robots\": [\n";
    
    for (size_t r = 0; r < allInfos.size(); ++r) {
        const auto& info = allInfos[r];
        json << "    {\n";
        json << "      \"id\": " << info.robotId << ",\n";
        json << "      \"filename\": \"" << info.filename << "\",\n";
        json << "      \"frame_count\": " << info.frameCount << ",\n";
        json << "      \"resolution\": [" << info.resolutionX << ", " << info.resolutionY << "],\n";
        json << "      \"framerate\": " << info.framerate << ",\n";
        json << "      \"frames\": [\n";
        
        for (size_t f = 0; f < info.frames.size(); ++f) {
            const auto& frame = info.frames[f];
            json << "        {\"index\": " << frame.frameIndex 
                 << ", \"x\": " << frame.x 
                 << ", \"y\": " << frame.y 
                 << ", \"width\": " << frame.width 
                 << ", \"height\": " << frame.height 
                 << ", \"scaling\": " << (int)frame.scaling << "}";
            if (f < info.frames.size() - 1) json << ",";
            json << "\n";
        }
        
        json << "      ]\n";
        json << "    }";
        if (r < allInfos.size() - 1) json << ",";
        json << "\n";
    }
    
    json << "  ]\n";
    json << "}\n";
    json.close();

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ✅ EXTRACTION TERMINÉE                                      ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    std::cout << "📄 Résultats sauvegardés dans: robot_coordinates_from_rbt.json\n";
    std::cout << "📊 " << allInfos.size() << " Robot(s) analysé(s)\n\n";

    return 0;
}
