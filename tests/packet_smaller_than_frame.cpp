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
    for (int i = 0; i < 4; ++i) push32(h, 0); // champs supplémentaires
    for (int i = 0; i < 2; ++i) push32(h, 0); // zone réservée
    return h;
}

TEST_CASE("Inconsistent packet sizes do not abort extraction") {
    fs::path tmpDir = fs::temp_directory_path();

    SECTION("packet size smaller than frame size is adjusted") {
        fs::path input = tmpDir / "packet_small_inconsistent.rbt";
        fs::path outDir = tmpDir / "packet_small_inconsistent_out";
        fs::create_directories(outDir);

        auto data = build_header();
        push16(data, 2); // frame size
        push16(data, 1); // packet size < frame size
        for (int i = 0; i < 256; ++i)
            push32(data, 0); // cue times
        for (int i = 0; i < 256; ++i)
            push16(data, 0); // cue values

        data.resize(((data.size() + 2047) / 2048) * 2048, 0);
        // Frame data: numCels = 0
        data.push_back(0);
        data.push_back(0);

        std::ofstream out(input, std::ios::binary);
        out.write(reinterpret_cast<const char *>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        out.close();

        robot::RobotExtractor extractor(input, outDir, false);
        REQUIRE_NOTHROW(extractor.extract());

        const auto &frames = robot::RobotExtractorTester::frameSizes(extractor);
        REQUIRE(frames.size() == 1);
        REQUIRE(frames[0] == 2);

        const auto &packets = robot::RobotExtractorTester::packetSizes(extractor);
        REQUIRE(packets.size() == 1);
        REQUIRE(packets[0] == 2);
    }

    SECTION("packet size larger than frame size is tolerated") {
        fs::path input = tmpDir / "packet_large_inconsistent.rbt";
        fs::path outDir = tmpDir / "packet_large_inconsistent_out";
        fs::create_directories(outDir);

        auto data = build_header();
        push16(data, 2); // frame size
        push16(data, 6); // packet size > frame size
        for (int i = 0; i < 256; ++i)
            push32(data, 0); // cue times
        for (int i = 0; i < 256; ++i)
            push16(data, 0); // cue values

        data.resize(((data.size() + 2047) / 2048) * 2048, 0);
        // Frame data: numCels = 0
        data.push_back(0);
        data.push_back(0);
        // Extra bytes to match the declared packet size
        data.insert(data.end(), 4, 0);

        std::ofstream out(input, std::ios::binary);
        out.write(reinterpret_cast<const char *>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        out.close();

        robot::RobotExtractor extractor(input, outDir, false);
        REQUIRE_NOTHROW(extractor.extract());

        const auto &frames = robot::RobotExtractorTester::frameSizes(extractor);
        REQUIRE(frames.size() == 1);
        REQUIRE(frames[0] == 2);

        const auto &packets = robot::RobotExtractorTester::packetSizes(extractor);
        REQUIRE(packets.size() == 1);
        REQUIRE(packets[0] == 6);
    }
}
