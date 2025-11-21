#ifndef EXR_WRITER_H
#define EXR_WRITER_H

#include <string>
#include <vector>
#include <cstdint>
#include <map>

namespace SierraExtractor {

/**
 * @brief Wrapper simplifié pour l'écriture de fichiers OpenEXR multi-canaux
 * 
 * Cette classe encapsule la complexité de l'API OpenEXR et fournit une interface
 * simple pour créer des fichiers .exr avec plusieurs canaux et métadonnées.
 * 
 * Exemple d'utilisation :
 * 
 *   EXRWriter writer("output.exr", 640, 480);
 *   writer.setCompression(EXRWriter::Compression::ZIP);
 *   writer.addStringAttribute("software", "RobotDecoder");
 *   writer.addChannel("R", red_data);
 *   writer.addChannel("G", green_data);
 *   writer.addChannel("B", blue_data);
 *   writer.addChannel("A", alpha_data);
 *   writer.write();
 */
class EXRWriter {
public:
    /**
     * @brief Types de compression supportés par OpenEXR
     */
    enum class Compression {
        NONE,       // Pas de compression
        RLE,        // Run-length encoding
        ZIPS,       // ZIP par scanline (rapide)
        ZIP,        // ZIP par blocs de 16 scanlines (bon ratio)
        PIZ,        // Wavelet compression (meilleur pour images naturelles)
        PXR24,      // Lossy 24-bit float
        B44,        // Lossy 4x4 block compression
        B44A,       // B44 avec zones plates non compressées
        DWAA,       // DCT-based lossy compression
        DWAB        // DWAA avec blocs de 256 scanlines
    };
    
    /**
     * @brief Type de pixel pour un canal
     */
    enum class PixelType {
        UINT,       // Unsigned int (32-bit)
        HALF,       // Half float (16-bit)
        FLOAT       // Float (32-bit)
    };
    
    /**
     * @brief Constructeur
     * @param filename Chemin du fichier .exr à créer
     * @param width Largeur de l'image en pixels
     * @param height Hauteur de l'image en pixels
     */
    EXRWriter(const std::string& filename, int width, int height);
    
    ~EXRWriter();
    
    /**
     * @brief Configure le type de compression
     * @param compression Type de compression à utiliser
     */
    void setCompression(Compression compression);
    
    /**
     * @brief Ajoute un attribut string dans les métadonnées
     * @param name Nom de l'attribut
     * @param value Valeur de l'attribut
     */
    void addStringAttribute(const std::string& name, const std::string& value);
    
    /**
     * @brief Ajoute un attribut int dans les métadonnées
     * @param name Nom de l'attribut
     * @param value Valeur de l'attribut
     */
    void addIntAttribute(const std::string& name, int value);
    
    /**
     * @brief Ajoute un attribut float dans les métadonnées
     * @param name Nom de l'attribut
     * @param value Valeur de l'attribut
     */
    void addFloatAttribute(const std::string& name, float value);
    
    /**
     * @brief Ajoute un canal uint8 (converti en HALF pour EXR)
     * @param name Nom du canal (ex: "R", "G", "B", "base.R", etc.)
     * @param data Données du canal (width * height uint8)
     */
    void addChannel(const std::string& name, const std::vector<uint8_t>& data);
    
    /**
     * @brief Ajoute un canal float
     * @param name Nom du canal
     * @param data Données du canal (width * height float)
     */
    void addChannel(const std::string& name, const std::vector<float>& data);
    
    /**
     * @brief Ajoute un canal uint32
     * @param name Nom du canal
     * @param data Données du canal (width * height uint32)
     */
    void addChannel(const std::string& name, const std::vector<uint32_t>& data);
    
    /**
     * @brief Écrit le fichier EXR avec tous les canaux et métadonnées ajoutés
     * @return true si succès, false si erreur
     */
    bool write();
    
    /**
     * @brief Obtient la largeur de l'image
     */
    int getWidth() const { return width_; }
    
    /**
     * @brief Obtient la hauteur de l'image
     */
    int getHeight() const { return height_; }

private:
    struct ChannelData {
        PixelType type;
        std::vector<uint8_t> data_bytes;  // Stockage générique
    };
    
    std::string filename_;
    int width_;
    int height_;
    Compression compression_;
    
    std::map<std::string, ChannelData> channels_;
    std::map<std::string, std::string> string_attributes_;
    std::map<std::string, int> int_attributes_;
    std::map<std::string, float> float_attributes_;
    
    /**
     * @brief Convertit uint8 vers half-float pour stockage EXR
     */
    void convertUint8ToHalf(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);
    
    /**
     * @brief Valide qu'un canal a la bonne taille
     */
    bool validateChannelSize(size_t data_size) const;
};

} // namespace SierraExtractor

#endif // EXR_WRITER_H
