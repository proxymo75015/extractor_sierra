#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;

static void push16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>(x >> 8));
}

TEST_CASE("Audio block size at limit is accepted") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "audio_block_limit.rbt";
    fs::path outDir = tmpDir / "audio_block_limit_out";
    fs::create_directories(outDir);

    std::vector<uint8_t> data;
    push16(data, 0x16);  // signature
    data.insert(data.end(), {'S', 'O', 'L', '\0'});
    push16(data, 5);  // version
    push16(data, robot::RobotExtractor::kMaxAudioBlockSize);  // audio block size
    push16(data, 0);  // primerZeroCompressFlag
    push16(data, 0);  // reserved
    push16(data, 1);  // numFrames
    push16(data, 0);  // paletteSize
    push16(data, 0);  // primerReservedSize
    push16(data, 320);  // xRes
    push16(data, 200);  // yRes
    data.push_back(0);  // hasPalette
    data.push_back(0);  // hasAudio
    push16(data, 0);  // reserved
    push16(data, 24);  // frameRate
    push16(data, 0);  // isHiRes
    push16(data, 0);  // maxSkippablePackets
    push16(data, 1);  // maxCelsPerFrame
    for (int i = 0; i < 8; ++i) {
        data.push_back(0);  // seekg(8)
    }

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, true);

    REQUIRE_NOTHROW(extractor.readHeader());
}
