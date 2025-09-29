#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
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

static std::vector<uint8_t> build_frame(uint16_t w, uint16_t h, uint8_t scale,
                                        const std::vector<uint8_t> &chunkData) {
  std::vector<uint8_t> data;
  data.reserve(2 + 22 + 10 + chunkData.size());
  push16(data, 1); // numCels

  std::vector<uint8_t> celHeader(22, 0);
  celHeader[1] = scale; // vertical scale
  celHeader[2] = static_cast<uint8_t>(w & 0xFF);
  celHeader[3] = static_cast<uint8_t>(w >> 8);
  celHeader[4] = static_cast<uint8_t>(h & 0xFF);
  celHeader[5] = static_cast<uint8_t>(h >> 8);
  const uint32_t compSize = static_cast<uint32_t>(chunkData.size());
  const uint16_t dataSize = static_cast<uint16_t>(10 + compSize);
  celHeader[14] = static_cast<uint8_t>(dataSize & 0xFF);
  celHeader[15] = static_cast<uint8_t>(dataSize >> 8);
  celHeader[16] = 1; // numChunks
  celHeader[17] = 0;
  data.insert(data.end(), celHeader.begin(), celHeader.end());

  push32(data, compSize);
  push32(data, compSize);
  push16(data, 2); // raw chunk
  data.insert(data.end(), chunkData.begin(), chunkData.end());

  return data;
}

TEST_CASE("Cel dépassant la limite dynamique est refusée") {
  const uint16_t w = 320;
  const uint16_t h = 200;
  const uint8_t scale = 100;
  const size_t chunkSize = static_cast<size_t>(w) * h;
  REQUIRE(chunkSize + 10 <= std::numeric_limits<uint16_t>::max());

  std::vector<uint8_t> chunk(chunkSize, 0x5A);
  auto frameData = build_frame(w, h, scale, chunk);

  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "cel_limit_dyn.bin";
  fs::path outDir = tmpDir / "cel_limit_dyn_out";
  fs::create_directories(outDir);

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(frameData.data()),
            static_cast<std::streamsize>(frameData.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, false);
  RobotExtractorTester::hasPalette(extractor) = false;
  RobotExtractorTester::bigEndian(extractor) = false;
  RobotExtractorTester::maxCelsPerFrame(extractor) = 1;
  RobotExtractorTester::fixedCelSizes(extractor) = {60000, 0, 0, 0};
  RobotExtractorTester::frameSizes(extractor) =
      {static_cast<uint32_t>(frameData.size())};
  RobotExtractorTester::packetSizes(extractor) =
      {static_cast<uint32_t>(frameData.size())};
  RobotExtractorTester::file(extractor).seekg(0, std::ios::beg);

  nlohmann::json frameJson;
  try {
    RobotExtractorTester::exportFrame(extractor, 0, frameJson);
    FAIL("Aucune exception levée pour une cel hors limite");
  } catch (const std::runtime_error &e) {
    CHECK_THAT(std::string(e.what()),
               Catch::Matchers::ContainsSubstring("Dimensions de cel invalides"));
  }

  fs::remove(input);
  fs::remove_all(outDir);
}

TEST_CASE("Cel de plus d'un mégapixel est exportée correctement") {
  const uint16_t w = 1920;
  const uint16_t h = 1080;
  const uint8_t scale = 1; // sourceHeight = 10
  const size_t sourceHeight = static_cast<size_t>(h) * scale / 100;
  REQUIRE(sourceHeight > 0);
  const size_t chunkSize = static_cast<size_t>(w) * sourceHeight;
  REQUIRE(chunkSize + 10 <= std::numeric_limits<uint16_t>::max());

  std::vector<uint8_t> chunk(chunkSize);
  for (size_t i = 0; i < chunk.size(); ++i) {
    chunk[i] = static_cast<uint8_t>(i & 0xFF);
  }
  auto frameData = build_frame(w, h, scale, chunk);

  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "cel_large.bin";
  fs::path outDir = tmpDir / "cel_large_out";
  fs::create_directories(outDir);

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(frameData.data()),
            static_cast<std::streamsize>(frameData.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, false);
  RobotExtractorTester::hasPalette(extractor) = false;
  RobotExtractorTester::bigEndian(extractor) = false;
  RobotExtractorTester::maxCelsPerFrame(extractor) = 1;
  RobotExtractorTester::fixedCelSizes(extractor) =
      {static_cast<uint32_t>(w) * h, 0, 0, 0};
  RobotExtractorTester::frameSizes(extractor) =
      {static_cast<uint32_t>(frameData.size())};
  RobotExtractorTester::packetSizes(extractor) =
      {static_cast<uint32_t>(frameData.size())};
  RobotExtractorTester::file(extractor).seekg(0, std::ios::beg);

  nlohmann::json frameJson;
  REQUIRE(RobotExtractorTester::exportFrame(extractor, 0, frameJson));
  REQUIRE(frameJson.contains("cels"));
  REQUIRE(frameJson["cels"].size() == 1);
  const auto &celJson = frameJson["cels"][0];
  CHECK(celJson["width"] == w);
  CHECK(celJson["height"] == h);
  CHECK(celJson["vertical_scale"] == scale);
  CHECK(celJson.contains("palette_required"));

  auto &celBuffer = RobotExtractorTester::celBuffer(extractor);
  REQUIRE(celBuffer.size() == static_cast<size_t>(w) * h);
  CHECK(RobotExtractorTester::celPixelLimit(extractor) ==
        static_cast<size_t>(w) * h);

  fs::remove(input);
  fs::remove_all(outDir);
}
