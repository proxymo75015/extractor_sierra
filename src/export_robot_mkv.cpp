/**
 * Programme d'export vidéo Robot en MKV multi-couches (mode batch)
 * 
 * Scanne automatiquement le répertoire RBT/ et traite tous les fichiers .RBT
 * Les résultats sont placés dans output/<rbt_name>/ avec:
 *   - <rbt>_video.mkv (MKV 4 pistes + audio)
 *   - <rbt>_audio.wav (PCM 22 kHz natif)
 *   - <rbt>_composite.mov (vidéo composite ProRes 4444 RGBA + audio)
 *   - <rbt>_metadata.txt (métadonnées)
 *   - <rbt>_frames/ (frames PNG individuelles)
 * 
 * Usage:
 *   export_robot_mkv [codec] [--canvas WIDTHxHEIGHT]
 * 
 * Codecs supportés:
 *   h264  - x264 (défaut, universel)
 *   h265  - x265 (meilleure compression)
 *   vp9   - VP9 (open source, excellente qualité)
 *   ffv1  - FFV1 (lossless, archivage)
 * 
 * Options:
 *   --canvas WIDTHxHEIGHT  - Forcer taille du canvas (ex: 640x480)
 *                            Si non spécifié, détection automatique
 */

#include "core/rbt_parser.h"
#include "formats/robot_mkv_exporter.h"
#include "utils/sci_util.h"
#include "../include/stb_image_write.h"
#include <cstring>
#include <sys/stat.h>
#include <climits>
#include <ctime>
#include <dirent.h>
#include <vector>
#include <string>
#include <algorithm>

using namespace RobotExtractor;

// Fonction pour lister tous les fichiers .RBT dans un répertoire
std::vector<std::string> findRbtFiles(const std::string& directory) {
    std::vector<std::string> rbtFiles;
    DIR* dir = opendir(directory.c_str());
    
    if (!dir) {
        return rbtFiles;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        
        // Vérifier extension .RBT (insensible à la casse)
        if (filename.length() > 4) {
            std::string ext = filename.substr(filename.length() - 4);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
            
            if (ext == ".RBT") {
                rbtFiles.push_back(directory + "/" + filename);
            }
        }
    }
    
    closedir(dir);
    std::sort(rbtFiles.begin(), rbtFiles.end());
    return rbtFiles;
}

// Fonction pour détecter automatiquement la résolution du jeu
void detectCanvasSize(int contentWidth, int contentHeight, int& canvasWidth, int& canvasHeight) {
    // Résolutions standard des jeux Sierra SCI32
    struct Resolution {
        int width, height;
        const char* name;
    };
    
    const Resolution standards[] = {
        {630, 450, "Phantasmagoria (630x450)"},
        {640, 480, "VGA (640x480)"},
        {640, 400, "VGA (640x400)"},
        {320, 240, "QVGA (320x240)"},
        {320, 200, "CGA (320x200)"},
    };
    
    // Choisir la plus petite résolution standard qui englobe le contenu
    for (const auto& res : standards) {
        if (contentWidth <= res.width && contentHeight <= res.height) {
            canvasWidth = res.width;
            canvasHeight = res.height;
            fprintf(stderr, "Auto-detected canvas: %s (content fits in %dx%d)\n", 
                    res.name, contentWidth, contentHeight);
            return;
        }
    }
    
    // Si aucune résolution standard ne convient, utiliser le contenu exact
    canvasWidth = contentWidth;
    canvasHeight = contentHeight;
    fprintf(stderr, "Canvas: %dx%d (exact content size, no standard resolution detected)\n",
            canvasWidth, canvasHeight);
}

// Fonction pour traiter un seul fichier RBT
bool processRbtFile(const std::string& inputPath, const std::string& outputDir, 
                    const char* codecName, MKVExportConfig::Codec codec,
                    int forceCanvasWidth = 0, int forceCanvasHeight = 0) {
    
    // Ouvrir le fichier Robot
    FILE* f = fopen(inputPath.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s\n", inputPath.c_str());
        return false;
    }
    
    RbtParser parser(f);
    
    // Parser l'en-tête
    if (!parser.parseHeader()) {
        fprintf(stderr, "Error: Failed to parse Robot header\n");
        fclose(f);
        return false;
    }
    
    size_t numFrames = parser.getNumFrames();
    int frameRate = parser.getFrameRate();
    bool hasAudio = parser.hasAudio();
    
    fprintf(stderr, "\nFrames: %zu @ %d fps (%.2f seconds)\n", 
            numFrames, frameRate, (double)numFrames / frameRate);
    fprintf(stderr, "Audio: %s\n", hasAudio ? "yes" : "no");
    
    // Configuration MKV
    MKVExportConfig config;
    config.framerate = frameRate;
    config.codec = codec;
    
    // Extraire le nom de base du fichier RBT (sans extension et chemin)
    std::string inputFilename = inputPath;
    size_t lastSlash = inputFilename.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        inputFilename = inputFilename.substr(lastSlash + 1);
    }
    size_t lastDot = inputFilename.find_last_of(".");
    if (lastDot != std::string::npos) {
        inputFilename = inputFilename.substr(0, lastDot);
    }
    
    // Chemins de sortie
    std::string mkvPath = outputDir + "/" + inputFilename + "_video";
    std::string wavPath = outputDir + "/" + inputFilename + "_audio.wav";
    std::string movPath = outputDir + "/" + inputFilename + "_composite.mov";
    std::string metadataPath = outputDir + "/" + inputFilename + "_metadata.txt";
    std::string framesDir = outputDir + "/" + inputFilename + "_frames";
    
    // Créer le sous-répertoire pour les frames
#ifdef _WIN32
    mkdir(framesDir.c_str());
#else
    mkdir(framesDir.c_str(), 0755);
#endif
    
    // Extraire la palette globale
    std::vector<uint8_t> globalPalette = parser.getPalette();
    
    if (globalPalette.empty()) {
        fprintf(stderr, "Error: No palette found\n");
        fclose(f);
        return false;
    }
    
    // Extraire toutes les frames et les décomposer en couches
    fprintf(stderr, "Extracting %zu frames...\n", numFrames);
    
    std::vector<RobotLayerFrame> allLayers;
    allLayers.reserve(numFrames);
    
    for (size_t i = 0; i < numFrames; ++i) {
        // Extraire les pixels indexés
        std::vector<uint8_t> pixelIndices;
        int width, height;
        
        if (!parser.extractFramePixels(i, pixelIndices, width, height)) {
            fprintf(stderr, "Error: Failed to extract frame %zu\n", i);
            continue;
        }
        
        // Décomposer en couches (avec gestion d'erreur pour allocations)
        try {
            RobotLayerFrame layer = decomposeRobotFrame(pixelIndices, globalPalette, width, height);
            allLayers.push_back(std::move(layer));
        } catch (const std::bad_alloc& e) {
            fprintf(stderr, "Error: Memory allocation failed for frame %zu (%dx%d)\n", i, width, height);
            fprintf(stderr, "       Try processing a smaller subset of frames or reduce resolution\n");
            fclose(f);
            return false;
        } catch (const std::exception& e) {
            fprintf(stderr, "Error: Exception while processing frame %zu: %s\n", i, e.what());
            continue;
        }
        
        if ((i + 1) % 10 == 0 || i == numFrames - 1) {
            fprintf(stderr, "\r  Extracting frame %zu/%zu...", i + 1, numFrames);
            fflush(stderr);
        }
    }
    fprintf(stderr, "\n");
    
    // Calculer dimensions du contenu (max des frames)
    int contentWidth = 0, contentHeight = 0;
    for (const auto& layer : allLayers) {
        if (layer.width > contentWidth) contentWidth = layer.width;
        if (layer.height > contentHeight) contentHeight = layer.height;
    }
    fprintf(stderr, "Content Resolution: %dx%d\n", contentWidth, contentHeight);
    
    // Déterminer taille finale du canvas
    int canvasWidth, canvasHeight;
    if (forceCanvasWidth > 0 && forceCanvasHeight > 0) {
        // Utiliser la taille forcée en ligne de commande
        canvasWidth = forceCanvasWidth;
        canvasHeight = forceCanvasHeight;
        fprintf(stderr, "Canvas (forced): %dx%d\n", canvasWidth, canvasHeight);
        
        if (contentWidth > canvasWidth || contentHeight > canvasHeight) {
            fprintf(stderr, "Warning: Content (%dx%d) exceeds canvas (%dx%d), will be clipped!\n",
                    contentWidth, contentHeight, canvasWidth, canvasHeight);
        }
    } else {
        // Détection automatique
        detectCanvasSize(contentWidth, contentHeight, canvasWidth, canvasHeight);
    }
    
    const int maxWidth = canvasWidth;
    const int maxHeight = canvasHeight;
    
    // Extraire l'audio
    if (hasAudio) {
        fprintf(stderr, "Extracting audio...\n");
        parser.extractAudio(wavPath);
        fprintf(stderr, "  ✓ Audio: %s\n", wavPath.c_str());
    }
    
    // Exporter en MKV multi-couches
    fprintf(stderr, "Encoding MKV (%s)...\n", codecName);
    
    RobotMKVExporter exporter(config);
    if (!exporter.exportMultiTrack(allLayers, mkvPath, hasAudio ? wavPath : "")) {
        fprintf(stderr, "Error: Multi-track MKV export failed\n");
        fclose(f);
        return false;
    }
    fprintf(stderr, "  ✓ MKV: %s.mkv\n", mkvPath.c_str());
    
    // Regénérer les frames RGBA composites pour l'export MOV avec dimensions fixes
    fprintf(stderr, "Normalizing frames to %dx%d canvas (ScummVM-compatible positioning)...\n", 
            maxWidth, maxHeight);
    
    for (size_t i = 0; i < allLayers.size(); ++i) {
        const RobotLayerFrame& layer = allLayers[i];
        const int w = layer.width;
        const int h = layer.height;
        
        // IMPORTANT: Les pixels dans layer sont déjà positionnés correctement par extractFramePixels
        // Le buffer layer contient un canvas complet avec padding (positions celX/celY respectées).
        // Si maxWidth/maxHeight sont identiques à w/h, simple copie 1:1
        // Si maxWidth/maxHeight sont plus grands, on copie le canvas source tel quel (préserve positions)
        const size_t paddedPixelCount = (size_t)maxWidth * (size_t)maxHeight;
        std::vector<uint8_t> rgbaImage(paddedPixelCount * 4, 0);  // Init noir transparent
        
        // Copier le canvas source vers le canvas final (copie 1:1 préservant toutes les positions)
        for (int y = 0; y < h && y < maxHeight; ++y) {
            for (int x = 0; x < w && x < maxWidth; ++x) {
                const size_t srcIdx = y * w + x;
                const size_t dstIdx = y * maxWidth + x;
                
                // Composer l'image finale avec transparence
                if (layer.alpha[srcIdx] == 0) {
                    // Pixel transparent (skip) - déjà à 0 par défaut
                    rgbaImage[dstIdx * 4 + 0] = 0;
                    rgbaImage[dstIdx * 4 + 1] = 0;
                    rgbaImage[dstIdx * 4 + 2] = 0;
                    rgbaImage[dstIdx * 4 + 3] = 0;
                } else if (layer.remap_mask[srcIdx] == 255) {
                    // Pixel remap (opaque avec couleur recolorée)
                    rgbaImage[dstIdx * 4 + 0] = layer.remap_color_r[srcIdx];
                    rgbaImage[dstIdx * 4 + 1] = layer.remap_color_g[srcIdx];
                    rgbaImage[dstIdx * 4 + 2] = layer.remap_color_b[srcIdx];
                    rgbaImage[dstIdx * 4 + 3] = 255;
                } else {
                    // Pixel base (opaque avec couleur fixe palette)
                    rgbaImage[dstIdx * 4 + 0] = layer.base_r[srcIdx];
                    rgbaImage[dstIdx * 4 + 1] = layer.base_g[srcIdx];
                    rgbaImage[dstIdx * 4 + 2] = layer.base_b[srcIdx];
                    rgbaImage[dstIdx * 4 + 3] = 255;
                }
            }
        }
        
        // Sauvegarder en PNG RGBA avec dimensions fixes
        char framePath[512];
        snprintf(framePath, sizeof(framePath), "%s/frame_%04zu.png", framesDir.c_str(), i);
        if (!stbi_write_png(framePath, maxWidth, maxHeight, 4, rgbaImage.data(), maxWidth * 4)) {
            fprintf(stderr, "Warning: Failed to write composite frame %zu\n", i);
        }
        
        if ((i + 1) % 10 == 0 || i == allLayers.size() - 1) {
            fprintf(stderr, "\r  Composite frame %zu/%zu...", i + 1, allLayers.size());
            fflush(stderr);
        }
    }
    fprintf(stderr, "\n");
    
    // Vérifier que les frames ont été générées
    char firstFrame[512];
    snprintf(firstFrame, sizeof(firstFrame), "%s/frame_0000.png", framesDir.c_str());
    FILE* checkFrame = fopen(firstFrame, "rb");
    if (!checkFrame) {
        fprintf(stderr, "ERROR: First frame not found: %s\n", firstFrame);
        fprintf(stderr, "Cannot generate MOV without frames!\n");
    } else {
        fclose(checkFrame);
        fprintf(stderr, "✓ Frames verified in: %s\n", framesDir.c_str());
    }
    
    // Générer vidéo composite MOV avec ProRes 4444 (support transparence)
    fprintf(stderr, "Generating composite MOV (ProRes 4444 with alpha)...\n");
    std::string ffmpegCmd = "ffmpeg -y -v verbose";
    ffmpegCmd += " -start_number 0 -framerate " + std::to_string(frameRate);
    ffmpegCmd += " -i \"" + framesDir + "/frame_%04d.png\"";
    if (hasAudio) {
        ffmpegCmd += " -i \"" + wavPath + "\"";
    }
    // ProRes 4444 : support natif RGBA avec transparence
    ffmpegCmd += " -c:v prores_ks -profile:v 4444 -pix_fmt yuva444p10le";
    if (hasAudio) {
        ffmpegCmd += " -c:a pcm_s16le";  // Audio PCM lossless dans MOV
    }
    ffmpegCmd += " \"" + movPath + "\"";
    
    fprintf(stderr, "\nFFmpeg command:\n%s\n\n", ffmpegCmd.c_str());
    int ret = system(ffmpegCmd.c_str());
    if (ret == 0) {
        fprintf(stderr, "  ✓ MOV: %s (ProRes 4444 RGBA)\n", movPath.c_str());
    } else {
        fprintf(stderr, "  ⚠ Warning: MOV generation failed (check FFmpeg ProRes support)\n");
    }
    
    // Générer le fichier de métadonnées
    fprintf(stderr, "Writing metadata...\n");
    FILE* metaFile = fopen(metadataPath.c_str(), "w");
    if (metaFile) {
        fprintf(metaFile, "=== Robot Video Metadata ===\n\n");
        fprintf(metaFile, "Source File: %s\n", inputPath.c_str());
        fprintf(metaFile, "Format: Sierra Robot Video (v5/v6)\n");
        fprintf(metaFile, "Platform: PC\n\n");
        
        fprintf(metaFile, "Video:\n");
        fprintf(metaFile, "  Frames: %zu\n", numFrames);
        fprintf(metaFile, "  Frame Rate: %d fps\n", frameRate);
        fprintf(metaFile, "  Duration: %.2f seconds\n", (double)numFrames / frameRate);
        fprintf(metaFile, "  Canvas Resolution: %dx%d\n", canvasWidth, canvasHeight);
        
        if (!allLayers.empty()) {
            fprintf(metaFile, "  Content Resolution: %dx%d\n", contentWidth, contentHeight);
        }
        
        fprintf(metaFile, "  Codec: %s\n\n", codecName);
        
        fprintf(metaFile, "Audio:\n");
        if (hasAudio) {
            fprintf(metaFile, "  Present: Yes\n");
            fprintf(metaFile, "  Sample Rate: 48000 Hz (resampled from 22050 Hz)\n");
            fprintf(metaFile, "  Channels: 1 (mono)\n");
            fprintf(metaFile, "  Format: PCM 16-bit\n");
        } else {
            fprintf(metaFile, "  Present: No\n");
        }
        
        fprintf(metaFile, "\nPalette:\n");
        fprintf(metaFile, "  Colors: %zu\n", globalPalette.size() / 3);
        fprintf(metaFile, "  Format: RGB (256 color indexed)\n\n");
        
        fprintf(metaFile, "Pixel Classification:\n");
        fprintf(metaFile, "  Type 1 (Base): Indices 0-235 (fixed opaque colors)\n");
        fprintf(metaFile, "  Type 2 (Remap): Indices 236-254 (recolorable zones)\n");
        fprintf(metaFile, "  Type 3 (Skip): Index 255 (transparent)\n\n");
        
        fprintf(metaFile, "\nOutput Files:\n");
        fprintf(metaFile, "  %s_video.mkv - Matroska with 4 video tracks + audio\n", inputFilename.c_str());
        fprintf(metaFile, "    * Track 0: BASE layer (pixels 0-235, RGB)\n");
        fprintf(metaFile, "    * Track 1: REMAP layer (pixels 236-254, RGB)\n");
        fprintf(metaFile, "    * Track 2: ALPHA layer (pixel 255, transparency mask)\n");
        fprintf(metaFile, "    * Track 3: LUMINANCE (grayscale Y)\n");
        fprintf(metaFile, "    * Audio: PCM 48 kHz mono\n");
        fprintf(metaFile, "  %s_audio.wav - PCM WAV 22 kHz (native quality)\n", inputFilename.c_str());
        fprintf(metaFile, "  %s_composite.mov - ProRes 4444 RGBA with alpha + PCM audio\n", inputFilename.c_str());
        fprintf(metaFile, "  %s_metadata.txt - This file\n\n", inputFilename.c_str());
        
        time_t now = time(nullptr);
        fprintf(metaFile, "Export Date: %s", ctime(&now));
        
        fclose(metaFile);
        fprintf(stderr, "  ✓ Metadata: %s\n", metadataPath.c_str());
    }
    
    fclose(f);
    return true;
}

int main(int argc, char* argv[]) {
    const char* codecStr = "h264";
    int forceCanvasWidth = 0;
    int forceCanvasHeight = 0;
    
    // Parser les arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--canvas") == 0 && i + 1 < argc) {
            // Parser WIDTHxHEIGHT
            if (sscanf(argv[i + 1], "%dx%d", &forceCanvasWidth, &forceCanvasHeight) == 2) {
                fprintf(stderr, "Canvas size override: %dx%d\n", forceCanvasWidth, forceCanvasHeight);
                i++; // Skip le prochain argument
            } else {
                fprintf(stderr, "Error: Invalid canvas format '%s'. Use WIDTHxHEIGHT (e.g., 640x480)\n", argv[i + 1]);
                return 1;
            }
        } else if (argv[i][0] != '-') {
            // Codec name
            codecStr = argv[i];
        }
    }
    
    // Déterminer le codec
    MKVExportConfig::Codec codec;
    if (strcmp(codecStr, "h265") == 0) {
        codec = MKVExportConfig::Codec::H265;
    } else if (strcmp(codecStr, "vp9") == 0) {
        codec = MKVExportConfig::Codec::VP9;
    } else if (strcmp(codecStr, "ffv1") == 0) {
        codec = MKVExportConfig::Codec::FFV1;
    } else {
        codec = MKVExportConfig::Codec::H264;
        codecStr = "h264";
    }
    
    fprintf(stderr, "\n=== Robot Video Batch Export ===\n");
    fprintf(stderr, "Version: 2.5.0 (2024-12-04) - ScummVM Canvas Auto-Detect\n");
    fprintf(stderr, "Codec: %s\n", codecStr);
    if (forceCanvasWidth > 0 && forceCanvasHeight > 0) {
        fprintf(stderr, "Canvas: %dx%d (forced)\n", forceCanvasWidth, forceCanvasHeight);
    } else {
        fprintf(stderr, "Canvas: Auto-detect (standard game resolutions)\n");
    }
    fprintf(stderr, "\n");
    
    // Vérifier si FFmpeg est disponible
    fprintf(stderr, "\nChecking FFmpeg availability...\n");
#ifdef _WIN32
    int ffmpegCheck = system("ffmpeg -version >nul 2>&1");
#else
    int ffmpegCheck = system("ffmpeg -version >/dev/null 2>&1");
#endif
    if (ffmpegCheck != 0) {
        fprintf(stderr, "\n");
        fprintf(stderr, "========================================\n");
        fprintf(stderr, "ERROR: FFmpeg is not installed or not in PATH!\n");
        fprintf(stderr, "========================================\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "This program requires FFmpeg to create MKV and MP4 files.\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Please install FFmpeg:\n");
        fprintf(stderr, "  Windows: https://ffmpeg.org/download.html#build-windows\n");
        fprintf(stderr, "  Linux:   sudo apt install ffmpeg\n");
        fprintf(stderr, "  macOS:   brew install ffmpeg\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Without FFmpeg, only WAV audio files will be generated.\n");
        fprintf(stderr, "\n");
        return 1;
    }
    fprintf(stderr, "FFmpeg found!\n");
    
    // Chercher le répertoire RBT (essayer RBT/ puis RBT_test/)
    std::vector<std::string> rbtFiles;
    std::string rbtDir;
    
    // Essayer RBT/
    rbtFiles = findRbtFiles("RBT");
    if (!rbtFiles.empty()) {
        rbtDir = "RBT";
    } else {
        // Essayer RBT_test/
        rbtFiles = findRbtFiles("RBT_test");
        if (!rbtFiles.empty()) {
            rbtDir = "RBT_test";
        }
    }
    
    if (rbtFiles.empty()) {
        fprintf(stderr, "\nError: No .RBT files found in RBT/ or RBT_test/ directory\n");
        fprintf(stderr, "Please create a 'RBT' directory and place your .RBT files there.\n");
        return 1;
    }
    
    fprintf(stderr, "Scanning %s/...\n\n", rbtDir.c_str());
    fprintf(stderr, "Found %zu RBT file(s):\n", rbtFiles.size());
    for (const auto& file : rbtFiles) {
        fprintf(stderr, "  - %s\n", file.c_str());
    }
    fprintf(stderr, "\n");
    
    // Créer le répertoire output/
#ifdef _WIN32
    mkdir("output");
#else
    mkdir("output", 0755);
#endif
    
    // Traiter chaque fichier
    size_t successCount = 0;
    size_t failCount = 0;
    
    for (size_t i = 0; i < rbtFiles.size(); ++i) {
        const std::string& inputPath = rbtFiles[i];
        
        // Extraire le nom de base du fichier (sans extension)
        std::string filename = inputPath;
        size_t lastSlash = filename.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            filename = filename.substr(lastSlash + 1);
        }
        size_t lastDot = filename.find_last_of(".");
        if (lastDot != std::string::npos) {
            filename = filename.substr(0, lastDot);
        }
        
        // Créer le sous-répertoire output/<rbt_name>/
        std::string fileOutputDir = "output/" + filename;
#ifdef _WIN32
        mkdir(fileOutputDir.c_str());
#else
        mkdir(fileOutputDir.c_str(), 0755);
#endif
        
        fprintf(stderr, "\n========================================\n");
        fprintf(stderr, "Processing [%zu/%zu]: %s\n", i + 1, rbtFiles.size(), filename.c_str());
        fprintf(stderr, "========================================\n");
        
        // Traiter le fichier
        if (processRbtFile(inputPath, fileOutputDir, codecStr, codec, forceCanvasWidth, forceCanvasHeight)) {
            successCount++;
            fprintf(stderr, "✓ SUCCESS: %s\n", filename.c_str());
        } else {
            failCount++;
            fprintf(stderr, "✗ FAILED: %s\n", filename.c_str());
        }
    }
    
    // Résumé final
    fprintf(stderr, "\n========================================\n");
    fprintf(stderr, "=== Batch Export Complete ===\n");
    fprintf(stderr, "========================================\n");
    fprintf(stderr, "Total files: %zu\n", rbtFiles.size());
    fprintf(stderr, "  Success: %zu\n", successCount);
    fprintf(stderr, "  Failed: %zu\n", failCount);
    fprintf(stderr, "\nAll outputs saved to: output/\n");
    
    return (failCount > 0) ? 1 : 0;
}
