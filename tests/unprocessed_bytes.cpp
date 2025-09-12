#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdint>

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

TEST_CASE("Extra data after cels throws") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "extra_bytes.rbt";
    fs::path outDir = tmpDir / "extra_bytes_out";
    fs::create_directories(outDir);

    std::vector<uint8_t> data;
    // Header
    push16(data, 0x16); // signature
    data.insert(data.end(), {'S','O','L','\0'});
    push16(data, 5); // version
    push16(data, 0); // audio block size
    push16(data, 0); // primerZeroCompressFlag
    push16(data, 0); // skip
    push16(data, 1); // numFrames
    push16(data, 768); // paletteSize
    push16(data, 0); // primerReservedSize
    push16(data, 1); // xRes
    push16(data, 1); // yRes
    data.push_back(1); // hasPalette
    data.push_back(0); // hasAudio
    push16(data, 0); // skip
    push16(data, 60); // frameRate
    push16(data, 0); // isHiRes
    push16(data, 0); // maxSkippablePackets
    push16(data, 1); // maxCelsPerFrame
    for (int i = 0; i < 2; ++i) push32(data, 0); // padding

    // Palette
    data.insert(data.end(), 768, 0);

    // Frame and packet sizes
    push16(data, 39); // frame size
    push16(data, 39); // packet size

    // Cues
    data.insert(data.end(), 1536, 0);

    // Align to 2048 bytes
    size_t pad = (2048 - (data.size() % 2048)) % 2048;
    data.insert(data.end(), pad, 0);

    // Frame data with extra bytes
    push16(data, 1); // numCels
    std::vector<uint8_t> celHeader(22, 0);
    celHeader[1] = 100; // verticalScale
    celHeader[2] = 1;   // width
    celHeader[4] = 1;   // height
    celHeader[14] = 11; // dataSize (10 bytes header + 1 byte pixel)
    celHeader[16] = 1;  // numChunks
    data.insert(data.end(), celHeader.begin(), celHeader.end());

    std::vector<uint8_t> chunkHeader(10, 0);
    chunkHeader[0] = 1; // compSz
    chunkHeader[4] = 1; // decompSz
    chunkHeader[8] = 2; // compType raw
    data.insert(data.end(), chunkHeader.begin(), chunkHeader.end());

    data.push_back(0); // pixel data
    data.insert(data.end(), {0,0,0,0}); // extra bytes

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, false);
    try {
        extractor.extract();
        FAIL("No exception thrown");
    } catch (const std::runtime_error &e) {
        REQUIRE(std::string(e.what()).find("octets non traités") != std::string::npos);
    }
}
