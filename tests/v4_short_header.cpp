#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <ios>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;
using robot::RobotExtractor;
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

TEST_CASE("Version 4 header without extended fields is accepted") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "v4_short_header.rbt";
    fs::path outDir = tmpDir / "v4_short_header_out";
    fs::create_directories(outDir);

    std::vector<uint8_t> data;
    push16(data, 0x16);  // signature
    data.insert(data.end(), {'S', 'O', 'L', '\0'});
    push16(data, 4);  // version
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

    const size_t headerSize = data.size();

    push16(data, 2);  // frame size table entry
    push16(data, 2);  // packet size table entry
    for (int i = 0; i < 256; ++i)
        push32(data, 0);  // cue times
    for (int i = 0; i < 256; ++i)
        push16(data, 0);  // cue values

    data.resize(((data.size() + 2047) / 2048) * 2048, 0);

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();

    RobotExtractor extractor(input, outDir, false);

    REQUIRE_NOTHROW(RobotExtractorTester::readHeader(extractor));
    CHECK(RobotExtractorTester::numFrames(extractor) == 1);
    CHECK(RobotExtractorTester::postHeaderPos(extractor) ==
          static_cast<std::streamoff>(headerSize));

    // No palette or audio primer are stored, so the helpers should operate
    // without consuming additional data beyond the reserved header section.
    REQUIRE_NOTHROW(RobotExtractorTester::readPrimer(extractor));
    REQUIRE_NOTHROW(RobotExtractorTester::readPalette(extractor));
    REQUIRE_NOTHROW(RobotExtractorTester::readSizesAndCues(extractor));

    const auto &frames = RobotExtractorTester::frameSizes(extractor);
    REQUIRE(frames.size() == 1);
    CHECK(frames[0] == 2);

    const auto &packets = RobotExtractorTester::packetSizes(extractor);
    REQUIRE(packets.size() == 1);
    CHECK(packets[0] == 2);

    const auto &fixedSizes = RobotExtractorTester::fixedCelSizes(extractor);
    for (uint32_t size : fixedSizes) {
        CHECK(size == 0);
    }

    fs::remove(input);
    fs::remove_all(outDir);
}
