#include "robot_mkv_exporter.h"
#include "../core/scummvm_robot_helpers.h"
#include "../include/stb_image_write.h"
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <sys/stat.h>
#include <ctime>

namespace RobotExtractor {

using namespace ScummVMRobot;

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
    // SKIP_COLOR maintenant défini dans scummvm_robot_helpers.h
    
    for (size_t i = 0; i < pixelCount; ++i) {
        uint8_t paletteIndex = pixelIndices[i];
        
        if (isTransparentPixel(paletteIndex)) {
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
    const std::string& audioPath,
    int canvasWidth,
    int canvasHeight
) {
    if (layers.empty()) {
        fprintf(stderr, "Error: no layers to export\n");
        return false;
    }
    
    const size_t numFrames = layers.size();
    
    // Utiliser canvas forcé si fourni, sinon calculer bbox globale réelle
    int maxWidth = canvasWidth;
    int maxHeight = canvasHeight;
    int cropOffsetX = 0, cropOffsetY = 0;  // Offset à retirer (mode tight crop)
    bool isCanvasMode = (canvasWidth > 0 && canvasHeight > 0);
    int globalMinX = 0, globalMinY = 0, globalMaxX = 0, globalMaxY = 0;
    
    if (!isCanvasMode) {
        // Mode tight crop: calculer la bounding box globale de tous les pixels visibles
        globalMinX = INT_MAX;
        globalMinY = INT_MAX;
        globalMaxX = 0;
        globalMaxY = 0;
        
        for (size_t i = 0; i < numFrames; ++i) {
            const RobotLayerFrame& layer = layers[i];
            
            // Trouver bbox de cette frame
            int minX = layer.width, minY = layer.height;
            int maxX = -1, maxY = -1;
            
            for (int y = 0; y < layer.height; ++y) {
                for (int x = 0; x < layer.width; ++x) {
                    size_t idx = y * layer.width + x;
                    if (layer.alpha[idx] > 0) {  // Pixel visible
                        if (x < minX) minX = x;
                        if (x > maxX) maxX = x;
                        if (y < minY) minY = y;
                        if (y > maxY) maxY = y;
                    }
                }
            }
            
            // Mettre à jour bbox globale
            if (maxX >= 0) {  // Si frame non vide
                if (minX < globalMinX) globalMinX = minX;
                if (minY < globalMinY) globalMinY = minY;
                if (maxX > globalMaxX) globalMaxX = maxX;
                if (maxY > globalMaxY) globalMaxY = maxY;
            }
        }
        
        cropOffsetX = globalMinX;
        cropOffsetY = globalMinY;
        maxWidth = globalMaxX - globalMinX + 1;
        maxHeight = globalMaxY - globalMinY + 1;
        
        fprintf(stderr, "Tight crop bounding box: %dx%d (removing offset %d,%d)\n", 
                maxWidth, maxHeight, cropOffsetX, cropOffsetY);
    } else {
        // Mode CANVAS: conserver les dimensions du canvas sans crop
        fprintf(stderr, "Canvas mode: %dx%d (no cropping, full canvas resolution)\n", maxWidth, maxHeight);
    }
    
    const int w = maxWidth;
    const int h = maxHeight;
    
    // Vérifier si des frames ont des résolutions différentes
    bool hasVariableResolution = false;
    for (size_t i = 0; i < numFrames; ++i) {
        if (layers[i].width != w || layers[i].height != h) {
            hasVariableResolution = true;
            fprintf(stderr, "Info: Frame %zu has resolution %dx%d (max is %dx%d)\n", 
                    i, layers[i].width, layers[i].height, w, h);
        }
    }
    
    if (hasVariableResolution) {
        fprintf(stderr, "Info: Video has variable frame sizes - will pad to max resolution %dx%d\n", w, h);
    }
    
    fprintf(stderr, "\n=== Exporting Multi-Track MKV ===\n");
    fprintf(stderr, "Frames: %zu\n", numFrames);
    fprintf(stderr, "Max Resolution: %dx%d\n", w, h);
    fprintf(stderr, "Output: %s.mkv\n\n", outputPath.c_str());
    
    // Créer 4 dossiers temporaires pour les 4 couches
    std::string tempBase = std::string("/tmp/robot_mkv_") + std::to_string(time(nullptr));
    std::string tempDirBase = tempBase + "_base";
    std::string tempDirRemap = tempBase + "_remap";
    std::string tempDirAlpha = tempBase + "_alpha";
    std::string tempDirComposite = tempBase + "_composite";
    
#ifdef _WIN32
    mkdir(tempDirBase.c_str());
    mkdir(tempDirRemap.c_str());
    mkdir(tempDirAlpha.c_str());
    mkdir(tempDirComposite.c_str());
#else
    mkdir(tempDirBase.c_str(), 0755);
    mkdir(tempDirRemap.c_str(), 0755);
    mkdir(tempDirAlpha.c_str(), 0755);
    mkdir(tempDirComposite.c_str(), 0755);
#endif
    
    // ========================================================================
    // ÉTAPE 1: Générer les frames PNG pour chaque couche
    // ========================================================================
    fprintf(stderr, "Step 1/4: Generating PNG frames for 4 layers...\n");
    
    for (size_t frameIdx = 0; frameIdx < numFrames; ++frameIdx) {
        const RobotLayerFrame& layer = layers[frameIdx];
        
        // Utiliser la résolution de CETTE frame
        const int frameWidth = layer.width;
        const int frameHeight = layer.height;
        const size_t framePixelCount = (size_t)frameWidth * (size_t)frameHeight;
        
        // Créer des buffers à la résolution MAXIMALE (avec padding si nécessaire)
        const size_t maxPixelCount = (size_t)w * (size_t)h;
        
        // Couche BASE: RGB complet des pixels 0-235
        std::vector<uint8_t> baseRGB(maxPixelCount * 3, 0);  // Noir par défaut
        // Couche REMAP: RGB complet des pixels 236-254
        std::vector<uint8_t> remapRGB(maxPixelCount * 3, 0);  // Noir par défaut
        // Couche ALPHA: Masque de transparence (pixel 255)
        std::vector<uint8_t> alphaGray(maxPixelCount, 255);  // Transparent par défaut
        // Couche LUMINANCE: Image finale en niveau de gris
        std::vector<uint8_t> luminanceRGB(maxPixelCount * 3, 0);  // Noir par défaut
        
        // Copier les données de la frame en appliquant le crop offset
        for (int y = 0; y < frameHeight; ++y) {
            for (int x = 0; x < frameWidth; ++x) {
                const size_t srcIdx = y * frameWidth + x;
                
                int dstX, dstY;
                if (isCanvasMode) {
                    // Mode canvas: les pixels sont déjà positionnés correctement dans le layer
                    // (le positionnement a été fait lors de la création des frames)
                    dstX = x;
                    dstY = y;
                } else {
                    // Mode tight crop: appliquer le crop offset
                    dstX = x - cropOffsetX;
                    dstY = y - cropOffsetY;
                }
                
                if (dstX < 0 || dstX >= w || dstY < 0 || dstY >= h) {
                    continue;  // Pixel en dehors du canvas/bbox
                }
                
                const size_t dstIdx = dstY * w + dstX;
                
                // BASE: Pixels opaques non-remap (RGB complet)
                if (layer.remap_mask[srcIdx] == 0 && layer.alpha[srcIdx] == 255) {
                    baseRGB[dstIdx * 3 + 0] = layer.base_r[srcIdx];
                    baseRGB[dstIdx * 3 + 1] = layer.base_g[srcIdx];
                    baseRGB[dstIdx * 3 + 2] = layer.base_b[srcIdx];
                }
                
                // REMAP: Pixels de recoloration (RGB complet)
                if (layer.remap_mask[srcIdx] == 255 && layer.alpha[srcIdx] == 255) {
                    remapRGB[dstIdx * 3 + 0] = layer.remap_color_r[srcIdx];
                    remapRGB[dstIdx * 3 + 1] = layer.remap_color_g[srcIdx];
                    remapRGB[dstIdx * 3 + 2] = layer.remap_color_b[srcIdx];
                }
                
                // ALPHA: Transparence (255 = skip, 0 = opaque)
                alphaGray[dstIdx] = (layer.alpha[srcIdx] == 0) ? 255 : 0;
                
                // LUMINANCE: Conversion RGB → Y (ITU-R BT.601)
                uint8_t finalR, finalG, finalB;
                if (layer.alpha[srcIdx] == 0) {
                    // Pixel skip = noir
                    finalR = finalG = finalB = 0;
                } else if (layer.remap_mask[srcIdx] == 255) {
                    // Pixel remap
                    finalR = layer.remap_color_r[srcIdx];
                    finalG = layer.remap_color_g[srcIdx];
                    finalB = layer.remap_color_b[srcIdx];
                } else {
                    // Pixel base
                    finalR = layer.base_r[srcIdx];
                    finalG = layer.base_g[srcIdx];
                    finalB = layer.base_b[srcIdx];
                }
                
                // Formule de luminance standard (BT.601)
                uint8_t Y = (uint8_t)(0.299f * finalR + 0.587f * finalG + 0.114f * finalB);
                luminanceRGB[dstIdx * 3 + 0] = Y;
                luminanceRGB[dstIdx * 3 + 1] = Y;
                luminanceRGB[dstIdx * 3 + 2] = Y;
            }
        }
        
        // Écrire les 4 PNG à la résolution MAXIMALE (avec padding si nécessaire)
        char filename[512];
        
        snprintf(filename, sizeof(filename), "%s/frame_%04zu.png", tempDirBase.c_str(), frameIdx);
        int result = stbi_write_png(filename, w, h, 3, baseRGB.data(), w * 3);
        if (!result) {
            fprintf(stderr, "\nError: stbi_write_png failed for base layer (frame %zu)\n", frameIdx);
            fprintf(stderr, "       File: %s\n", filename);
            fprintf(stderr, "       Max Resolution: %dx%d, Frame Resolution: %dx%d\n", w, h, frameWidth, frameHeight);
            return false;
        }
        
        snprintf(filename, sizeof(filename), "%s/frame_%04zu.png", tempDirRemap.c_str(), frameIdx);
        result = stbi_write_png(filename, w, h, 3, remapRGB.data(), w * 3);
        if (!result) {
            fprintf(stderr, "\nError: stbi_write_png failed for remap layer (frame %zu)\n", frameIdx);
            fprintf(stderr, "       File: %s\n", filename);
            return false;
        }
        
        snprintf(filename, sizeof(filename), "%s/frame_%04zu.png", tempDirAlpha.c_str(), frameIdx);
        result = stbi_write_png(filename, w, h, 1, alphaGray.data(), w);
        if (!result) {
            fprintf(stderr, "\nError: stbi_write_png failed for alpha layer (frame %zu)\n", frameIdx);
            fprintf(stderr, "       File: %s\n", filename);
            return false;
        }
        
        snprintf(filename, sizeof(filename), "%s/frame_%04zu.png", tempDirComposite.c_str(), frameIdx);
        result = stbi_write_png(filename, w, h, 3, luminanceRGB.data(), w * 3);
        if (!result) {
            fprintf(stderr, "\nError: stbi_write_png failed for luminance layer (frame %zu)\n", frameIdx);
            fprintf(stderr, "       File: %s\n", filename);
            return false;
        }
        
        // Libérer explicitement la mémoire des buffers temporaires
        baseRGB.clear();
        baseRGB.shrink_to_fit();
        remapRGB.clear();
        remapRGB.shrink_to_fit();
        alphaGray.clear();
        alphaGray.shrink_to_fit();
        luminanceRGB.clear();
        luminanceRGB.shrink_to_fit();
        
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
    
#ifdef _WIN32
    cmd << " -f matroska \"" << outputFile << "\" 2>nul";
#else
    cmd << " -f matroska \"" << outputFile << "\" 2>&1 | tail -5";
#endif
    
    fprintf(stderr, "  Encoding 4 video tracks + audio into MKV...\n");
    int result = system(cmd.str().c_str());
    
    if (result != 0) {
        fprintf(stderr, "Error: FFmpeg encoding failed (exit code %d)\n", result);
        return false;
    }
    
    // ========================================================================
    // ÉTAPE 2bis: Générer MOV ProRes 4444 RGBA (composite avec alpha)
    // ========================================================================
    fprintf(stderr, "\nStep 2bis/4: Generating ProRes 4444 MOV with alpha...\n");
    
    // Extraire le dossier de sortie depuis outputPath (ex: "output/1000/1000_video" -> "output/1000/1000_frames")
    std::string outputDir = outputPath.substr(0, outputPath.find_last_of("/\\"));
    size_t lastSlash = outputPath.find_last_of("/\\");
    size_t videoPos = outputPath.rfind("_video");
    std::string robotName = outputPath.substr(lastSlash + 1, videoPos - lastSlash - 1);
    std::string framesDir = outputDir + "/" + robotName + "_frames";
    
    fprintf(stderr, "  Saving PNG RGBA frames to: %s\n", framesDir.c_str());
    
    // Créer le dossier frames s'il n'existe pas déjà
#ifdef _WIN32
    std::string mkdirCmd = "if not exist \"" + framesDir + "\" mkdir \"" + framesDir + "\"";
    system(mkdirCmd.c_str());
#else
    mkdir(framesDir.c_str(), 0755);
#endif
    
    for (size_t i = 0; i < numFrames; ++i) {
        const RobotLayerFrame& layer = layers[i];
        const int frameWidth = layer.width;
        const int frameHeight = layer.height;
        
        // Créer image RGBA avec padding au canvas si nécessaire
        const size_t canvasPixelCount = (size_t)w * (size_t)h;
        std::vector<uint8_t> rgbaImage(canvasPixelCount * 4, 0);  // Noir transparent par défaut
        
        // Copier pixels de la frame vers le canvas
        for (int y = 0; y < frameHeight; ++y) {
            for (int x = 0; x < frameWidth; ++x) {
                const size_t srcIdx = y * frameWidth + x;
                
                int dstX, dstY;
                if (isCanvasMode) {
                    // Mode canvas: les pixels sont déjà positionnés correctement dans le layer
                    dstX = x;
                    dstY = y;
                } else {
                    // Mode tight crop: appliquer le crop offset
                    dstX = x - cropOffsetX;
                    dstY = y - cropOffsetY;
                }
                
                if (dstX < 0 || dstX >= w || dstY < 0 || dstY >= h) {
                    continue;  // Pixel en dehors du canvas/bbox
                }
                
                const size_t dstIdx = dstY * w + dstX;
                
                if (layer.alpha[srcIdx] == 0) {
                    // Transparent (skip pixel 255)
                    rgbaImage[dstIdx * 4 + 0] = 0;
                    rgbaImage[dstIdx * 4 + 1] = 0;
                    rgbaImage[dstIdx * 4 + 2] = 0;
                    rgbaImage[dstIdx * 4 + 3] = 0;
                } else if (layer.remap_mask[srcIdx] == 255) {
                    // Pixel remap (236-254)
                    rgbaImage[dstIdx * 4 + 0] = layer.remap_color_r[srcIdx];
                    rgbaImage[dstIdx * 4 + 1] = layer.remap_color_g[srcIdx];
                    rgbaImage[dstIdx * 4 + 2] = layer.remap_color_b[srcIdx];
                    rgbaImage[dstIdx * 4 + 3] = 255;
                } else {
                    // Pixel base (0-235)
                    rgbaImage[dstIdx * 4 + 0] = layer.base_r[srcIdx];
                    rgbaImage[dstIdx * 4 + 1] = layer.base_g[srcIdx];
                    rgbaImage[dstIdx * 4 + 2] = layer.base_b[srcIdx];
                    rgbaImage[dstIdx * 4 + 3] = 255;
                }
            }
        }
        
        // Sauvegarder en PNG RGBA dans le dossier frames de sortie
        char framePath[512];
        snprintf(framePath, sizeof(framePath), "%s/frame_%04zu.png", framesDir.c_str(), i);
        if (!stbi_write_png(framePath, w, h, 4, rgbaImage.data(), w * 4)) {
            fprintf(stderr, "Warning: Failed to write PNG frame %zu\n", i);
        }
    }
    
    // Générer MOV ProRes 4444 avec canal alpha
    std::string movFile = outputPath + ".mov";
    std::ostringstream movCmd;
    movCmd << "ffmpeg -y -loglevel error -framerate " << config_.framerate
           << " -start_number 0 -i " << framesDir << "/frame_%04d.png";
    
    if (!audioPath.empty()) {
        movCmd << " -i \"" << audioPath << "\"";
    }
    
    movCmd << " -c:v prores_ks -profile:v 4444 -pix_fmt yuva444p10le -vendor apl0";
    
    if (!audioPath.empty()) {
        movCmd << " -c:a pcm_s16le -shortest";
    }
    
    movCmd << " \"" << movFile << "\"";
    
    int movResult = system(movCmd.str().c_str());
    if (movResult == 0) {
        fprintf(stderr, "✓ MOV file: %s\n", movFile.c_str());
    } else {
        fprintf(stderr, "⚠ Warning: MOV generation failed\n");
    }
    
    // ========================================================================
    // ÉTAPE 3: Nettoyer les dossiers temporaires
    // ========================================================================
    fprintf(stderr, "\nStep 3/4: Cleaning up temporary files...\n");
    
    std::ostringstream cleanupCmd;
#ifdef _WIN32
    cleanupCmd << "rd /s /q \"" << tempDirBase << "\" \"" << tempDirRemap << "\" "
               << "\"" << tempDirAlpha << "\" \"" << tempDirComposite << "\" 2>nul";
#else
    cleanupCmd << "rm -rf " << tempDirBase << " " << tempDirRemap << " " 
               << tempDirAlpha << " " << tempDirComposite;
#endif
    system(cleanupCmd.str().c_str());
    
    fprintf(stderr, "\n=========================================\n");
    fprintf(stderr, "Step 4/4: Export complete! ✓\n");
    fprintf(stderr, "=========================================\n");
    fprintf(stderr, "Video Resolution: %d×%d\n", w, h);
    fprintf(stderr, "Number of Frames: %zu\n", numFrames);
    fprintf(stderr, "Frame Rate:       %d fps\n", config_.framerate);
    fprintf(stderr, "Audio Track:      %s\n", audioPath.empty() ? "None" : "Included (PCM 16-bit)");
    fprintf(stderr, "\nMKV Tracks:\n");
    fprintf(stderr, "  • Track 0 (BASE):      RGB base layer (pixels 0-235)\n");
    fprintf(stderr, "  • Track 1 (REMAP):     RGB remap layer (pixels 236-254)\n");
    fprintf(stderr, "  • Track 2 (ALPHA):     Binary transparency mask (skip pixel 255)\n");
    fprintf(stderr, "  • Track 3 (LUMINANCE): Grayscale Y layer\n");
    if (!audioPath.empty()) {
        fprintf(stderr, "  • Audio Track:     PCM 48 kHz mono\n");
    }
    fprintf(stderr, "\nMOV ProRes 4444:\n");
    fprintf(stderr, "  • File:     %s\n", movFile.c_str());
    fprintf(stderr, "  • Codec:    Apple ProRes 4444 (profile 4)\n");
    fprintf(stderr, "  • Alpha:    yuva444p10le (10-bit with transparency)\n");
    fprintf(stderr, "  • Combined: RGB composite + alpha channel\n");
    fprintf(stderr, "=========================================\n");
    
    return true;
}

} // namespace RobotExtractor
