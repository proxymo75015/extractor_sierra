#ifndef ROBOT_EXR_EXPORTER_H
#define ROBOT_EXR_EXPORTER_H

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace SierraExtractor {

/**
 * @brief Structure représentant un frame Robot décodé avec séparation des types de pixels
 */
struct RobotFrameLayers {
    int width;
    int height;
    
    // Couche 1: Couleurs fixes (pixels 0-235 PC / 0-236 Mac)
    std::vector<uint8_t> base_r;
    std::vector<uint8_t> base_g;
    std::vector<uint8_t> base_b;
    
    // Couche 2: Masque de remap (1.0 si pixel 236-254 PC / 237-254 Mac, 0.0 sinon)
    std::vector<float> remap_mask;
    
    // Couleurs originales de remap (avant transformation dynamique)
    std::vector<uint8_t> remap_color_r;
    std::vector<uint8_t> remap_color_g;
    std::vector<uint8_t> remap_color_b;
    
    // Couche 3: Alpha (0.0 si pixel=255 skipColor, 1.0 sinon)
    std::vector<float> alpha;
    
    // Données additionnelles pour debug/référence
    std::vector<uint8_t> pixel_indices; // Index palette original 0-255
    
    RobotFrameLayers(int w, int h);
};

/**
 * @brief Configuration pour l'export OpenEXR
 */
struct EXRExportConfig {
    enum class Compression {
        NONE,       // Pas de compression
        ZIP,        // ZIP compression (bon ratio)
        PIZ,        // PIZ compression (meilleur pour images naturelles)
        RLE,        // Run-length encoding (bon pour masques)
        ZIPS        // ZIP par scanline (rapide)
    };
    
    Compression compression = Compression::ZIP;
    bool include_pixel_indices = true;  // Inclure couche debug avec indices originaux
    bool include_palette_metadata = true; // Stocker palette dans metadata
    bool is_mac_platform = false;       // true pour Mac (remap 237-254), false pour PC (236-254)
    
    EXRExportConfig() = default;
};

/**
 * @brief Exportateur de frames Robot vers format OpenEXR multi-couches
 * 
 * Cette classe permet d'exporter les frames Robot en préservant les trois types
 * de pixels dans des couches séparées :
 * - Layer 1: RGB base (couleurs fixes opaques)
 * - Layer 2: Mask remap (zones de recoloration dynamique)
 * - Layer 3: Alpha transparency (transparence)
 * 
 * Format des couches OpenEXR :
 * - "base.R", "base.G", "base.B"         : Couleurs fixes (uint8)
 * - "remap_mask.Y"                        : Masque binaire remap (float)
 * - "remap_color.R", "G", "B"            : Couleurs remap originales (uint8)
 * - "alpha.A"                             : Canal alpha (float)
 * - "pixel_index.Y" (optionnel)          : Index palette original (uint8)
 * 
 * Metadata incluses :
 * - "robot:version"                       : Version Robot (5 ou 6)
 * - "robot:platform"                      : "PC" ou "Mac"
 * - "robot:remap_range"                   : "236-254" ou "237-254"
 * - "robot:skip_color"                    : "255"
 * - "robot:palette" (optionnel)          : Palette complète 256 couleurs
 */
class RobotEXRExporter {
public:
    /**
     * @brief Constructeur
     * @param config Configuration d'export
     */
    explicit RobotEXRExporter(const EXRExportConfig& config = EXRExportConfig());
    
    ~RobotEXRExporter();
    
    /**
     * @brief Extrait les couches d'un frame Robot à partir des données décodées
     * 
     * @param pixel_data Données pixel décodées (indices palette 0-255)
     * @param width Largeur de l'image
     * @param height Hauteur de l'image
     * @param palette Palette Robot (256 entrées RGB)
     * @return Structure contenant toutes les couches séparées
     */
    RobotFrameLayers extractLayers(
        const std::vector<uint8_t>& pixel_data,
        int width,
        int height,
        const std::vector<uint8_t>& palette  // 256 * 3 bytes (RGB)
    );
    
    /**
     * @brief Exporte un frame vers fichier OpenEXR
     * 
     * @param layers Couches extraites
     * @param output_path Chemin du fichier .exr à créer
     * @param palette Palette originale (pour metadata si config.include_palette_metadata=true)
     * @param frame_number Numéro de frame (pour metadata)
     * @return true si succès, false si erreur
     */
    bool exportFrame(
        const RobotFrameLayers& layers,
        const std::string& output_path,
        const std::vector<uint8_t>& palette,
        int frame_number = 0
    );
    
    /**
     * @brief Exporte une séquence complète de frames
     * 
     * @param frames_data Vecteur de données pixel pour chaque frame
     * @param width Largeur commune
     * @param height Hauteur commune
     * @param palette Palette commune
     * @param output_directory Répertoire de sortie
     * @param base_name Nom de base (ex: "frame" → "frame_0001.exr")
     * @return Nombre de frames exportés avec succès
     */
    int exportSequence(
        const std::vector<std::vector<uint8_t>>& frames_data,
        int width,
        int height,
        const std::vector<uint8_t>& palette,
        const std::string& output_directory,
        const std::string& base_name = "frame"
    );
    
    /**
     * @brief Configure la plateforme (PC ou Mac) pour déterminer la plage de remap
     * @param is_mac true pour Mac (237-254), false pour PC (236-254)
     */
    void setPlatform(bool is_mac);
    
    /**
     * @brief Obtient la configuration actuelle
     */
    const EXRExportConfig& getConfig() const { return config_; }
    
    /**
     * @brief Modifie la configuration
     */
    void setConfig(const EXRExportConfig& config) { config_ = config; }

private:
    EXRExportConfig config_;
    
    // Plages de remap selon plateforme
    uint8_t remap_start_;  // 236 (PC) ou 237 (Mac)
    uint8_t remap_end_;    // 254 (les deux)
    static constexpr uint8_t SKIP_COLOR = 255;
    
    /**
     * @brief Détermine si un index de palette est dans la zone de remap
     */
    inline bool isRemapPixel(uint8_t pixel_index) const {
        return pixel_index >= remap_start_ && pixel_index <= remap_end_;
    }
    
    /**
     * @brief Détermine si un index de palette est transparent
     */
    inline bool isTransparent(uint8_t pixel_index) const {
        return pixel_index == SKIP_COLOR;
    }
    
    /**
     * @brief Détermine si un index de palette est une couleur fixe opaque
     */
    inline bool isOpaqueColor(uint8_t pixel_index) const {
        return pixel_index < remap_start_;
    }
    
    /**
     * @brief Convertit un index palette en couleur RGB via la palette
     */
    void paletteIndexToRGB(
        uint8_t index,
        const std::vector<uint8_t>& palette,
        uint8_t& r, uint8_t& g, uint8_t& b
    ) const;
};

} // namespace SierraExtractor

#endif // ROBOT_EXR_EXPORTER_H
