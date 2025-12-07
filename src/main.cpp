// Extracteur Robot unifié - MP4, MKV multicouche, MOV ProRes
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <map>

#include "core/rbt_parser.h"
#include "formats/robot_mkv_exporter.h"

namespace fs = std::filesystem;
using namespace RobotExtractor;

// Fonction pour charger les coordonnées depuis robot_positions_extracted.txt
std::map<uint32_t, std::pair<int16_t, int16_t>> loadCoordinatesFromFile(const std::string& coordsFile) {
    std::map<uint32_t, std::pair<int16_t, int16_t>> coords;
    
    std::ifstream coordsStream(coordsFile);
    if (coordsStream.is_open()) {
        std::string line;
        while (std::getline(coordsStream, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            uint32_t id;
            int16_t x, y;
            if (std::sscanf(line.c_str(), "%u %hd %hd", &id, &x, &y) == 3) {
                coords[id] = {x, y};
            }
        }
        coordsStream.close();
    }
    
    return coords;
}

// Fonction pour scanner les scripts SCI et extraire les coordonnées manquantes
std::map<uint32_t, std::pair<int16_t, int16_t>> scanResourceScripts(const std::string& resourceDir);

// Fonction pour traiter un fichier RBT individuel
bool processRobotFile(const std::string& rbtPath, const std::string& ressciDir, 
                      const std::string& baseOutDir, int maxFramesArg,
                      const std::map<uint32_t, std::pair<int16_t, int16_t>>& allCoords) {
    
    // Extraire le Robot ID depuis le nom du fichier
    std::string filename = fs::path(rbtPath).filename().string();
    std::string robotName;
    uint32_t robotId = 0;
    {
        size_t dotPos = filename.find('.');
        robotName = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
        try {
            robotId = std::stoul(robotName);
        } catch (...) {
            std::fprintf(stderr, "⚠️  Impossible d'extraire Robot ID de '%s', ignoré\n", filename.c_str());
            return false;
        }
    }
    
    std::string outDir = std::string(baseOutDir) + "/" + robotName;
    
    std::fprintf(stderr, "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    std::fprintf(stderr, "🎬 Robot %u (%s)\n", robotId, filename.c_str());
    std::fprintf(stderr, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    // Chercher les coordonnées
    int16_t robotX = 0, robotY = 0;
    bool coordsFound = false;
    
    auto it = allCoords.find(robotId);
    if (it != allCoords.end()) {
        robotX = it->second.first;
        robotY = it->second.second;
        coordsFound = true;
        std::fprintf(stderr, "   🎯 Position: (%d, %d)\n", robotX, robotY);
    } else {
        std::fprintf(stderr, "   ⚠️  Coordonnées non trouvées, utilisation de (0, 0)\n");
    }

    std::FILE *f = std::fopen(rbtPath.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "   ❌ Erreur ouverture: %s\n", rbtPath.c_str());
        return false;
    }

    RbtParser parser(f);
    if (!parser.parseHeader()) {
        std::fprintf(stderr, "   ❌ Erreur parsing header\n");
        std::fclose(f);
        return false;
    }

    std::fprintf(stderr, "   📊 %zu frames, framerate=%d\n", parser.getNumFrames(), parser.getFrameRate());
    
    // Configurer le mode de rendu
    if (coordsFound && (robotX != 0 || robotY != 0)) {
        parser.setCanvasMode(robotX, robotY, 630, 450);
    } else {
        parser.disableCanvasMode();
        parser.computeMaxDimensions();
    }

    // Créer structure de sortie
    std::string cmd = std::string("mkdir -p ") + outDir;
    std::system(cmd.c_str());
    
    std::string framesDir = outDir + "/" + robotName + "_frames";
    cmd = std::string("mkdir -p ") + framesDir;
    std::system(cmd.c_str());

    // Extraction frames
    size_t maxFrames = parser.getNumFrames();
    if (maxFramesArg > 0) {
        maxFrames = (size_t)maxFramesArg;
    }

    std::fprintf(stderr, "   🎞️  Extraction %zu frames...\n", maxFrames);
    for (size_t i = 0; i < maxFrames && i < parser.getNumFrames(); ++i) {
        if (!parser.extractFrame(i, framesDir.c_str())) {
            std::fprintf(stderr, "   ⚠️  Frame %zu échouée\n", i);
        }
    }

    // Extraction audio
    std::string audioWav = outDir + "/" + robotName + "_audio.wav";
    
    if (parser.hasAudio()) {
        std::fprintf(stderr, "   🔊 Extraction audio...\n");
        parser.extractAudio(audioWav, maxFrames);
    }
    
    bool hasAudio = (std::ifstream(audioWav).good());
    int ret = 0;
    
    // ========================================
    // Génération MKV multicouche + MOV ProRes
    // ========================================
    
    std::string mkvPath = outDir + "/" + robotName + "_video";
    std::string movPath = outDir + "/" + robotName + "_composite.mov";
    
    std::fprintf(stderr, "   📦 Génération MKV multicouche + MOV ProRes...\n");
    
    // Extraire toutes les frames et les décomposer en couches
    std::vector<RobotLayerFrame> allLayers;
    allLayers.reserve(maxFrames);
    
    // Récupérer palette globale
    std::vector<uint8_t> palette = parser.getPalette();
    
    // Réouvrir le fichier pour extractFramePixels
    std::FILE *f2 = std::fopen(rbtPath.c_str(), "rb");
    if (!f2) {
        std::fprintf(stderr, "   ⚠️  Impossible de rouvrir %s pour MKV/MOV\n", rbtPath.c_str());
    } else {
        RbtParser parser2(f2);
        if (!parser2.parseHeader()) {
            std::fprintf(stderr, "   ⚠️  Erreur parsing pour MKV/MOV\n");
            std::fclose(f2);
        } else {
            // Configurer le même mode de rendu
            if (coordsFound && (robotX != 0 || robotY != 0)) {
                parser2.setCanvasMode(robotX, robotY, 630, 450);
            } else {
                parser2.disableCanvasMode();
                parser2.computeMaxDimensions();
            }
            
            // Extraire les pixels de chaque frame
            for (size_t frameIdx = 0; frameIdx < maxFrames; ++frameIdx) {
                std::vector<uint8_t> pixelIndices;
                int width = 0, height = 0;
                
                if (!parser2.extractFramePixels(frameIdx, pixelIndices, width, height)) {
                    std::fprintf(stderr, "   ⚠️  Frame %zu extraction échec\n", frameIdx);
                    continue;
                }
                
                // Décomposer en couches
                try {
                    RobotLayerFrame layer = decomposeRobotFrame(pixelIndices, palette, width, height);
                    allLayers.push_back(std::move(layer));
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "   ⚠️  Frame %zu décomposition échec: %s\n", frameIdx, e.what());
                    continue;
                }
            }
            std::fclose(f2);
        }
    }
    
    // Exporter MKV multicouche (4 pistes vidéo + audio)
    if (!allLayers.empty()) {
        MKVExportConfig mkvConfig;
        mkvConfig.framerate = parser.getFrameRate();
        mkvConfig.codec = MKVExportConfig::Codec::H264;
        
        RobotMKVExporter exporter(mkvConfig);
        
        std::string audioForMkv = hasAudio ? audioWav : "";
        
        // Passer les dimensions canvas seulement si coordonnées trouvées et non-nulles
        int canvasW = 0, canvasH = 0;
        if (coordsFound && (robotX != 0 || robotY != 0)) {
            canvasW = 630;
            canvasH = 450;
            std::fprintf(stderr, "   → Mode canvas: %dx%d pour MKV/MOV\n", canvasW, canvasH);
        } else {
            std::fprintf(stderr, "   → Mode crop: dimensions auto pour MKV/MOV\n");
        }
        
        // Génération MKV + MOV via exportMultiTrack()
        if (exporter.exportMultiTrack(allLayers, mkvPath, audioForMkv, canvasW, canvasH)) {
            std::fprintf(stderr, "      • MKV:    %s.mkv (4 pistes)\n", mkvPath.c_str());
            std::fprintf(stderr, "      • MOV:    %s_composite.mov (ProRes 4444 RGBA)\n", mkvPath.c_str());
            
            // Supprimer les anciens frames PPM (remplacés par PNG RGBA)
            std::string cleanupPPM = "rm -f " + framesDir + "/*.ppm 2>/dev/null || true";
            std::system(cleanupPPM.c_str());
        } else {
            std::fprintf(stderr, "   ⚠️  Export MKV/MOV échec\n");
        }
    }
    
    // Écrire métadonnées complètes
    std::string metadataFile = outDir + "/metadata.txt";
    std::FILE* metaFp = std::fopen(metadataFile.c_str(), "w");
    if (metaFp) {
        std::fprintf(metaFp, "Robot ID: %u\n", robotId);
        std::fprintf(metaFp, "Frames: %zu\n", parser.getNumFrames());
        std::fprintf(metaFp, "Frame Rate: %d fps\n", parser.getFrameRate());
        std::fprintf(metaFp, "Has Audio: %s\n", parser.hasAudio() ? "yes" : "no");
        std::fprintf(metaFp, "Position: (%d, %d)\n", robotX, robotY);
        std::fprintf(metaFp, "Coordinates Found: %s\n", coordsFound ? "yes" : "no");
        std::fclose(metaFp);
    }
    
    // Résumé
    std::fprintf(stderr, "   ✅ Extraction réussie\n");
    std::fprintf(stderr, "      • Frames PNG: %s/\n", framesDir.c_str());
    if (hasAudio) std::fprintf(stderr, "      • Audio:  %s\n", audioWav.c_str());

    std::fclose(f);
    return true;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::printf("Usage: %s <rbt_dir> [ressci_dir] [out_dir]\n", argv[0]);
        std::printf("  <rbt_dir>     - Répertoire contenant les fichiers .RBT (ex: RBT/)\n");
        std::printf("  [ressci_dir]  - Répertoire RESSCI pour coordonnées (défaut: Resource/)\n");
        std::printf("  [out_dir]     - Répertoire de sortie (défaut: output/)\n");
        std::printf("\nExtrait automatiquement tous les fichiers Robot du répertoire RBT/\n");
        std::printf("et explore Resource/ pour trouver les coordonnées manquantes.\n");
        return 1;
    }

    const char *rbtDir = argv[1];
    const char *ressciDir = (argc >= 3) ? argv[2] : "Resource";
    const char *baseOutDir = (argc >= 4) ? argv[3] : "output";
    int maxFramesArg = -1;  // Pas de limite
    
    std::fprintf(stderr, "╔════════════════════════════════════════════════════╗\n");
    std::fprintf(stderr, "║  Extracteur Robot - Traitement par lot           ║\n");
    std::fprintf(stderr, "╚════════════════════════════════════════════════════╝\n\n");
    std::fprintf(stderr, "📂 Répertoire RBT:      %s\n", rbtDir);
    std::fprintf(stderr, "📂 Répertoire RESSCI:   %s\n", ressciDir);
    std::fprintf(stderr, "📂 Sortie:              %s\n\n", baseOutDir);
    
    // Étape 1: Charger les coordonnées depuis robot_positions_extracted.txt
    std::fprintf(stderr, "════════════════════════════════════════════════════\n");
    std::fprintf(stderr, "📋 ÉTAPE 1: Chargement coordonnées existantes\n");
    std::fprintf(stderr, "════════════════════════════════════════════════════\n");
    
    std::map<uint32_t, std::pair<int16_t, int16_t>> allCoords;
    std::string coordsFilePath = std::string(ressciDir) + "/robot_positions_extracted.txt";
    
    if (fs::exists(coordsFilePath)) {
        allCoords = loadCoordinatesFromFile(coordsFilePath);
        std::fprintf(stderr, "✅ Chargé %zu coordonnées depuis %s\n", allCoords.size(), coordsFilePath.c_str());
    } else {
        std::fprintf(stderr, "⚠️  Fichier %s non trouvé\n", coordsFilePath.c_str());
    }
    
    // Étape 2: Scanner les scripts SCI pour les coordonnées manquantes
    std::fprintf(stderr, "\n════════════════════════════════════════════════════\n");
    std::fprintf(stderr, "🔍 ÉTAPE 2: Scan scripts SCI pour coordonnées manquantes\n");
    std::fprintf(stderr, "════════════════════════════════════════════════════\n");
    
    std::map<uint32_t, std::pair<int16_t, int16_t>> scriptCoords = scanResourceScripts(ressciDir);
    
    // Fusionner les coordonnées (priorité aux existantes)
    size_t addedCount = 0;
    for (const auto& [robotId, coords] : scriptCoords) {
        if (allCoords.find(robotId) == allCoords.end()) {
            allCoords[robotId] = coords;
            addedCount++;
            std::fprintf(stderr, "   + Robot %u: (%d, %d)\n", robotId, coords.first, coords.second);
        }
    }
    
    std::fprintf(stderr, "✅ Ajouté %zu nouvelles coordonnées depuis scripts\n", addedCount);
    std::fprintf(stderr, "📊 Total: %zu Robot avec coordonnées\n", allCoords.size());
    
    // Étape 3: Lister tous les fichiers .RBT
    std::fprintf(stderr, "\n════════════════════════════════════════════════════\n");
    std::fprintf(stderr, "📁 ÉTAPE 3: Recherche fichiers .RBT\n");
    std::fprintf(stderr, "════════════════════════════════════════════════════\n");
    
    std::vector<std::string> rbtFiles;
    
    try {
        for (const auto& entry : fs::directory_iterator(rbtDir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                std::string ext = entry.path().extension().string();
                
                // Convertir extension en majuscules pour comparaison
                std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
                
                if (ext == ".RBT") {
                    rbtFiles.push_back(entry.path().string());
                    std::fprintf(stderr, "   📄 %s\n", filename.c_str());
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::fprintf(stderr, "❌ Erreur lecture répertoire %s: %s\n", rbtDir, e.what());
        return 2;
    }
    
    if (rbtFiles.empty()) {
        std::fprintf(stderr, "❌ Aucun fichier .RBT trouvé dans %s\n", rbtDir);
        return 3;
    }
    
    // Trier les fichiers par numéro de Robot
    std::sort(rbtFiles.begin(), rbtFiles.end());
    
    std::fprintf(stderr, "✅ Trouvé %zu fichiers .RBT\n", rbtFiles.size());
    
    // Étape 4: Traiter chaque fichier Robot
    std::fprintf(stderr, "\n════════════════════════════════════════════════════\n");
    std::fprintf(stderr, "⚙️  ÉTAPE 4: Extraction des Robots\n");
    std::fprintf(stderr, "════════════════════════════════════════════════════\n");
    
    size_t successCount = 0;
    size_t failCount = 0;
    
    for (const auto& rbtPath : rbtFiles) {
        bool success = processRobotFile(rbtPath, ressciDir, baseOutDir, maxFramesArg, allCoords);
        if (success) {
            successCount++;
        } else {
            failCount++;
        }
    }
    
    // Résumé final
    std::fprintf(stderr, "\n╔════════════════════════════════════════════════════╗\n");
    std::fprintf(stderr, "║  RÉSUMÉ FINAL                                     ║\n");
    std::fprintf(stderr, "╚════════════════════════════════════════════════════╝\n");
    std::fprintf(stderr, "✅ Succès:    %zu / %zu\n", successCount, rbtFiles.size());
    std::fprintf(stderr, "❌ Échecs:    %zu / %zu\n", failCount, rbtFiles.size());
    std::fprintf(stderr, "📂 Sortie:    %s/\n", baseOutDir);
    std::fprintf(stderr, "════════════════════════════════════════════════════\n");
    
    return (failCount == 0) ? 0 : 1;
}

// Fonction pour scanner les scripts SCI et extraire coordonnées
std::map<uint32_t, std::pair<int16_t, int16_t>> scanResourceScripts(const std::string& resourceDir) {
    std::map<uint32_t, std::pair<int16_t, int16_t>> coords;
    
    // Chercher extract_coordinates
    std::string extractorPath = "./build/extract_coordinates";
    
    if (!fs::exists(extractorPath)) {
        std::fprintf(stderr, "⚠️  %s non trouvé, coordonnées scripts ignorées\n", extractorPath.c_str());
        return coords;
    }
    
    // Chercher le répertoire contenant RESMAP/RESSCI
    std::string scanDir = resourceDir;
    
    // Si Resource/ n'a pas de RESMAP, chercher dans phantasmagoria_game/
    if (!fs::exists(resourceDir + "/RESMAP.001") && !fs::exists(resourceDir + "/RESMAP.000")) {
        if (fs::exists("phantasmagoria_game/RESMAP.001") || fs::exists("phantasmagoria_game/RESMAP.000")) {
            scanDir = "phantasmagoria_game";
            std::fprintf(stderr, "   📀 Utilisation de %s pour scan scripts\n", scanDir.c_str());
        }
    }
    
    // Vérifier que le répertoire a bien des fichiers RESSCI
    bool hasRessci = fs::exists(scanDir + "/RESSCI.001") || fs::exists(scanDir + "/RESSCI.000");
    if (!hasRessci) {
        std::fprintf(stderr, "   ⚠️  Pas de fichiers RESSCI dans %s\n", scanDir.c_str());
        return coords;
    }
    
    // Créer fichier temporaire pour les résultats
    std::string tempFile = "/tmp/robot_coords_temp.txt";
    std::string cmd = extractorPath + " " + scanDir + " > " + tempFile + " 2>/dev/null";
    
    std::fprintf(stderr, "   🔍 Scan %s...\n", scanDir.c_str());
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::fprintf(stderr, "   ⚠️  Extraction coordonnées scripts échouée (code %d)\n", ret);
        return coords;
    }
    
    // Lire les résultats du format extract_coordinates
    std::ifstream tempStream(tempFile);
    if (tempStream.is_open()) {
        std::string line;
        while (std::getline(tempStream, line)) {
            // Format extract_coordinates: "Script 123: Robot 1000 at (x=315, y=200)"
            // ou format simple: "1000 315 200"
            
            // Essayer parsing format simple
            uint32_t id;
            int16_t x, y;
            if (std::sscanf(line.c_str(), "%u %hd %hd", &id, &x, &y) == 3) {
                coords[id] = {x, y};
                continue;
            }
            
            // Essayer parsing format verbose
            if (line.find("Robot") != std::string::npos && line.find("at (x=") != std::string::npos) {
                size_t robotPos = line.find("Robot ");
                size_t xPos = line.find("x=");
                size_t yPos = line.find("y=");
                
                if (robotPos != std::string::npos && xPos != std::string::npos && yPos != std::string::npos) {
                    uint32_t robotId = std::stoul(line.substr(robotPos + 6));
                    int16_t xCoord = std::stoi(line.substr(xPos + 2));
                    int16_t yCoord = std::stoi(line.substr(yPos + 2));
                    coords[robotId] = {xCoord, yCoord};
                }
            }
        }
        tempStream.close();
    }
    
    // Nettoyer
    std::remove(tempFile.c_str());
    
    return coords;
}
