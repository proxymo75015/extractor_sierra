#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <ios>
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

TEST_CASE("Extended header fields are required") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "missing_ext_fields.rbt";
    fs::path outDir = tmpDir / "missing_ext_fields_out";
    fs::create_directories(outDir);

    std::vector<uint8_t> data;
    push16(data, 0x16);  // signature
    data.insert(data.end(), {'S', 'O', 'L', '\0'});
    push16(data, 5);  // version
    push16(data, 0);  // audio block size
    push16(data, 0);  // primerZeroCompressFlag
    push16(data, 0);  // reserved
    push16(data, 1);  // numFrames
    push16(data, 0);  // paletteSize
    push16(data, 0);  // primerReservedSize
    push16(data, 1);  // xRes
    push16(data, 1);  // yRes
    data.push_back(0);  // hasPalette
    data.push_back(0);  // hasAudio
    push16(data, 0);  // reserved
    push16(data, 60);  // frameRate
    push16(data, 0);  // isHiRes
    push16(data, 0);  // maxSkippablePackets
    push16(data, 1);  // maxCelsPerFrame

    // Only the legacy reserved bytes, missing the four new int32_t fields.
    for (int i = 0; i < 2; ++i) push32(data, 0);

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, false);
    REQUIRE_THROWS_AS(RobotExtractorTester::readHeader(extractor),
                      std::ios_base::failure);
}
