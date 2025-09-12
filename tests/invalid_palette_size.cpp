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

static void push32(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

static std::vector<uint8_t> build_header(uint16_t paletteSize) {
    std::vector<uint8_t> h;
    push16(h, 0x16);            // signature
    h.insert(h.end(), {'S','O','L','\0'});
    push16(h, 5);               // version
    push16(h, 0);               // audio block size
    push16(h, 0);               // primerZeroCompressFlag
    push16(h, 0);               // skip
    push16(h, 1);               // numFrames
    push16(h, paletteSize);     // paletteSize
    push16(h, 0);               // primerReservedSize
    push16(h, 1);               // xRes
    push16(h, 1);               // yRes
    h.push_back(1);             // hasPalette
    h.push_back(0);             // hasAudio
    push16(h, 0);               // skip
    push16(h, 60);              // frameRate
    push16(h, 0);               // isHiRes
    push16(h, 0);               // maxSkippablePackets
    push16(h, 1);               // maxCelsPerFrame
    for (int i = 0; i < 2; ++i) push32(h, 0); // padding
    return h;
}

TEST_CASE("Palette size not divisible by 3 throws") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "palette_bad.rbt";
    fs::path outDir = tmpDir / "palette_bad_out";
    fs::create_directories(outDir);

    auto data = build_header(770); // 770 not divisible by 3
    data.insert(data.end(), 770, 0); // palette data

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, false);
    try {
        extractor.extract();
        FAIL("Aucune exception levée");
    } catch (const std::runtime_error &e) {
        REQUIRE(std::string(e.what()).find("non multiple de 3") != std::string::npos);
    }
}
