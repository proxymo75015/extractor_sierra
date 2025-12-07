#ifndef SCUMMVM_ROBOT_HELPERS_H
#define SCUMMVM_ROBOT_HELPERS_H

#include <cstdint>
#include <vector>
#include <string>

namespace ScummVMRobot {

// ============================================================================
// CONSTANTES SCUMMVM
// ============================================================================

// Valeur d'index palette pour pixel transparent (skip color)
constexpr uint8_t SKIP_COLOR = 255;

// Dimensions canvas Phantasmagoria (640x480 original, mais ScummVM utilise 630x450)
constexpr int PHANTASMAGORIA_CANVAS_WIDTH = 630;
constexpr int PHANTASMAGORIA_CANVAS_HEIGHT = 450;

// ============================================================================
// STRUCTURES
// ============================================================================

/**
 * Position d'un Robot extraite du fichier RESSCI
 * Correspond aux coordonnées de référence utilisées par ScummVM
 */
struct RobotPosition {
    int robotId;    // ID du Robot (-1 si non trouvé)
    int16_t x;      // Position X de référence (canvasX dans ScummVM)
    int16_t y;      // Position Y de référence (canvasY dans ScummVM)
    
    RobotPosition() : robotId(-1), x(0), y(0) {}
    RobotPosition(int id, int16_t px, int16_t py) : robotId(id), x(px), y(py) {}
    
    bool isValid() const { return robotId != -1; }
};

/**
 * Métadonnées d'un cel (frame individuelle)
 * Extraites du fichier RBT
 */
struct CelMetadata {
    uint16_t celX;       // Offset X relatif du cel
    uint16_t celY;       // Offset Y relatif du cel (représente le BAS du sprite)
    uint16_t celWidth;   // Largeur réelle du cel
    uint16_t celHeight;  // Hauteur réelle du cel
    
    CelMetadata() : celX(0), celY(0), celWidth(0), celHeight(0) {}
    CelMetadata(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
        : celX(x), celY(y), celWidth(w), celHeight(h) {}
};

/**
 * Position calculée d'un cel sur l'écran
 * Utilise la formule ScummVM
 */
struct CelScreenPosition {
    int offsetX;  // Position X du coin supérieur gauche
    int offsetY;  // Position Y du coin supérieur gauche
    int width;    // Largeur du cel
    int height;   // Hauteur du cel
    
    CelScreenPosition() : offsetX(0), offsetY(0), width(0), height(0) {}
    CelScreenPosition(int x, int y, int w, int h)
        : offsetX(x), offsetY(y), width(w), height(h) {}
};

// ============================================================================
// FORMULES SCUMMVM
// ============================================================================

/**
 * Calcule la position écran d'un cel en mode CANVAS
 * Formule ScummVM robot_decoder.cpp:1493-1501
 * 
 * @param robotPos Position de référence du Robot (depuis RESSCI)
 * @param celMeta Métadonnées du cel (depuis RBT)
 * @return Position écran calculée
 * 
 * Architecture ScummVM:
 * - robotPos (canvasX, canvasY) = point de référence du Robot
 * - celMeta (celX, celY) = offset relatif de ce cel
 * - celY représente le BAS du sprite (origine en bas)
 * - offsetY = position du coin supérieur gauche
 */
inline CelScreenPosition calculateCanvasPosition(const RobotPosition& robotPos,
                                                   const CelMetadata& celMeta) {
    CelScreenPosition result;
    
    // Position absolue sur le canvas (formule ScummVM)
    result.offsetX = robotPos.x + celMeta.celX;
    result.offsetY = robotPos.y + celMeta.celY - celMeta.celHeight;
    result.width = celMeta.celWidth;
    result.height = celMeta.celHeight;
    
    return result;
}

/**
 * Calcule la position relative d'un cel en mode CROP
 * Formule ScummVM adaptée (sans position RESSCI de référence)
 * 
 * @param celMeta Métadonnées du cel (depuis RBT)
 * @return Position relative calculée
 * 
 * Architecture mode CROP:
 * - Pas de position RESSCI (Robot non trouvé)
 * - celX, celY utilisés comme offsets relatifs
 * - celY représente le BAS du sprite (origine en bas)
 * - Les cels sont positionnés les uns par rapport aux autres
 */
inline CelScreenPosition calculateCropPosition(const CelMetadata& celMeta) {
    CelScreenPosition result;
    
    // Position relative (formule ScummVM sans canvasX/canvasY)
    result.offsetX = celMeta.celX;
    result.offsetY = celMeta.celY - celMeta.celHeight;
    result.width = celMeta.celWidth;
    result.height = celMeta.celHeight;
    
    return result;
}

/**
 * Vérifie si un pixel est transparent
 * @param paletteIndex Index dans la palette
 * @return true si transparent (skip color)
 */
inline bool isTransparentPixel(uint8_t paletteIndex) {
    return paletteIndex == SKIP_COLOR;
}

/**
 * Arrondit une dimension à un multiple de 2 (requis pour H.264/YUV420p)
 * @param dimension Dimension à arrondir
 * @return Dimension arrondie (pair)
 */
inline int roundToEven(int dimension) {
    return (dimension % 2 == 0) ? dimension : dimension + 1;
}

// ============================================================================
// GESTION POSITIONS RESSCI
// ============================================================================

/**
 * Charge les positions des Robots depuis robot_positions_extracted.txt
 * @param filename Chemin du fichier (par défaut: robot_positions_extracted.txt)
 * @return Vecteur de positions Robot
 */
std::vector<RobotPosition> loadRobotPositions(const std::string& filename = "robot_positions_extracted.txt");

/**
 * Recherche la position d'un Robot par son ID
 * @param positions Vecteur de positions chargées
 * @param robotId ID du Robot à rechercher
 * @return Position trouvée (robotId=-1 si non trouvé)
 */
RobotPosition findRobotPosition(const std::vector<RobotPosition>& positions, int robotId);

} // namespace ScummVMRobot

#endif // SCUMMVM_ROBOT_HELPERS_H
