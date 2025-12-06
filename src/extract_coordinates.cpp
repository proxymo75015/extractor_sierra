/**
 * @file extract_coordinates.cpp
 * @brief Outil d'extraction des coordonnées Robot depuis RESSCI/RESMAP
 * 
 * Utilise le parser RESSCI pour extraire les coordonnées x,y des vidéos Robot
 * depuis les scripts et sections HEAP de Phantasmagoria
 * 
 * Usage: extract_coordinates <répertoire_resource> [robot_id]
 */

#include "core/ressci_parser.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <algorithm>

using namespace SCI;

/**
 * @brief Affiche l'aide
 */
void printUsage(const char* program) {
    std::cout << "Usage: " << program << " <répertoire_resource> [robot_id]\n\n";
    std::cout << "Extrait les coordonnées x,y des vidéos Robot depuis les fichiers RESSCI.\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  répertoire_resource  Chemin vers le répertoire contenant RESMAP/RESSCI\n";
    std::cout << "  robot_id            (optionnel) ID spécifique du Robot à rechercher\n\n";
    std::cout << "Exemples:\n";
    std::cout << "  " << program << " Resource/\n";
    std::cout << "  " << program << " Resource/ 1000\n";
    std::cout << "  " << program << " phantasmagoria_game/ 230\n\n";
    std::cout << "Sortie:\n";
    std::cout << "  Génère robot_positions.txt avec le format:\n";
    std::cout << "  robot_id X Y [script_id] [priority] [scale]\n";
}

/**
 * @brief Charge tous les volumes RESSCI disponibles
 */
int loadAllVolumes(RESSCIParser& parser, const std::string& resourceDir) {
    int volumesLoaded = 0;
    
    // Essayer de charger les volumes 1-7 (Phantasmagoria sur 7 CD)
    for (int vol = 1; vol <= 7; vol++) {
        std::string ressciPath = resourceDir + "/RESSCI.00" + std::to_string(vol);
        
        // Vérifier si le fichier existe
        std::ifstream test(ressciPath);
        if (!test.good()) {
            // Essayer le format RESSCI.001, RESSCI.002, etc.
            ressciPath = resourceDir + "/RESSCI." + 
                        (vol < 10 ? "00" : vol < 100 ? "0" : "") + 
                        std::to_string(vol);
            test.open(ressciPath);
            if (!test.good()) {
                continue;
            }
        }
        test.close();
        
        if (parser.loadRessci(ressciPath, vol)) {
            volumesLoaded++;
        }
    }
    
    return volumesLoaded;
}

/**
 * @brief Point d'entrée principal
 */
int main(int argc, char** argv) {
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  EXTRACTEUR DE COORDONNÉES ROBOT - PHANTASMAGORIA           ║\n";
    std::cout << "║  Parser RESSCI/RESMAP pour extraction x,y                   ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    std::string resourceDir = argv[1];
    int targetRobotId = -1;
    
    if (argc >= 3) {
        targetRobotId = std::atoi(argv[2]);
        std::cout << "🎯 Recherche spécifique du Robot #" << targetRobotId << "\n\n";
    }
    
    // Enlever le slash final si présent
    if (!resourceDir.empty() && resourceDir.back() == '/') {
        resourceDir.pop_back();
    }
    
    // Créer le parser
    RESSCIParser parser;
    
    // 1. Charger RESMAP
    std::cout << "📂 Chargement du RESMAP...\n";
    std::string resmapPath = resourceDir + "/RESMAP.001";
    
    if (!parser.loadResMap(resmapPath)) {
        // Essayer RESMAP.00X
        for (int i = 1; i <= 7; i++) {
            resmapPath = resourceDir + "/RESMAP.00" + std::to_string(i);
            if (parser.loadResMap(resmapPath)) {
                break;
            }
        }
    }
    
    std::cout << "\n";
    
    // 2. Charger les volumes RESSCI
    std::cout << "💿 Chargement des volumes RESSCI...\n";
    int volumesLoaded = loadAllVolumes(parser, resourceDir);
    
    if (volumesLoaded == 0) {
        std::cerr << "❌ Aucun volume RESSCI chargé !\n";
        std::cerr << "Vérifiez que le répertoire contient RESSCI.001, RESSCI.002, etc.\n";
        return 1;
    }
    
    std::cout << "✅ " << volumesLoaded << " volume(s) chargé(s)\n\n";
    
    // 3. Extraire les coordonnées
    std::cout << "🔍 Extraction des coordonnées Robot...\n\n";
    auto coords = parser.extractRobotCoordinates();
    
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";
    
    if (coords.empty()) {
        std::cout << "⚠️  Aucune coordonnée hardcodée trouvée dans les scripts.\n\n";
        std::cout << "EXPLICATION:\n";
        std::cout << "Les coordonnées Robot dans Phantasmagoria sont calculées dynamiquement\n";
        std::cout << "via des variables et propriétés d'objets, pas hardcodées dans le bytecode.\n\n";
        std::cout << "SOLUTIONS:\n";
        std::cout << "1. Utiliser ScummVM avec logs de débogage (méthode recommandée)\n";
        std::cout << "2. Utiliser generate_smart_positions.py pour positions par défaut\n";
        std::cout << "3. Analyser manuellement le HEAP avec un debugger SCI\n\n";
        return 0;
    }
    
    // 4. Organiser les résultats
    std::map<uint16_t, std::vector<RobotCoordinates>> coordsByRobot;
    
    for (const auto& coord : coords) {
        if (targetRobotId < 0 || coord.robotId == targetRobotId) {
            coordsByRobot[coord.robotId].push_back(coord);
        }
    }
    
    // 5. Afficher les résultats
    std::cout << "✅ " << coords.size() << " coordonnée(s) trouvée(s) pour " 
              << coordsByRobot.size() << " Robot(s)\n\n";
    
    std::cout << "COORDONNÉES ROBOT EXTRAITES:\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";
    
    for (const auto& [robotId, robotCoords] : coordsByRobot) {
        std::cout << "🎬 Robot #" << robotId << ":\n";
        
        // Grouper par position unique
        std::map<std::pair<int16_t, int16_t>, int> positionCounts;
        for (const auto& c : robotCoords) {
            positionCounts[{c.x, c.y}]++;
        }
        
        for (const auto& c : robotCoords) {
            std::cout << "   Position: (" << std::setw(3) << c.x << ", " 
                     << std::setw(3) << c.y << ")";
            
            if (c.scriptId > 0) {
                std::cout << " [Script " << c.scriptId << "]";
            }
            
            if (c.priority != 0) {
                std::cout << " [Priority: " << c.priority << "]";
            }
            
            if (c.scale != 128) {
                std::cout << " [Scale: " << c.scale << "]";
            }
            
            auto key = std::make_pair(c.x, c.y);
            if (positionCounts[key] > 1) {
                std::cout << " (×" << positionCounts[key] << ")";
                positionCounts[key] = 0; // Afficher seulement une fois
            }
            
            std::cout << "\n";
        }
        
        std::cout << "\n";
    }
    
    // 6. Sauvegarder dans robot_positions.txt
    std::string outputFile = "robot_positions_extracted.txt";
    std::ofstream out(outputFile);
    
    if (out) {
        out << "# Coordonnées Robot extraites depuis RESSCI/RESMAP\n";
        out << "# Format: robot_id X Y [script_id] [priority] [scale]\n";
        out << "# Généré automatiquement par extract_coordinates\n";
        out << "#\n";
        out << "# Résolution Phantasmagoria: 630x450 pixels\n";
        out << "# Origine: coin supérieur gauche (0,0)\n";
        out << "#\n\n";
        
        for (const auto& [robotId, robotCoords] : coordsByRobot) {
            // Utiliser la position la plus fréquente
            std::map<std::pair<int16_t, int16_t>, int> positionCounts;
            for (const auto& c : robotCoords) {
                positionCounts[{c.x, c.y}]++;
            }
            
            auto mostCommon = std::max_element(
                positionCounts.begin(),
                positionCounts.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; }
            );
            
            if (mostCommon != positionCounts.end()) {
                // Trouver le premier coord avec cette position
                for (const auto& c : robotCoords) {
                    if (c.x == mostCommon->first.first && c.y == mostCommon->first.second) {
                        out << std::setw(5) << robotId << " "
                           << std::setw(3) << c.x << " "
                           << std::setw(3) << c.y;
                        
                        if (c.scriptId > 0 || c.priority != 0 || c.scale != 128) {
                            out << "  # Script:" << c.scriptId;
                            if (c.priority != 0) out << " Priority:" << c.priority;
                            if (c.scale != 128) out << " Scale:" << c.scale;
                        }
                        
                        out << "\n";
                        break;
                    }
                }
            }
        }
        
        out.close();
        
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        std::cout << "💾 Sauvegardé dans: " << outputFile << "\n\n";
    } else {
        std::cerr << "❌ Erreur d'écriture dans " << outputFile << "\n";
    }
    
    // 7. Informations complémentaires
    std::cout << "📋 PROCHAINES ÉTAPES:\n\n";
    std::cout << "1. Vérifier " << outputFile << "\n";
    std::cout << "2. Comparer avec les positions ScummVM si disponibles\n";
    std::cout << "3. Intégrer dans robot_extractor avec l'option --positions\n";
    std::cout << "4. Tester l'extraction vidéo avec positionnement\n\n";
    
    std::cout << "💡 ASTUCE:\n";
    std::cout << "Si aucune coordonnée n'a été trouvée, utilisez:\n";
    std::cout << "  python3 generate_smart_positions.py RBT/\n\n";
    
    return 0;
}
