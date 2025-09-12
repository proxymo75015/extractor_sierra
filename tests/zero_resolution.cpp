#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;
using robot::RobotExtractorTester;

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

static std::vector<uint8_t> build_header_zero_res() {
    std::vector<uint8_t> h;
    push16(h, 0x16);            // signature
    h.insert(h.end(), {'S','O','L','\0'});
    push16(h, 5);               // version
    push16(h, 0);               // audio block size
    push16(h, 0);               // primerZeroCompressFlag
    push16(h, 0);               // skip
    push16(h, 1);               // numFrames
    push16(h, 0);               // paletteSize
    push16(h, 0);               // primerReservedSize
    push16(h, 0);               // xRes
    push16(h, 0);               // yRes
    h.push_back(0);             // hasPalette
    h.push_back(0);             // hasAudio
    push16(h, 0);               // skip
    push16(h, 60);              // frameRate
    push16(h, 0);               // isHiRes
    push16(h, 0);               // maxSkippablePackets
    push16(h, 1);               // maxCelsPerFrame
    for (int i = 0; i < 2; ++i) push32(h, 0); // padding
    return h;
}

TEST_CASE("xRes and yRes can be zero") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "zero_res.rbt";
    auto data = build_header_zero_res();
    {
        std::ofstream out(input, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    fs::path outDir = tmpDir / "zero_res_out";
    fs::create_directories(outDir);

    robot::RobotExtractor extractor(input, outDir, false);
    REQUIRE_NOTHROW(RobotExtractorTester::readHeader(extractor));
    REQUIRE(RobotExtractorTester::xRes(extractor) == 0);
    REQUIRE(RobotExtractorTester::yRes(extractor) == 0);
}
