#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;

namespace {

void push16(std::vector<uint8_t> &v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}

void push32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

std::vector<uint8_t> build_header(uint16_t version) {
  std::vector<uint8_t> h;
  push16(h, 0x16); // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, version);
  push16(h, 0); // audio block size
  push16(h, 0); // primerZeroCompressFlag
  push16(h, 0); // primerReservedSize high bits / reserved
  push16(h, 1); // numFrames
  push16(h, 0); // paletteSize
  push16(h, 0); // primerReservedSize
  push16(h, 1); // xRes
  push16(h, 1); // yRes
  h.push_back(0); // hasPalette
  h.push_back(0); // hasAudio
  push16(h, 0); // reserved
  push16(h, 60); // frameRate
  push16(h, 0); // isHiRes
  push16(h, 0); // maxSkippablePackets
  push16(h, 1); // maxCelsPerFrame
  for (int i = 0; i < 4; ++i)
    push32(h, 0);
  for (int i = 0; i < 2; ++i)
    push32(h, 0);
  return h;
}

void append_cues(std::vector<uint8_t> &data) {
  for (int i = 0; i < 256; ++i)
    push32(data, 0);
  for (int i = 0; i < 256; ++i)
    push16(data, 0);
}

void write_robot_file(const fs::path &path, const std::vector<uint8_t> &data) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
}

} // namespace

TEST_CASE("Empty frame payloads are accepted") {
  fs::path tmpDir = fs::temp_directory_path();

  SECTION("Robot version 5") {
    fs::path input = tmpDir / "empty_frame_v5.rbt";
    fs::path outDir = tmpDir / "empty_frame_v5_out";
    if (fs::exists(outDir)) {
      fs::remove_all(outDir);
    }
    fs::create_directories(outDir);

    auto data = build_header(5);
    push16(data, 0); // frame size table entry
    push16(data, 0); // packet size table entry
    append_cues(data);
    const size_t aligned = ((data.size() + 2047) / 2048) * 2048;
    data.resize(aligned, 0);

    write_robot_file(input, data);

    robot::RobotExtractor extractor(input, outDir, false);

    REQUIRE_NOTHROW(robot::RobotExtractorTester::readHeader(extractor));
    REQUIRE_NOTHROW(robot::RobotExtractorTester::readPrimer(extractor));
    REQUIRE_NOTHROW(robot::RobotExtractorTester::readPalette(extractor));
    REQUIRE_NOTHROW(robot::RobotExtractorTester::readSizesAndCues(extractor));

    const auto &frames = robot::RobotExtractorTester::frameSizes(extractor);
    REQUIRE(frames.size() == 1);
    CHECK(frames[0] == 0);

    const auto &packets = robot::RobotExtractorTester::packetSizes(extractor);
    REQUIRE(packets.size() == 1);
    CHECK(packets[0] == 0);

    nlohmann::json frameJson;
    REQUIRE_NOTHROW(
        robot::RobotExtractorTester::exportFrame(extractor, 0, frameJson));
    CHECK(frameJson["cels"].empty());

    fs::remove(input);
    fs::remove_all(outDir);
  }

  SECTION("Robot version 6") {
    fs::path input = tmpDir / "empty_frame_v6.rbt";
    fs::path outDir = tmpDir / "empty_frame_v6_out";
    if (fs::exists(outDir)) {
      fs::remove_all(outDir);
    }
    fs::create_directories(outDir);

    auto data = build_header(6);
    push32(data, 0); // frame size table entry
    push32(data, 0); // packet size table entry
    append_cues(data);
    const size_t aligned = ((data.size() + 2047) / 2048) * 2048;
    data.resize(aligned, 0);

    write_robot_file(input, data);

    robot::RobotExtractor extractor(input, outDir, false);

    REQUIRE_NOTHROW(robot::RobotExtractorTester::readHeader(extractor));
    REQUIRE_NOTHROW(robot::RobotExtractorTester::readPrimer(extractor));
    REQUIRE_NOTHROW(robot::RobotExtractorTester::readPalette(extractor));
    REQUIRE_NOTHROW(robot::RobotExtractorTester::readSizesAndCues(extractor));

    const auto &frames = robot::RobotExtractorTester::frameSizes(extractor);
    REQUIRE(frames.size() == 1);
    CHECK(frames[0] == 0);

    const auto &packets = robot::RobotExtractorTester::packetSizes(extractor);
    REQUIRE(packets.size() == 1);
    CHECK(packets[0] == 0);

    nlohmann::json frameJson;
    REQUIRE_NOTHROW(
        robot::RobotExtractorTester::exportFrame(extractor, 0, frameJson));
    CHECK(frameJson["cels"].empty());

    fs::remove(input);
    fs::remove_all(outDir);
  }
}
