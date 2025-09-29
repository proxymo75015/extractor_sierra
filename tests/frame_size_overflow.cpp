#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;
static void push16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>(x >> 8));
}

static void push32(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

static std::vector<uint8_t> build_header_v6() {
    std::vector<uint8_t> h;
    push16(h, 0x16);           // signature
    h.insert(h.end(), {'S', 'O', 'L', '\0'});
    push16(h, 6);              // version
    push16(h, 0);              // audio block size
    push16(h, 0);              // primerZeroCompressFlag
    push16(h, 0);              // skip
    push16(h, 1);              // numFrames
    push16(h, 0);              // paletteSize
    push16(h, 0);              // primerReservedSize
    push16(h, 1);              // xRes
    push16(h, 1);              // yRes
    h.push_back(0);            // hasPalette
    h.push_back(0);            // hasAudio
    push16(h, 0);              // skip
    push16(h, 60);             // frameRate
    push16(h, 0);              // isHiRes
    push16(h, 0);              // maxSkippablePackets
    push16(h, 1);              // maxCelsPerFrame
    for (int i = 0; i < 4; ++i) push32(h, 0); // champs supplémentaires
    for (int i = 0; i < 2; ++i) push32(h, 0); // zone réservée
    return h;
}

TEST_CASE("La taille de frame excessive déclenche une erreur") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "frame_size_overflow.rbt";
    fs::path outDir = tmpDir / "frame_size_overflow_out";
    fs::create_directories(outDir);

    auto data = build_header_v6();
    const uint32_t frameSize = 16 * 1024 * 1024;
    push32(data, frameSize);  // frame size table
    push32(data, frameSize);  // packet size table
    for (int i = 0; i < 256; ++i)
        push32(data, 0);  // cue times
    for (int i = 0; i < 256; ++i)
        push16(data, 0);  // cue values

    const size_t padding = (2048 - (data.size() % 2048)) % 2048;
    data.insert(data.end(), padding, 0);

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, false);
    try {
        extractor.extract();
        FAIL("No exception thrown");
    } catch (const std::runtime_error &e) {
        REQUIRE(std::string(e.what()).find(
                    "Somme des tailles de frame dépasse les données restantes du fichier") !=
                std::string::npos);
    }
}
