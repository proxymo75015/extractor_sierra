#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace RobotExtractor {

/**
 * Structure contenant les données d'une frame décomposée en couches
 */
struct RobotLayerFrame {
    int width;
    int height;
    
    // Couche 1: RGB base (pixels 0-235) - Couleurs fixes opaques
    std::vector<uint8_t> base_r;
    std::vector<uint8_t> base_g;
    std::vector<uint8_t> base_b;
    
    // Couche 2: Mask remap (pixels 236-254) - Zone de recoloration
    std::vector<uint8_t> remap_mask;     // 255 = pixel remap, 0 = autre
    std::vector<uint8_t> remap_color_r;  // Couleur RGB du pixel remap
    std::vector<uint8_t> remap_color_g;
    std::vector<uint8_t> remap_color_b;
    
    // Couche 3: Alpha transparency (pixel 255) - Transparence
    std::vector<uint8_t> alpha;  // 255 = opaque, 0 = transparent (skip)
    
    RobotLayerFrame(int w, int h) : width(w), height(h) {
        size_t size = w * h;
        base_r.resize(size, 0);
        base_g.resize(size, 0);
        base_b.resize(size, 0);
        remap_mask.resize(size, 0);
        remap_color_r.resize(size, 0);
        remap_color_g.resize(size, 0);
        remap_color_b.resize(size, 0);
        alpha.resize(size, 255);  // Par défaut opaque
    }
};

/**
 * Configuration pour l'export MKV
 */
struct MKVExportConfig {
    enum class Codec {
        H264,      // x264 (universel)
        H265,      // x265 (meilleure compression)
        VP9,       // VP9 (open source, excellente qualité)
        FFV1       // FFV1 (lossless, archivage)
    };
    
    Codec codec = Codec::H264;
    int framerate = 10;
    int quality = 23;  // CRF pour x264/x265/VP9 (18-28, plus bas = meilleure qualité)
};

/**
 * Exporteur MKV multi-couches pour vidéos Robot
 */
class RobotMKVExporter {
public:
    RobotMKVExporter(const MKVExportConfig& config = MKVExportConfig());
    
    /**
     * Exporte une séquence de frames en MKV avec 4 pistes vidéo
     * 
     * @param layers        Vecteur de frames décomposées en couches
     * @param outputPath    Chemin du fichier MKV de sortie (sans extension)
     * @param audioPath     Chemin du fichier audio WAV (optionnel)
     * @return true si succès
     * 
     * Structure du MKV:
     * - Track 0: BASE layer (RGB, pixels 0-235)
     * - Track 1: REMAP layer (RGB, pixels 236-254)
     * - Track 2: ALPHA layer (grayscale, pixel 255)
     * - Track 3: COMPOSITE layer (preview RGB final)
     * - Audio: PCM 48kHz (si fourni)
     */
    bool exportMultiTrack(const std::vector<RobotLayerFrame>& layers,
                          const std::string& outputPath,
                          const std::string& audioPath = "");
    
private:
    MKVExportConfig config_;
};

/**
 * Décompose une frame Robot en couches selon les types de pixels
 * 
 * @param pixelIndices  Indices palette de chaque pixel (320x200)
 * @param palette       Palette RGB (256 couleurs × 3 composantes)
 * @param width         Largeur de l'image
 * @param height        Hauteur de l'image
 * @return Frame décomposée en couches
 * 
 * Types de pixels Robot (Sierra SCI):
 * - 0-235 (PC) / 0-236 (Mac): BASE - Couleurs fixes opaques
 * - 236-254 (PC) / 237-254 (Mac): REMAP - Zone de recoloration
 * - 255: SKIP - Pixel transparent (alpha = 0)
 */
RobotLayerFrame decomposeRobotFrame(
    const std::vector<uint8_t>& pixelIndices,
    const std::vector<uint8_t>& palette,
    int width,
    int height
);

} // namespace RobotExtractor
