/**
 * @file rbt_parser_with_lzs.cpp
 * @brief Extracteur de coordonnées X/Y depuis fichiers RBT Phantasmagoria
 * 
 * DÉCOUVERTE CLEF: Les frames RBT sont COMPRESSÉES (LZS/STACpack)
 * Structure:
 *   1. Header global (60B) avec metadata
 *   2. Chunk palette + tables tailles + padding 0x800
 *   3. Frames compressées (taille variable)
 *   4. Après décompression: header frame avec X/Y aux offsets 8-11
 * 
 * Compile: g++ -std=c++17 -I include src/rbt_parser_with_lzs.cpp src/formats/lzs.cpp -o build/rbt_parser_lzs
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <iomanip>
#include <string>
#include <algorithm>
#include <filesystem>
#include "formats/lzs.h"

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;

inline uint16_t readLE16(const std::vector<u8>& data, size_t pos) {
    return data[pos] | (data[pos + 1] << 8);
}

inline int16_t readLE16Signed(const std::vector<u8>& data, size_t pos) {
    return static_cast<int16_t>(readLE16(data, pos));
}

inline uint32_t readLE32(const std::vector<u8>& data, size_t pos) {
    return data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16) | (data[pos + 3] << 24);
}

struct FrameInfo {
    uint16_t index;
    int16_t x, y;          // Signés!
    uint16_t width, height;
    bool valid;
};

struct RobotInfo {
    uint16_t robotId;
    std::string filename;
    uint16_t frameCount;
    uint16_t resX, resY;
    uint16_t framerate;
    std::vector<FrameInfo> frames;
};

RobotInfo parseRBT(const std::string& path) {
    RobotInfo info = {};
    info.filename = std::filesystem::path(path).filename().string();
    
    // Extraire ID du nom (ex: "90.RBT" -> 90)
    std::string idStr = info.filename.substr(0, info.filename.find('.'));
    info.robotId = static_cast<uint16_t>(std::stoi(idStr));
    
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "❌ Fichier non trouvé: " << path << "\n";
        return info;
    }
    
    std::vector<u8> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    
    // Vérifier signature SOL
    if (data.size() < 60 || data[0] != 0x16 || data[2] != 'S' || data[3] != 'O' || data[4] != 'L') {
        std::cerr << "❌ Signature SOL invalide\n";
        return info;
    }
    
    // Parser header global
    u16 version = readLE16(data, 0x06);
    info.frameCount = readLE16(data, 0x0E);
    u16 paletteSize = readLE16(data, 0x10);
    info.framerate = readLE16(data, 0x1C);
    
    std::cout << "\n╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║  RBT: " << std::setw(44) << std::left << info.filename << " ║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n";
    std::cout << "  Robot ID:    " << info.robotId << "\n";
    std::cout << "  Frames:      " << info.frameCount << "\n";
    std::cout << "  Framerate:   " << info.framerate << " FPS\n";
    std::cout << "  Version:     " << version << "\n\n";
    
    // Position des tables de tailles
    size_t tablePos = 60 + paletteSize;
    size_t table2Pos = tablePos + info.frameCount * 2;
    
    // Position des données de frames (aligné sur 0x800)
    size_t dataPos = 60 + paletteSize + (info.frameCount * 4) + 1536;
    if (dataPos % 0x800 != 0) {
        dataPos += (0x800 - (dataPos % 0x800));
    }
    
    std::cout << "📍 Extraction des coordonnées (frames compressées LZS):\n";
    std::cout << "   Data start: 0x" << std::hex << dataPos << std::dec << "\n\n";
    
    // Parser chaque frame
    size_t currentPos = dataPos;
    uint16_t validFrames = 0;
    
    for (u16 i = 0; i < info.frameCount && currentPos < data.size(); ++i) {
        // Lire taille de la frame (table 2: taille totale)
        u16 frameSize = readLE16(data, table2Pos + i * 2);
        
        if (currentPos + frameSize > data.size()) {
            std::cerr << "⚠️  Frame " << i << ": taille " << frameSize << " dépasse fichier\n";
            break;
        }
        
        // Extraire données compressées
        std::vector<u8> compressed(data.begin() + currentPos, 
                                   data.begin() + currentPos + frameSize);
        
        // Décompresser avec LZS (taille décompressée estimée: 640×480 max = 307200)
        std::vector<u8> decompressed(320000);  // Buffer large
        int decompSize = LZSDecompress(compressed.data(), compressed.size(),
                                      decompressed.data(), decompressed.size());
        
        FrameInfo frame = {i, 0, 0, 0, 0, false};
        
        if (decompSize > 16) {
            // Parser header décompressé
            // Offset 8-9:   X (int16 LE signé)
            // Offset 10-11: Y (int16 LE signé)
            // Offset 12-13: Width (uint16)
            // Offset 14-15: Height (uint16)
            
            frame.x = readLE16Signed(decompressed, 8);
            frame.y = readLE16Signed(decompressed, 10);
            frame.width = readLE16(decompressed, 12);
            frame.height = readLE16(decompressed, 14);
            
            // Validation: coordonnées raisonnables pour 640×480
            if (frame.x >= -100 && frame.x <= 700 &&
                frame.y >= -100 && frame.y <= 550 &&
                frame.width > 0 && frame.width <= 640 &&
                frame.height > 0 && frame.height <= 480) {
                frame.valid = true;
                validFrames++;
                
                if (validFrames <= 10 || (i % 10 == 0)) {  // Afficher 10 premières + chaque 10ème
                    std::cout << "  Frame " << std::setw(3) << i 
                             << ": X=" << std::setw(4) << frame.x
                             << ", Y=" << std::setw(4) << frame.y
                             << "  [" << frame.width << "×" << frame.height << "]"
                             << " (" << frameSize << "B comp, " << decompSize << "B decomp)\n";
                }
            } else {
                std::cerr << "⚠️  Frame " << i << ": coordonnées invalides "
                         << "(X=" << frame.x << ", Y=" << frame.y 
                         << ", W=" << frame.width << ", H=" << frame.height << ")\n";
            }
        } else {
            std::cerr << "⚠️  Frame " << i << ": échec décompression (taille=" << decompSize << ")\n";
        }
        
        info.frames.push_back(frame);
        currentPos += frameSize;
    }
    
    std::cout << "\n  ✅ " << validFrames << "/" << info.frameCount << " frames valides\n";
    
    return info;
}

int main(int argc, char** argv) {
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  EXTRACTEUR COORDONNÉES RBT - PHANTASMAGORIA            ║\n";
    std::cout << "║  Avec Décompression LZS/STACpack                        ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    
    if (argc < 2) {
        std::cout << "\nUsage: " << argv[0] << " <fichier.RBT> [fichier2.RBT ...]\n";
        std::cout << "   ou: " << argv[0] << " <répertoire_RBT>\n\n";
        std::cout << "Exemples:\n";
        std::cout << "  " << argv[0] << " RBT/90.RBT\n";
        std::cout << "  " << argv[0] << " RBT/\n\n";
        return 1;
    }
    
    std::vector<std::string> rbtFiles;
    
    // Si répertoire, lister tous les .RBT
    if (std::filesystem::is_directory(argv[1])) {
        for (const auto& entry : std::filesystem::directory_iterator(argv[1])) {
            if (entry.path().extension() == ".RBT" || entry.path().extension() == ".rbt") {
                rbtFiles.push_back(entry.path().string());
            }
        }
        std::sort(rbtFiles.begin(), rbtFiles.end());
    } else {
        for (int i = 1; i < argc; ++i) {
            rbtFiles.push_back(argv[i]);
        }
    }
    
    if (rbtFiles.empty()) {
        std::cerr << "❌ Aucun fichier RBT trouvé\n";
        return 1;
    }
    
    std::cout << "\n📂 " << rbtFiles.size() << " fichier(s) à analyser\n";
    
    // Parser tous les fichiers
    std::vector<RobotInfo> allRobots;
    for (const auto& path : rbtFiles) {
        auto info = parseRBT(path);
        if (!info.frames.empty()) {
            allRobots.push_back(info);
        }
    }
    
    // Générer fichier de sortie JSON
    std::ofstream json("robot_coordinates.json");
    json << "{\n";
    json << "  \"format\": \"Phantasmagoria Robot v5 (LZS decompressed)\",\n";
    json << "  \"total_robots\": " << allRobots.size() << ",\n";
    json << "  \"robots\": [\n";
    
    for (size_t r = 0; r < allRobots.size(); ++r) {
        const auto& robot = allRobots[r];
        json << "    {\n";
        json << "      \"id\": " << robot.robotId << ",\n";
        json << "      \"filename\": \"" << robot.filename << "\",\n";
        json << "      \"frame_count\": " << robot.frameCount << ",\n";
        json << "      \"framerate\": " << robot.framerate << ",\n";
        json << "      \"frames\": [\n";
        
        for (size_t f = 0; f < robot.frames.size(); ++f) {
            const auto& frame = robot.frames[f];
            if (frame.valid) {
                json << "        {\"index\": " << frame.index
                     << ", \"x\": " << frame.x
                     << ", \"y\": " << frame.y
                     << ", \"width\": " << frame.width
                     << ", \"height\": " << frame.height << "}";
                if (f < robot.frames.size() - 1 && robot.frames[f+1].valid) json << ",";
                json << "\n";
            }
        }
        
        json << "      ]\n";
        json << "    }";
        if (r < allRobots.size() - 1) json << ",";
        json << "\n";
    }
    
    json << "  ]\n";
    json << "}\n";
    json.close();
    
    // Générer fichier texte simple (compatible export_robot_mkv)
    std::ofstream txt("robot_positions.txt");
    txt << "# Coordonnées X/Y des Robots Phantasmagoria\n";
    txt << "# Format: RobotID X Y\n";
    txt << "# Extrait des fichiers RBT (première frame de chaque Robot)\n\n";
    
    for (const auto& robot : allRobots) {
        if (!robot.frames.empty() && robot.frames[0].valid) {
            txt << std::setw(4) << robot.robotId << " "
                << std::setw(4) << robot.frames[0].x << " "
                << std::setw(4) << robot.frames[0].y << "\n";
        }
    }
    txt.close();
    
    std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ✅ EXTRACTION TERMINÉE                                  ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    std::cout << "📄 Fichiers générés:\n";
    std::cout << "   - robot_coordinates.json (détails toutes frames)\n";
    std::cout << "   - robot_positions.txt (première frame, format simple)\n";
    std::cout << "\n📊 " << allRobots.size() << " Robot(s) analysé(s)\n\n";
    
    return 0;
}
