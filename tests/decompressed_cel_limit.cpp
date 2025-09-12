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

TEST_CASE("Cel decompresse au-dela de la limite est refuse") {
  const uint16_t w = 1024;
  const uint16_t h = 1024; // w*h == kMaxCelPixels
  const uint8_t scale = 150; // sourceHeight = 1536 -> expected > kMaxCelPixels

  std::vector<uint8_t> data;
  data.reserve(2 + 22);
  push16(data, 1); // numCels

  std::vector<uint8_t> celHeader(22, 0);
  celHeader[1] = scale; // vertical scale
  celHeader[2] = static_cast<uint8_t>(w & 0xFF);
  celHeader[3] = static_cast<uint8_t>(w >> 8);
  celHeader[4] = static_cast<uint8_t>(h & 0xFF);
  celHeader[5] = static_cast<uint8_t>(h >> 8);
  // dataSize and numChunks left to zero
  data.insert(data.end(), celHeader.begin(), celHeader.end());

  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "cel_limit.bin";
  fs::path outDir = tmpDir / "cel_limit_out";
  fs::create_directories(outDir);

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, false);
  RobotExtractorTester::hasPalette(extractor) = true;
  RobotExtractorTester::bigEndian(extractor) = false;
  RobotExtractorTester::maxCelsPerFrame(extractor) = 1;
  RobotExtractorTester::frameSizes(extractor) = {static_cast<uint32_t>(data.size())};
  RobotExtractorTester::packetSizes(extractor) = {static_cast<uint32_t>(data.size())};
  RobotExtractorTester::palette(extractor).assign(768, std::byte{0});
  RobotExtractorTester::file(extractor).seekg(0, std::ios::beg);

  nlohmann::json frameJson;
  try {
    RobotExtractorTester::exportFrame(extractor, 0, frameJson);
    FAIL("No exception thrown");
  } catch (const std::runtime_error &e) {
    REQUIRE(std::string(e.what()).find(
                "Cel décompressé dépasse la taille maximale") !=
            std::string::npos);
  }
}
