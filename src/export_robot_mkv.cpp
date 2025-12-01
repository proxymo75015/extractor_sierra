/**
 * Programme d'export vidéo Robot en MKV multi-couches
 * 
 * Génère un fichier MKV avec 4 pistes vidéo + audio:
 * - Track 0: BASE layer (RGB, pixels 0-235)
 * - Track 1: REMAP layer (RGB, pixels 236-254)  
 * - Track 2: ALPHA layer (transparency, pixel 255)
 * - Track 3: LUMINANCE layer (grayscale Y)
 * - Audio: PCM 48 kHz mono
 * 
 * Usage:
 *   export_robot_mkv <input.rbt> <output_dir> [codec]
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
#include <cstring>
#include <sys/stat.h>
#include <climits>
#include <ctime>

using namespace RobotExtractor;

// Fonction helper pour extraire une frame et la décomposer en couches
bool extractFrameAsLayers(RbtParser& parser, size_t frameIdx, 
                          const std::vector<uint8_t>& palette,
                          RobotLayerFrame& outLayer);

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input.rbt> <output_dir> [codec]\n", argv[0]);
        fprintf(stderr, "\nCodecs: h264 (default), h265, vp9, ffv1\n");
        return 1;
    }
    
    const char* inputPath = argv[1];
    const char* outputDir = argv[2];
    const char* codecStr = (argc >= 4) ? argv[3] : "h264";
    
    // Créer le dossier de sortie
    mkdir(outputDir, 0755);
    
    // Ouvrir le fichier Robot
    FILE* f = fopen(inputPath, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s\n", inputPath);
        return 1;
    }
    
    RbtParser parser(f);
    
    // Parser l'en-tête
    if (!parser.parseHeader()) {
        fprintf(stderr, "Error: Failed to parse Robot header\n");
        fclose(f);
        return 1;
    }
    
    size_t numFrames = parser.getNumFrames();
    int frameRate = parser.getFrameRate();
    bool hasAudio = parser.hasAudio();
    
    fprintf(stderr, "\n=== Robot File Info ===\n");
    fprintf(stderr, "Input: %s\n", inputPath);
    fprintf(stderr, "Output directory: %s\n", outputDir);
    fprintf(stderr, "Frames: %zu @ %d fps (%.2f seconds)\n", 
            numFrames, frameRate, (double)numFrames / frameRate);
    fprintf(stderr, "Audio: %s\n", hasAudio ? "yes" : "no");
    
    // Configuration MKV
    MKVExportConfig config;
    config.framerate = frameRate;
    
    if (strcmp(codecStr, "h265") == 0) {
        config.codec = MKVExportConfig::Codec::H265;
    } else if (strcmp(codecStr, "vp9") == 0) {
        config.codec = MKVExportConfig::Codec::VP9;
    } else if (strcmp(codecStr, "ffv1") == 0) {
        config.codec = MKVExportConfig::Codec::FFV1;
    } else {
        config.codec = MKVExportConfig::Codec::H264;
        codecStr = "h264";
    }
    
    fprintf(stderr, "Codec: %s\n", codecStr);
    
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
    
    // Chemins de sortie avec nom du fichier RBT
    std::string mkvPath = std::string(outputDir) + "/" + inputFilename + "_video";
    std::string wavPath = std::string(outputDir) + "/" + inputFilename + "_audio.wav";
    std::string metadataPath = std::string(outputDir) + "/" + inputFilename + "_metadata.txt";
    
    // Extraire la palette globale
    std::vector<uint8_t> globalPalette = parser.getPalette();
    
    if (globalPalette.empty()) {
        fprintf(stderr, "Error: No palette found\n");
        fclose(f);
        return 1;
    }
    
    // Extraire toutes les frames et les décomposer en couches
    fprintf(stderr, "\n=== Extracting Frames (decomposing into layers) ===\n");
    
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
        
        // Décomposer en couches
        RobotLayerFrame layer = decomposeRobotFrame(pixelIndices, globalPalette, width, height);
        allLayers.push_back(std::move(layer));
        
        if ((i + 1) % 10 == 0 || i == numFrames - 1) {
            fprintf(stderr, "\rExtracting frame %zu/%zu...", i + 1, numFrames);
            fflush(stderr);
        }
    }
    fprintf(stderr, "\n");
    
    // Statistiques sur les types de pixels
    fprintf(stderr, "\n=== Pixel Classification Statistics ===\n");
    if (!allLayers.empty()) {
        size_t totalPixels = allLayers[0].width * allLayers[0].height;
        
        // Analyser TOUTES les frames pour trouver des pixels REMAP/SKIP
        size_t totalBase = 0, totalRemap = 0, totalSkip = 0;
        int firstRemapFrame = -1, firstSkipFrame = -1;
        
        for (size_t frameIdx = 0; frameIdx < allLayers.size(); ++frameIdx) {
            const auto& layer = allLayers[frameIdx];
            size_t baseCount = 0, remapCount = 0, skipCount = 0;
            
            for (size_t i = 0; i < totalPixels; ++i) {
                if (layer.alpha[i] == 0) {
                    skipCount++;
                } else if (layer.remap_mask[i] == 255) {
                    remapCount++;
                } else {
                    baseCount++;
                }
            }
            
            totalBase += baseCount;
            totalRemap += remapCount;
            totalSkip += skipCount;
            
            if (remapCount > 0 && firstRemapFrame == -1) {
                firstRemapFrame = frameIdx;
                fprintf(stderr, "  Frame %zu: first REMAP pixels detected! base=%zu remap=%zu skip=%zu\n",
                        frameIdx, baseCount, remapCount, skipCount);
            }
            if (skipCount > 0 && firstSkipFrame == -1) {
                firstSkipFrame = frameIdx;
                fprintf(stderr, "  Frame %zu: first SKIP pixels detected! base=%zu remap=%zu skip=%zu\n",
                        frameIdx, baseCount, remapCount, skipCount);
            }
        }
        
        fprintf(stderr, "\n  Summary across ALL frames:\n");
        size_t grandTotal = totalBase + totalRemap + totalSkip;
        fprintf(stderr, "    base=%zu (%.1f%%) remap=%zu (%.1f%%) skip=%zu (%.1f%%)\n",
                totalBase, 100.0 * totalBase / grandTotal,
                totalRemap, 100.0 * totalRemap / grandTotal,
                totalSkip, 100.0 * totalSkip / grandTotal);
        
        if (firstRemapFrame == -1) {
            fprintf(stderr, "  ⚠ WARNING: NO REMAP pixels found in any frame!\n");
        }
        if (firstSkipFrame == -1) {
            fprintf(stderr, "  ⚠ WARNING: NO SKIP pixels found in any frame!\n");
        }
    }
    
    // Extraire l'audio
    std::vector<int16_t> audioSamples;
    if (hasAudio) {
        fprintf(stderr, "\n=== Extracting Audio ===\n");
        parser.extractAudio(wavPath);
        
        // Charger audio.wav pour le passer à l'exporteur MKV
        fprintf(stderr, "✓ Audio exported: %s\n", wavPath.c_str());
    }
    
    // Exporter en MKV multi-couches
    fprintf(stderr, "\n=== Exporting Multi-Track MKV ===\n");
    
    RobotMKVExporter exporter(config);
    if (!exporter.exportMultiTrack(allLayers, mkvPath, hasAudio ? wavPath : "")) {
        fprintf(stderr, "\nError: Multi-track MKV export failed\n");
        fclose(f);
        return 1;
    }
    
    // Générer le fichier de métadonnées
    fprintf(stderr, "\n=== Writing Metadata ===\n");
    FILE* metaFile = fopen(metadataPath.c_str(), "w");
    if (metaFile) {
        fprintf(metaFile, "=== Robot Video Metadata ===\n\n");
        fprintf(metaFile, "Source File: %s\n", inputPath);
        fprintf(metaFile, "Format: Sierra Robot Video (v5/6)\n");
        fprintf(metaFile, "Platform: PC\n\n");
        
        fprintf(metaFile, "Video:\n");
        fprintf(metaFile, "  Frames: %zu\n", numFrames);
        fprintf(metaFile, "  Frame Rate: %d fps\n", frameRate);
        fprintf(metaFile, "  Duration: %.2f seconds\n", (double)numFrames / frameRate);
        
        if (!allLayers.empty()) {
            fprintf(metaFile, "  Resolution: %dx%d\n", allLayers[0].width, allLayers[0].height);
        }
        
        fprintf(metaFile, "  Codec: %s\n\n", codecStr);
        
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
        fprintf(metaFile, "  %s_video.mkv - Matroska with 4 video tracks:\n", inputFilename.c_str());
        fprintf(metaFile, "    * Track 0: BASE layer (pixels 0-235, RGB)\n");
        fprintf(metaFile, "    * Track 1: REMAP layer (pixels 236-254, RGB)\n");
        fprintf(metaFile, "    * Track 2: ALPHA layer (pixel 255, transparency mask)\n");
        fprintf(metaFile, "    * Track 3: LUMINANCE (grayscale Y)\n");
        fprintf(metaFile, "    * Audio: PCM 48 kHz mono\n");
        fprintf(metaFile, "  %s_audio.wav - PCM WAV 22 kHz (native quality)\n", inputFilename.c_str());
        fprintf(metaFile, "  %s_metadata.txt - This file\n\n", inputFilename.c_str());
        
        time_t now = time(nullptr);
        fprintf(metaFile, "Export Date: %s", ctime(&now));
        
        fclose(metaFile);
        fprintf(stderr, "✓ Metadata written: %s\n", metadataPath.c_str());
    }
    
    fprintf(stderr, "\n✓ Export complete!\n");
    fprintf(stderr, "  Video (MKV): %s.mkv (4 video tracks + audio)\n", mkvPath.c_str());
    fprintf(stderr, "    - Track 0: BASE layer (RGB, pixels 0-235)\n");
    fprintf(stderr, "    - Track 1: REMAP layer (RGB, pixels 236-254)\n");
    fprintf(stderr, "    - Track 2: ALPHA layer (transparency mask)\n");
    fprintf(stderr, "    - Track 3: LUMINANCE layer (grayscale Y)\n");
    if (hasAudio) {
        fprintf(stderr, "  Audio: %s\n", wavPath.c_str());
    }
    fprintf(stderr, "  Metadata: %s\n", metadataPath.c_str());
    
    fclose(f);
    return 0;
}
