#include "robot_exr_exporter.h"
#include "exr_writer.h"
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace SierraExtractor {

// ============================================================================
// RobotFrameLayers Implementation
// ============================================================================

RobotFrameLayers::RobotFrameLayers(int w, int h) 
    : width(w), height(h) {
    size_t pixel_count = static_cast<size_t>(width) * height;
    
    // Allouer toutes les couches
    base_r.resize(pixel_count, 0);
    base_g.resize(pixel_count, 0);
    base_b.resize(pixel_count, 0);
    
    remap_mask.resize(pixel_count, 0.0f);
    
    remap_color_r.resize(pixel_count, 0);
    remap_color_g.resize(pixel_count, 0);
    remap_color_b.resize(pixel_count, 0);
    
    alpha.resize(pixel_count, 1.0f);
    
    pixel_indices.resize(pixel_count, 0);
}

// ============================================================================
// RobotEXRExporter Implementation
// ============================================================================

RobotEXRExporter::RobotEXRExporter(const EXRExportConfig& config)
    : config_(config) {
    setPlatform(config.is_mac_platform);
}

RobotEXRExporter::~RobotEXRExporter() = default;

void RobotEXRExporter::setPlatform(bool is_mac) {
    config_.is_mac_platform = is_mac;
    remap_start_ = is_mac ? 237 : 236;
    remap_end_ = 254;
}

void RobotEXRExporter::paletteIndexToRGB(
    uint8_t index,
    const std::vector<uint8_t>& palette,
    uint8_t& r, uint8_t& g, uint8_t& b
) const {
    if (palette.size() < 768) {  // 256 * 3
        throw std::runtime_error("Invalid palette size (expected 768 bytes)");
    }
    
    size_t offset = static_cast<size_t>(index) * 3;
    r = palette[offset];
    g = palette[offset + 1];
    b = palette[offset + 2];
}

RobotFrameLayers RobotEXRExporter::extractLayers(
    const std::vector<uint8_t>& pixel_data,
    int width,
    int height,
    const std::vector<uint8_t>& palette
) {
    if (palette.size() < 768) {
        throw std::runtime_error("Palette must contain 768 bytes (256 RGB entries)");
    }
    
    size_t expected_size = static_cast<size_t>(width) * height;
    if (pixel_data.size() != expected_size) {
        throw std::runtime_error("Pixel data size mismatch");
    }
    
    RobotFrameLayers layers(width, height);
    
    // Parcourir chaque pixel et classifier selon sa valeur
    for (size_t i = 0; i < pixel_data.size(); ++i) {
        uint8_t pixel_index = pixel_data[i];
        layers.pixel_indices[i] = pixel_index;
        
        uint8_t r, g, b;
        paletteIndexToRGB(pixel_index, palette, r, g, b);
        
        // Classification selon le TYPE de pixel (déterminé par sa VALEUR)
        
        if (isTransparent(pixel_index)) {
            // Type 1: Skip color (255) - Pixel transparent
            layers.alpha[i] = 0.0f;
            layers.base_r[i] = 0;
            layers.base_g[i] = 0;
            layers.base_b[i] = 0;
            layers.remap_mask[i] = 0.0f;
            layers.remap_color_r[i] = 0;
            layers.remap_color_g[i] = 0;
            layers.remap_color_b[i] = 0;
            
        } else if (isRemapPixel(pixel_index)) {
            // Type 2: Remap colors (236-254 PC / 237-254 Mac)
            // Couleur définie dans palette mais "flottante" (recoloration dynamique)
            layers.alpha[i] = 1.0f;
            layers.remap_mask[i] = 1.0f;  // Marquer comme zone de remap
            layers.remap_color_r[i] = r;  // Stocker couleur originale
            layers.remap_color_g[i] = g;
            layers.remap_color_b[i] = b;
            
            // Base layer contient aussi la couleur pour fallback
            layers.base_r[i] = r;
            layers.base_g[i] = g;
            layers.base_b[i] = b;
            
        } else {
            // Type 3: Opaque colors (0-235 PC / 0-236 Mac)
            // Couleurs fixes
            layers.alpha[i] = 1.0f;
            layers.base_r[i] = r;
            layers.base_g[i] = g;
            layers.base_b[i] = b;
            layers.remap_mask[i] = 0.0f;  // Pas de remap
            layers.remap_color_r[i] = 0;
            layers.remap_color_g[i] = 0;
            layers.remap_color_b[i] = 0;
        }
    }
    
    return layers;
}

bool RobotEXRExporter::exportFrame(
    const RobotFrameLayers& layers,
    const std::string& output_path,
    const std::vector<uint8_t>& palette,
    int frame_number
) {
    try {
        EXRWriter writer(output_path, layers.width, layers.height);
        
        // Configurer la compression
        switch (config_.compression) {
            case EXRExportConfig::Compression::NONE:
                writer.setCompression(EXRWriter::Compression::NONE);
                break;
            case EXRExportConfig::Compression::ZIP:
                writer.setCompression(EXRWriter::Compression::ZIP);
                break;
            case EXRExportConfig::Compression::PIZ:
                writer.setCompression(EXRWriter::Compression::PIZ);
                break;
            case EXRExportConfig::Compression::RLE:
                writer.setCompression(EXRWriter::Compression::RLE);
                break;
            case EXRExportConfig::Compression::ZIPS:
                writer.setCompression(EXRWriter::Compression::ZIPS);
                break;
        }
        
        // Ajouter les métadonnées Robot
        writer.addStringAttribute("robot:version", "5/6");
        writer.addStringAttribute("robot:platform", config_.is_mac_platform ? "Mac" : "PC");
        
        std::ostringstream remap_range;
        remap_range << static_cast<int>(remap_start_) << "-" << static_cast<int>(remap_end_);
        writer.addStringAttribute("robot:remap_range", remap_range.str());
        
        writer.addStringAttribute("robot:skip_color", "255");
        writer.addIntAttribute("robot:frame_number", frame_number);
        
        // Inclure palette dans metadata si demandé
        if (config_.include_palette_metadata && palette.size() >= 768) {
            std::ostringstream palette_str;
            for (size_t i = 0; i < 768; ++i) {
                if (i > 0) palette_str << ",";
                palette_str << static_cast<int>(palette[i]);
            }
            writer.addStringAttribute("robot:palette", palette_str.str());
        }
        
        // Ajouter les canaux RGB de base (couleurs fixes)
        writer.addChannel("base.R", layers.base_r);
        writer.addChannel("base.G", layers.base_g);
        writer.addChannel("base.B", layers.base_b);
        
        // Ajouter le masque de remap (float)
        writer.addChannel("remap_mask.Y", layers.remap_mask);
        
        // Ajouter les couleurs de remap originales
        writer.addChannel("remap_color.R", layers.remap_color_r);
        writer.addChannel("remap_color.G", layers.remap_color_g);
        writer.addChannel("remap_color.B", layers.remap_color_b);
        
        // Ajouter le canal alpha
        writer.addChannel("alpha.A", layers.alpha);
        
        // Ajouter les indices originaux si demandé (utile pour debug)
        if (config_.include_pixel_indices) {
            writer.addChannel("pixel_index.Y", layers.pixel_indices);
        }
        
        // Écrire le fichier
        return writer.write();
        
    } catch (const std::exception& e) {
        // Log error (à adapter selon votre système de logging)
        return false;
    }
}

int RobotEXRExporter::exportSequence(
    const std::vector<std::vector<uint8_t>>& frames_data,
    int width,
    int height,
    const std::vector<uint8_t>& palette,
    const std::string& output_directory,
    const std::string& base_name
) {
    int success_count = 0;
    
    for (size_t i = 0; i < frames_data.size(); ++i) {
        // Générer le nom de fichier avec padding (ex: frame_0001.exr)
        std::ostringstream filename;
        filename << output_directory << "/" << base_name << "_"
                 << std::setfill('0') << std::setw(4) << i << ".exr";
        
        try {
            // Extraire les couches
            RobotFrameLayers layers = extractLayers(frames_data[i], width, height, palette);
            
            // Exporter le frame
            if (exportFrame(layers, filename.str(), palette, static_cast<int>(i))) {
                ++success_count;
            }
            
        } catch (const std::exception& e) {
            // Log error et continuer
            continue;
        }
    }
    
    return success_count;
}

} // namespace SierraExtractor
