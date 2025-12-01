#include "robot_mkv_exporter.h"
#include "../include/stb_image_write.h"
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <ctime>

namespace RobotExtractor {

RobotMKVExporter::RobotMKVExporter(const MKVExportConfig& config)
    : config_(config) {
}

RobotLayerFrame decomposeRobotFrame(
    const std::vector<uint8_t>& pixelIndices,
    const std::vector<uint8_t>& palette,
    int width,
    int height
) {
    RobotLayerFrame frame(width, height);
    const size_t pixelCount = width * height;
    
    // Classification des types de pixels Robot (Sierra SCI)
    // Référence: ScummVM engines/sci/graphics/robot.cpp
    const uint8_t REMAP_START_PC = 236;
    const uint8_t REMAP_END = 254;
    const uint8_t SKIP_COLOR = 255;
    
    for (size_t i = 0; i < pixelCount; ++i) {
        uint8_t paletteIndex = pixelIndices[i];
        
        if (paletteIndex == SKIP_COLOR) {
            // Type 3: SKIP - Pixel transparent
            frame.alpha[i] = 0;  // Transparent
            frame.base_r[i] = 0;
            frame.base_g[i] = 0;
            frame.base_b[i] = 0;
            frame.remap_mask[i] = 0;
        }
        else if (paletteIndex >= REMAP_START_PC && paletteIndex <= REMAP_END) {
            // Type 2: REMAP - Zone de recoloration (236-254)
            frame.alpha[i] = 255;  // Opaque
            frame.remap_mask[i] = 255;  // Marquer comme remap
            
            // Stocker la couleur RGB du pixel remap
            size_t palIdx = paletteIndex * 3;
            frame.remap_color_r[i] = palette[palIdx + 0];
            frame.remap_color_g[i] = palette[palIdx + 1];
            frame.remap_color_b[i] = palette[palIdx + 2];
            
            // Pas de couleur base pour ce pixel
            frame.base_r[i] = 0;
            frame.base_g[i] = 0;
            frame.base_b[i] = 0;
        }
        else {
            // Type 1: BASE - Couleur fixe opaque (0-235)
            frame.alpha[i] = 255;  // Opaque
            frame.remap_mask[i] = 0;  // Pas de remap
            
            // Stocker la couleur RGB base
            size_t palIdx = paletteIndex * 3;
            frame.base_r[i] = palette[palIdx + 0];
            frame.base_g[i] = palette[palIdx + 1];
            frame.base_b[i] = palette[palIdx + 2];
        }
    }
    
    return frame;
}

bool RobotMKVExporter::exportMultiTrack(
    const std::vector<RobotLayerFrame>& layers,
    const std::string& outputPath,
    const std::string& audioPath
) {
    if (layers.empty()) {
        fprintf(stderr, "Error: no layers to export\n");
        return false;
    }
    
    const int w = layers[0].width;
    const int h = layers[0].height;
    const size_t numFrames = layers.size();
    
    fprintf(stderr, "\n=== Exporting Multi-Track MKV ===\n");
    fprintf(stderr, "Frames: %zu\n", numFrames);
    fprintf(stderr, "Resolution: %dx%d\n", w, h);
    fprintf(stderr, "Output: %s.mkv\n\n", outputPath.c_str());
    
    // Créer 4 dossiers temporaires pour les 4 couches
    std::string tempBase = std::string("/tmp/robot_mkv_") + std::to_string(time(nullptr));
    std::string tempDirBase = tempBase + "_base";
    std::string tempDirRemap = tempBase + "_remap";
    std::string tempDirAlpha = tempBase + "_alpha";
    std::string tempDirComposite = tempBase + "_composite";
    
    mkdir(tempDirBase.c_str(), 0755);
    mkdir(tempDirRemap.c_str(), 0755);
    mkdir(tempDirAlpha.c_str(), 0755);
    mkdir(tempDirComposite.c_str(), 0755);
    
    // ========================================================================
    // ÉTAPE 1: Générer les frames PNG pour chaque couche
    // ========================================================================
    fprintf(stderr, "Step 1/4: Generating PNG frames for 4 layers...\n");
    
    for (size_t frameIdx = 0; frameIdx < numFrames; ++frameIdx) {
        const RobotLayerFrame& layer = layers[frameIdx];
        const size_t pixelCount = w * h;
        
        // Couche BASE: RGB complet des pixels 0-235
        std::vector<uint8_t> baseRGB(pixelCount * 3);
        // Couche REMAP: RGB complet des pixels 236-254
        std::vector<uint8_t> remapRGB(pixelCount * 3);
        // Couche ALPHA: Masque de transparence (pixel 255)
        std::vector<uint8_t> alphaGray(pixelCount);
        // Couche LUMINANCE: Image finale en niveau de gris (Y)
        std::vector<uint8_t> luminanceGray(pixelCount);
        
        for (size_t i = 0; i < pixelCount; ++i) {
            // BASE: Pixels opaques non-remap (RGB complet)
            if (layer.remap_mask[i] == 0 && layer.alpha[i] == 255) {
                baseRGB[i * 3 + 0] = layer.base_r[i];
                baseRGB[i * 3 + 1] = layer.base_g[i];
                baseRGB[i * 3 + 2] = layer.base_b[i];
            } else {
                baseRGB[i * 3 + 0] = 0;
                baseRGB[i * 3 + 1] = 0;
                baseRGB[i * 3 + 2] = 0;
            }
            
            // REMAP: Pixels de recoloration (RGB complet)
            if (layer.remap_mask[i] == 255 && layer.alpha[i] == 255) {
                remapRGB[i * 3 + 0] = layer.remap_color_r[i];
                remapRGB[i * 3 + 1] = layer.remap_color_g[i];
                remapRGB[i * 3 + 2] = layer.remap_color_b[i];
            } else {
                remapRGB[i * 3 + 0] = 0;
                remapRGB[i * 3 + 1] = 0;
                remapRGB[i * 3 + 2] = 0;
            }
            
            // ALPHA: Transparence (255 = skip, 0 = opaque)
            alphaGray[i] = (layer.alpha[i] == 0) ? 255 : 0;
            
            // LUMINANCE: Conversion RGB → Y (ITU-R BT.601)
            // Y = 0.299*R + 0.587*G + 0.114*B
            uint8_t finalR, finalG, finalB;
            if (layer.alpha[i] == 0) {
                // Pixel skip = noir
                finalR = finalG = finalB = 0;
            } else if (layer.remap_mask[i] == 255) {
                // Pixel remap
                finalR = layer.remap_color_r[i];
                finalG = layer.remap_color_g[i];
                finalB = layer.remap_color_b[i];
            } else {
                // Pixel base
                finalR = layer.base_r[i];
                finalG = layer.base_g[i];
                finalB = layer.base_b[i];
            }
            
            // Formule de luminance standard (BT.601)
            uint8_t Y = (uint8_t)(0.299f * finalR + 0.587f * finalG + 0.114f * finalB);
            luminanceGray[i] = Y;
        }
        
        // Écrire les 4 PNG
        char filename[512];
        
        snprintf(filename, sizeof(filename), "%s/frame_%04zu.png", tempDirBase.c_str(), frameIdx);
        if (!stbi_write_png(filename, w, h, 3, baseRGB.data(), w * 3)) {
            fprintf(stderr, "Error: failed to write base layer %s\n", filename);
            return false;
        }
        
        snprintf(filename, sizeof(filename), "%s/frame_%04zu.png", tempDirRemap.c_str(), frameIdx);
        if (!stbi_write_png(filename, w, h, 3, remapRGB.data(), w * 3)) {
            fprintf(stderr, "Error: failed to write remap layer %s\n", filename);
            return false;
        }
        
        snprintf(filename, sizeof(filename), "%s/frame_%04zu.png", tempDirAlpha.c_str(), frameIdx);
        if (!stbi_write_png(filename, w, h, 1, alphaGray.data(), w)) {
            fprintf(stderr, "Error: failed to write alpha layer %s\n", filename);
            return false;
        }
        
        snprintf(filename, sizeof(filename), "%s/frame_%04zu.png", tempDirComposite.c_str(), frameIdx);
        if (!stbi_write_png(filename, w, h, 1, luminanceGray.data(), w)) {
            fprintf(stderr, "Error: failed to write luminance layer %s\n", filename);
            return false;
        }
        
        if ((frameIdx + 1) % 10 == 0 || frameIdx == numFrames - 1) {
            fprintf(stderr, "\r  Writing frame %zu/%zu...", frameIdx + 1, numFrames);
            fflush(stderr);
        }
    }
    fprintf(stderr, "\n");
    
    // ========================================================================
    // ÉTAPE 2: Encoder UN SEUL MKV avec 4 pistes vidéo + audio
    // ========================================================================
    fprintf(stderr, "\nStep 2/4: Encoding MKV with 4 video tracks...\n");
    
    std::string outputFile = outputPath + ".mkv";
    
    // Sélectionner le codec vidéo
    std::ostringstream codecSettings;
    switch (config_.codec) {
        case MKVExportConfig::Codec::H264:
            codecSettings << "libx264 -preset medium -crf " << config_.quality;
            break;
        case MKVExportConfig::Codec::H265:
            codecSettings << "libx265 -preset medium -crf " << config_.quality;
            break;
        case MKVExportConfig::Codec::VP9:
            codecSettings << "libvpx-vp9 -crf " << config_.quality << " -b:v 0";
            break;
        case MKVExportConfig::Codec::FFV1:
            codecSettings << "ffv1 -level 3";  // Lossless
            break;
    }
    
    // Construire la commande FFmpeg pour MKV multi-pistes
    // MKV (Matroska) supporte nativement plusieurs pistes vidéo
    std::ostringstream cmd;
    cmd << "ffmpeg -y -framerate " << config_.framerate
        << " -i " << tempDirBase << "/frame_%04d.png "        // Input 0: BASE
        << " -framerate " << config_.framerate
        << " -i " << tempDirRemap << "/frame_%04d.png "       // Input 1: REMAP
        << " -framerate " << config_.framerate
        << " -i " << tempDirAlpha << "/frame_%04d.png "       // Input 2: ALPHA
        << " -framerate " << config_.framerate
        << " -i " << tempDirComposite << "/frame_%04d.png ";  // Input 3: COMPOSITE
    
    if (!audioPath.empty()) {
        cmd << " -i " << audioPath << " ";  // Input 4: AUDIO
    }
    
    // Mapper toutes les pistes vidéo + audio
    cmd << " -map 0:v -map 1:v -map 2:v -map 3:v ";
    if (!audioPath.empty()) {
        cmd << " -map 4:a ";
    }
    
    // Configurer le codec pour chaque piste vidéo
    cmd << " -c:v:0 " << codecSettings.str()
        << " -c:v:1 " << codecSettings.str()
        << " -c:v:2 " << codecSettings.str()
        << " -c:v:3 " << codecSettings.str();
    
    // Configurer l'audio (PCM ou AAC selon préférence)
    if (!audioPath.empty()) {
        cmd << " -c:a pcm_s16le -ar 48000 -af aresample=resampler=soxr ";
    }
    
    // Métadonnées pour identifier les pistes
    cmd << " -metadata:s:v:0 title=\"BASE - RGB (0-235)\" "
        << " -metadata:s:v:1 title=\"REMAP - RGB (236-254)\" "
        << " -metadata:s:v:2 title=\"ALPHA - Transparency\" "
        << " -metadata:s:v:3 title=\"LUMINANCE - Grayscale Y\" ";
    
    cmd << " -f matroska \"" << outputFile << "\" 2>&1 | tail -5";
    
    fprintf(stderr, "  Encoding 4 video tracks + audio into MKV...\n");
    int result = system(cmd.str().c_str());
    
    if (result != 0) {
        fprintf(stderr, "Error: FFmpeg encoding failed (exit code %d)\n", result);
        return false;
    }
    
    // ========================================================================
    // ÉTAPE 3: Nettoyer les dossiers temporaires
    // ========================================================================
    fprintf(stderr, "\nStep 3/4: Cleaning up temporary files...\n");
    
    std::ostringstream cleanupCmd;
    cleanupCmd << "rm -rf " << tempDirBase << " " << tempDirRemap << " " 
               << tempDirAlpha << " " << tempDirComposite;
    system(cleanupCmd.str().c_str());
    
    fprintf(stderr, "\nStep 4/4: Export complete!\n");
    fprintf(stderr, "✓ MKV file: %s\n", outputFile.c_str());
    fprintf(stderr, "  - Track 0: BASE layer (RGB, pixels 0-235)\n");
    fprintf(stderr, "  - Track 1: REMAP layer (RGB, pixels 236-254)\n");
    fprintf(stderr, "  - Track 2: ALPHA layer (transparency mask)\n");
    fprintf(stderr, "  - Track 3: LUMINANCE layer (grayscale Y)\n");
    if (!audioPath.empty()) {
        fprintf(stderr, "  - Audio: PCM 48 kHz mono\n");
    }
    
    return true;
}

} // namespace RobotExtractor
