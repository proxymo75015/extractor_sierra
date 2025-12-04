/**
 * Programme d'export vidéo Robot en MKV multi-couches (mode batch)
 * 
 * Scanne automatiquement le répertoire RBT/ et traite tous les fichiers .RBT
 * Les résultats sont placés dans output/<rbt_name>/ avec:
 *   - <rbt>_video.mkv (MKV 4 pistes + audio)
 *   - <rbt>_audio.wav (PCM 22 kHz natif)
 *   - <rbt>_composite.mp4 (vidéo composite H.264 + audio)
 *   - <rbt>_metadata.txt (métadonnées)
 *   - <rbt>_frames/ (frames PNG individuelles)
 * 
 * Usage:
 *   export_robot_mkv [codec]
 * 
 * Codecs supportés:
 *   h264  - x264 (défaut, universel)
 *   h265  - x265 (meilleure compression)
 *   vp9   - VP9 (open source, excellente qualité)
 *   ffv1  - FFV1 (lossless, archivage)
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

// Fonction pour traiter un seul fichier RBT
bool processRbtFile(const std::string& inputPath, const std::string& outputDir, 
                    const char* codecName, MKVExportConfig::Codec codec) {
    
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
    std::string mp4Path = outputDir + "/" + inputFilename + "_composite.mp4";
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
        
        // Sauvegarder la frame composite en PNG
        char framePath[512];
        snprintf(framePath, sizeof(framePath), "%s/frame_%04zu.png", framesDir.c_str(), i);
        
        // Créer image composite RGBA avec transparence
        std::vector<uint8_t> rgbaImage;
        try {
            rgbaImage.resize((size_t)width * (size_t)height * 4);
        } catch (const std::bad_alloc& e) {
            fprintf(stderr, "Warning: Cannot allocate RGBA buffer for frame %zu, skipping PNG export\n", i);
            continue;
        }
        
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                size_t pixelIdx = y * width + x;
                uint8_t paletteIndex = pixelIndices[pixelIdx];
                
                if (paletteIndex == 255) {
                    // Pixel transparent (skip) = transparent
                    rgbaImage[pixelIdx * 4 + 0] = 0;
                    rgbaImage[pixelIdx * 4 + 1] = 0;
                    rgbaImage[pixelIdx * 4 + 2] = 0;
                    rgbaImage[pixelIdx * 4 + 3] = 0;  // Alpha = 0 (transparent)
                } else {
                    // Couleur depuis la palette (opaque)
                    rgbaImage[pixelIdx * 4 + 0] = globalPalette[paletteIndex * 3 + 0];
                    rgbaImage[pixelIdx * 4 + 1] = globalPalette[paletteIndex * 3 + 1];
                    rgbaImage[pixelIdx * 4 + 2] = globalPalette[paletteIndex * 3 + 2];
                    rgbaImage[pixelIdx * 4 + 3] = 255;  // Alpha = 255 (opaque)
                }
            }
        }
        
        if (!stbi_write_png(framePath, width, height, 4, rgbaImage.data(), width * 4)) {
            fprintf(stderr, "Warning: Failed to write frame %zu to %s\n", i, framePath);
        }
        
        if ((i + 1) % 10 == 0 || i == numFrames - 1) {
            fprintf(stderr, "\r  Frame %zu/%zu...", i + 1, numFrames);
            fflush(stderr);
        }
    }
    fprintf(stderr, "\n");
    
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
    
    // Générer vidéo composite MP4
    fprintf(stderr, "Generating composite MP4...\n");
    std::string ffmpegCmd = "ffmpeg -loglevel error -y -i \"" + mkvPath + ".mkv\" -map 0:0 ";
    if (hasAudio) {
        ffmpegCmd += "-map 0:4 ";  // Piste audio (track 4)
    }
    ffmpegCmd += "-c:v libx264 -preset medium -crf 18 ";
    if (hasAudio) {
        ffmpegCmd += "-c:a aac -b:a 192k ";
    }
    ffmpegCmd += "\"" + mp4Path + "\" 2>&1";
    
    int ret = system(ffmpegCmd.c_str());
    if (ret == 0) {
        fprintf(stderr, "  ✓ MP4: %s\n", mp4Path.c_str());
    } else {
        fprintf(stderr, "  ⚠ Warning: MP4 generation failed\n");
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
        
        if (!allLayers.empty()) {
            fprintf(metaFile, "  Resolution: %dx%d\n", allLayers[0].width, allLayers[0].height);
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
        
        fprintf(metaFile, "Output Files:\n");
        fprintf(metaFile, "  %s_video.mkv - Matroska with 4 video tracks + audio\n", inputFilename.c_str());
        fprintf(metaFile, "    * Track 0: BASE layer (pixels 0-235, RGB)\n");
        fprintf(metaFile, "    * Track 1: REMAP layer (pixels 236-254, RGB)\n");
        fprintf(metaFile, "    * Track 2: ALPHA layer (pixel 255, transparency mask)\n");
        fprintf(metaFile, "    * Track 3: LUMINANCE (grayscale Y)\n");
        fprintf(metaFile, "    * Audio: PCM 48 kHz mono\n");
        fprintf(metaFile, "  %s_audio.wav - PCM WAV 22 kHz (native quality)\n", inputFilename.c_str());
        fprintf(metaFile, "  %s_composite.mp4 - H.264 composite video + AAC audio\n", inputFilename.c_str());
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
    const char* codecStr = (argc >= 2) ? argv[1] : "h264";
    
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
    fprintf(stderr, "Version: 2.3.1 (2024-12-04) - Critical Bugfix\n");
    fprintf(stderr, "Codec: %s\n", codecStr);
    fprintf(stderr, "Max Resolution: Adaptive (up to Full HD)\n");
    
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
        if (processRbtFile(inputPath, fileOutputDir, codecStr, codec)) {
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
