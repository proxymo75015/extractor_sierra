// Minimal standalone Robot .rbt extractor
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>

#include "core/rbt_parser.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        std::printf("Usage: %s <input.rbt> <out_dir> [max_frames]\n", argv[0]);
        std::printf("Extracts video frames and interleaved mono audio to WAV file, then generates MP4\n");
        return 1;
    }

    const char *inPath = argv[1];
    const char *outDir = argv[2];

    std::FILE *f = std::fopen(inPath, "rb");
    if (!f) {
        std::perror("fopen");
        return 2;
    }

    RbtParser parser(f);
    if (!parser.parseHeader()) {
        std::fprintf(stderr, "Failed to parse header\n");
        std::fclose(f);
        return 3;
    }

    std::fprintf(stderr, "RBT: %zu frames, frameRate=%d\n", parser.getNumFrames(), parser.getFrameRate());

    // Create output directory structure
    std::string cmd = std::string("mkdir -p ") + outDir;
    std::system(cmd.c_str());
    
    std::string framesDir = std::string(outDir) + "/frames";
    cmd = std::string("mkdir -p ") + framesDir;
    std::system(cmd.c_str());

    parser.dumpMetadata(outDir);

    // Extract frames
    size_t maxFrames = parser.getNumFrames();
    if (argc >= 4) {
        int mf = std::atoi(argv[3]);
        if (mf > 0) maxFrames = (size_t)mf;
    }

    std::fprintf(stderr, "\n=== Extracting %zu frames ===\n", maxFrames);
    for (size_t i = 0; i < maxFrames && i < parser.getNumFrames(); ++i) {
        if (!parser.extractFrame(i, framesDir.c_str())) {
            std::fprintf(stderr, "Warning: failed to extract frame %zu\n", i);
        }
    }

    // Extract audio to audio.wav (22050 Hz mono)
    if (parser.hasAudio()) {
        std::fprintf(stderr, "\n=== Extracting audio ===\n");
        parser.extractAudio(outDir, maxFrames);
    }

    // Generate MP4 video with FFmpeg
    std::fprintf(stderr, "\n=== Generating MP4 video ===\n");
    
    std::string audioWav = std::string(outDir) + "/audio.wav";
    std::string outputMp4 = std::string(outDir) + "/output.mp4";
    
    // Check if audio file exists
    bool hasAudio = (std::ifstream(audioWav).good());
    
    std::ostringstream ffmpegCmd;
    ffmpegCmd << "ffmpeg -y -framerate " << parser.getFrameRate() 
              << " -pattern_type glob -i '" << framesDir << "/*.ppm'";
    
    if (hasAudio) {
        // Audio 22050 Hz mono
        ffmpegCmd << " -i " << audioWav
                  << " -c:v libx264 -pix_fmt yuv420p -preset fast -crf 18"
                  << " -c:a aac -b:a 128k"
                  << " -shortest " << outputMp4;
    } else {
        // Video only
        ffmpegCmd << " -c:v libx264 -pix_fmt yuv420p -preset fast -crf 18"
                  << " " << outputMp4;
    }
    
    std::fprintf(stderr, "Running: %s\n", ffmpegCmd.str().c_str());
    int ret = std::system(ffmpegCmd.str().c_str());
    
    if (ret == 0) {
        std::fprintf(stderr, "\n✅ Success! Output:\n");
        std::fprintf(stderr, "   Frames:  %s/\n", framesDir.c_str());
        if (hasAudio) std::fprintf(stderr, "   Audio: %s (22050 Hz mono)\n", audioWav.c_str());
        std::fprintf(stderr, "   Video:   %s\n", outputMp4.c_str());
    } else {
        std::fprintf(stderr, "⚠️  FFmpeg failed (code %d). Output still available:\n", ret);
        std::fprintf(stderr, "   Frames:  %s/\n", framesDir.c_str());
        if (hasAudio) std::fprintf(stderr, "   Audio: %s (22050 Hz mono)\n", audioWav.c_str());
    }

    std::fclose(f);
    return 0;
}
