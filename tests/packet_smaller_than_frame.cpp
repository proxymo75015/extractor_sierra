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

static std::vector<uint8_t> build_header() {
    std::vector<uint8_t> h;
    push16(h, 0x16);           // signature
    h.insert(h.end(), {'S','O','L','\0'});
    push16(h, 5);              // version
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
    for (int i = 0; i < 4; ++i) push32(h, 0); // maxCelArea
    for (int i = 0; i < 2; ++i) push32(h, 0); // padding
    return h;
}

TEST_CASE("Packet size smaller than frame size throws") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "packet_small.rbt";
    fs::path outDir = tmpDir / "packet_small_out";
    fs::create_directories(outDir);

    auto data = build_header();
    push16(data, 10); // frame size
    push16(data, 5);  // packet size < frame size

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, false);
    try {
        extractor.extract();
        FAIL("No exception thrown");
    } catch (const std::runtime_error &e) {
        REQUIRE(std::string(e.what()).find("Packet size < frame size") != std::string::npos);
    }
}
