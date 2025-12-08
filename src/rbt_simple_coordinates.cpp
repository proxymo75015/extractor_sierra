/**
 * @file rbt_simple_coordinates.cpp
 * @brief Extracteur coordonnées X/Y depuis headers de fragments RBT
 * 
 * RÉVÉLATION FINALE: Les coordonnées sont dans les HEADERS DE FRAGMENTS!
 * Pas besoin de décompression - juste parser les headers (10 bytes chacun)
 * 
 * Structure:
 *   - Header frame (8B): unknown[4] + unknown[2] + fragmentCount[2]
 *   - Pour chaque fragment:
 *       Header (10B): compSize[4] + decompSize[2] + X[2] + Y[2]
 *       Data compressée (compSize bytes)
 * 
 * Compile: g++ -std=c++17 src/rbt_simple_coordinates.cpp -o build/rbt_coords
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

inline u16 readLE16(const std::vector<u8>& data, size_t pos) {
    return data[pos] | (data[pos + 1] << 8);
}

inline int16_t readLE16Signed(const std::vector<u8>& data, size_t pos) {
    return static_cast<int16_t>(readLE16(data, pos));
}

inline u32 readLE32(const std::vector<u8>& data, size_t pos) {
    return data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16) | (data[pos + 3] << 24);
}

struct FragmentCoords {
    int16_t x, y;
    u16 width, height;
};

struct FrameCoords {
    u16 frameIndex;
    std::vector<FragmentCoords> fragments;
    int16_t minX, minY, maxX, maxY;  // Bounding box
};

struct RobotCoords {
    u16 robotId;
    std::string filename;
    u16 totalFrames;
    std::vector<FrameCoords> frames;
};

RobotCoords extractCoordinates(const std::string& path) {
    RobotCoords robot = {};
    robot.filename = std::filesystem::path(path).filename().string();
    
    std::string idStr = robot.filename.substr(0, robot.filename.find('.'));
    robot.robotId = static_cast<u16>(std::stoi(idStr));
    
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "❌ Fichier introuvable: " << path << "\n";
        return robot;
    }
    
    std::vector<u8> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    
    // Vérifier signature
    if (data.size() < 60 || data[0] != 0x16 || data[2] != 'S' || data[3] != 'O' || data[4] != 'L') {
        std::cerr << "❌ Signature SOL invalide\n";
        return robot;
    }
    
    // Parser header
    robot.totalFrames = readLE16(data, 0x0E);
    u16 paletteSize = readLE16(data, 0x10);
    
    std::cout << "\n╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Robot #" << std::setw(4) << robot.robotId << " - " 
              << std::setw(40) << std::left << robot.filename << " ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n";
    std::cout << "  Frames: " << robot.totalFrames << "\n\n";
    
    // Position des tables
    size_t tablePos = 60 + paletteSize;
    size_t table2Pos = tablePos + robot.totalFrames * 2;
    
    // Position données (aligné 0x800)
    size_t dataPos = 60 + paletteSize + (robot.totalFrames * 4) + 1536;
    if (dataPos % 0x800 != 0) {
        dataPos += (0x800 - (dataPos % 0x800));
    }
    
    // Parser chaque frame
    size_t currentPos = dataPos;
    for (u16 frameIdx = 0; frameIdx < robot.totalFrames && currentPos + 8 < data.size(); ++frameIdx) {
        FrameCoords frame = {frameIdx, {}, 32767, 32767, -32767, -32767};
        
        // Lire header frame (8 bytes)
        u16 fragmentCount = readLE16(data, currentPos + 6);
        currentPos += 8;
        
        // Parser chaque fragment
        for (u16 fragIdx = 0; fragIdx < fragmentCount && currentPos + 10 < data.size(); ++fragIdx) {
            FragmentCoords frag = {};
            
            // Header fragment (10 bytes)
            u32 compSize = readLE32(data, currentPos);
            frag.width = readLE16(data, currentPos + 4);
            frag.height = 0;  // Pas dans header, calculé après decomp
            frag.x = readLE16Signed(data, currentPos + 6);
            frag.y = readLE16Signed(data, currentPos + 8);
            
            // Mettre à jour bounding box
            if (frag.x < frame.minX) frame.minX = frag.x;
            if (frag.y < frame.minY) frame.minY = frag.y;
            if (frag.x + frag.width > frame.maxX) frame.maxX = frag.x + frag.width;
            
            frame.fragments.push_back(frag);
            
            // Sauter header + données compressées
            currentPos += 10 + compSize;
        }
        
        robot.frames.push_back(frame);
        
        // Afficher première et chaque 10ème frame
        if (frameIdx < 5 || frameIdx % 10 == 0) {
            std::cout << "  Frame " << std::setw(3) << frameIdx 
                     << ": " << fragmentCount << " fragment(s)";
            
            if (!frame.fragments.empty()) {
                std::cout << " - BBox: X=" << frame.minX << ".." << frame.maxX
                         << ", Y=" << frame.minY;
                
                // Afficher premier fragment
                std::cout << " [Frag0: X=" << frame.fragments[0].x
                         << ", Y=" << frame.fragments[0].y
                         << ", W=" << frame.fragments[0].width << "]";
            }
            std::cout << "\n";
        }
    }
    
    std::cout << "\n  ✅ " << robot.frames.size() << " frames analysées\n";
    
    return robot;
}

int main(int argc, char** argv) {
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  EXTRACTEUR COORDONNÉES RBT - VERSION SIMPLIFIÉE       ║\n";
    std::cout << "║  Parse headers de fragments (sans décompression)       ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    
    if (argc < 2) {
        std::cout << "\nUsage: " << argv[0] << " <fichier.RBT> [fichier2 ...]\n";
        std::cout << "   ou: " << argv[0] << " <répertoire_RBT>\n\n";
        return 1;
    }
    
    std::vector<std::string> rbtFiles;
    
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
        std::cerr << "❌ Aucun fichier RBT\n";
        return 1;
    }
    
    std::cout << "\n📂 " << rbtFiles.size() << " fichier(s)\n";
    
    // Extraire coordonnées
    std::vector<RobotCoords> allRobots;
    for (const auto& path : rbtFiles) {
        auto robot = extractCoordinates(path);
        if (!robot.frames.empty()) {
            allRobots.push_back(robot);
        }
    }
    
    // Générer fichier JSON
    std::ofstream json("robot_coordinates.json");
    json << "{\n";
    json << "  \"format\": \"Phantasmagoria Robot v5 - Fragment coordinates\",\n";
    json << "  \"note\": \"X/Y sont des coordonnées de fragments (cels), pas global\",\n";
    json << "  \"robots\": [\n";
    
    for (size_t r = 0; r < allRobots.size(); ++r) {
        const auto& robot = allRobots[r];
        json << "    {\n";
        json << "      \"id\": " << robot.robotId << ",\n";
        json << "      \"filename\": \"" << robot.filename << "\",\n";
        json << "      \"frame_count\": " << robot.totalFrames << ",\n";
        json << "      \"frames\": [\n";
        
        for (size_t f = 0; f < robot.frames.size(); ++f) {
            const auto& frame = robot.frames[f];
            json << "        {\n";
            json << "          \"index\": " << frame.frameIndex << ",\n";
            json << "          \"bounding_box\": {\"x1\": " << frame.minX 
                 << ", \"y1\": " << frame.minY
                 << ", \"x2\": " << frame.maxX
                 << ", \"y2\": " << frame.maxY << "},\n";
            json << "          \"fragments\": [\n";
            
            for (size_t fg = 0; fg < frame.fragments.size(); ++fg) {
                const auto& frag = frame.fragments[fg];
                json << "            {\"x\": " << frag.x 
                     << ", \"y\": " << frag.y
                     << ", \"width\": " << frag.width << "}";
                if (fg < frame.fragments.size() - 1) json << ",";
                json << "\n";
            }
            
            json << "          ]\n";
            json << "        }";
            if (f < robot.frames.size() - 1) json << ",";
            json << "\n";
        }
        
        json << "      ]\n";
        json << "    }";
        if (r < allRobots.size() - 1) json << ",";
        json << "\n";
    }
    
    json << "  ]\n";
    json << "}\n";
    json.close();
    
    // Fichier texte (première frame, premier fragment)
    std::ofstream txt("robot_positions.txt");
    txt << "# Coordonnées Robot Phantasmagoria\n";
    txt << "# Format: RobotID X Y (premier fragment, première frame)\n\n";
    
    for (const auto& robot : allRobots) {
        if (!robot.frames.empty() && !robot.frames[0].fragments.empty()) {
            const auto& frag = robot.frames[0].fragments[0];
            txt << std::setw(4) << robot.robotId << " "
                << std::setw(4) << frag.x << " "
                << std::setw(4) << frag.y << "\n";
        }
    }
    txt.close();
    
    std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ✅ EXTRACTION TERMINÉE                                  ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    std::cout << "📄 Fichiers générés:\n";
    std::cout << "   - robot_coordinates.json (toutes coords fragments)\n";
    std::cout << "   - robot_positions.txt (première frame/fragment)\n";
    std::cout << "\n📊 " << allRobots.size() << " Robot(s) analysé(s)\n\n";
    
    return 0;
}
