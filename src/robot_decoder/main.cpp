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

#include "rbt_parser.h"
#include "dpcm.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        std::printf("Usage: %s <input.rbt> <out_dir>\n", argv[0]);
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

    parser.dumpMetadata(outDir);

    // create frames directory
    std::string framesDir = std::string(outDir) + "/frames";
    std::string cmd = std::string("mkdir -p ") + framesDir;
    std::system(cmd.c_str());

    size_t maxFrames = parser.getNumFrames();
    if (argc >= 4) {
        int mf = std::atoi(argv[3]);
        if (mf > 0) maxFrames = (size_t)mf;
    }

    for (size_t i = 0; i < maxFrames && i < parser.getNumFrames(); ++i) {
        if (!parser.extractFrame(i, framesDir.c_str())) {
            std::fprintf(stderr, "Warning: failed to extract frame %zu\n", i);
        }
    }

    // Audio extraction is optional: pass fourth arg "audio" to enable.
    if (parser.hasAudio() && argc >= 5 && std::strcmp(argv[4], "audio") == 0) {
        std::string audioOut = std::string(outDir) + "/audio.raw.pcm";
        std::FILE *af = std::fopen(audioOut.c_str(), "wb");
        if (af) {
            size_t totalSamples = 0;
            parser.extractAllAudio([&](const int16_t *samples, size_t sampleCount){
                size_t wrote = fwrite(samples, sizeof(int16_t), sampleCount, af);
                totalSamples += wrote;
            });
            std::fclose(af);
            std::printf("Wrote raw PCM to %s (mono 22050Hz 16-bit) samples=%zu\n", audioOut.c_str(), totalSamples);
        }
    } else if (parser.hasAudio()) {
        std::fprintf(stderr, "Audio present in file; skipping extraction (pass 'audio' arg to enable)\n");
    }

    std::fclose(f);
    std::printf("Done. Output in %s\n", outDir);
    return 0;
}
