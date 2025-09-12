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

TEST_CASE("Cel with zero width throws") {
  std::vector<uint8_t> data;
  data.reserve(2 + 22);
  push16(data, 1); // numCels

  std::vector<uint8_t> celHeader(22, 0);
  celHeader[1] = 100; // vertical scale
  // width = 0
  celHeader[2] = 0;
  celHeader[3] = 0;
  celHeader[4] = 1; // height
  data.insert(data.end(), celHeader.begin(), celHeader.end());

  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "invalid_cel_width.bin";
  fs::path outDir = tmpDir / "invalid_cel_width_out";
  fs::create_directories(outDir);

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, false);
  extractor.m_hasPalette = true;
  extractor.m_bigEndian = false;
  extractor.m_maxCelsPerFrame = 1;
  extractor.m_frameSizes = {static_cast<uint32_t>(data.size())};
  extractor.m_packetSizes = {static_cast<uint32_t>(data.size())};
  extractor.m_palette.assign(768, std::byte{0});
  extractor.m_fp.seekg(0, std::ios::beg);

  nlohmann::json frameJson;
  try {
    extractor.exportFrame(0, frameJson);
    FAIL("No exception thrown");
  } catch (const std::runtime_error &e) {
    REQUIRE(std::string(e.what()).find("Dimensions de cel invalides") !=
            std::string::npos);
  }
}

TEST_CASE("Cel with zero height throws") {
  std::vector<uint8_t> data;
  data.reserve(2 + 22);
  push16(data, 1); // numCels

  std::vector<uint8_t> celHeader(22, 0);
  celHeader[1] = 100; // vertical scale
  celHeader[2] = 1;   // width
  // height = 0
  celHeader[4] = 0;
  celHeader[5] = 0;
  data.insert(data.end(), celHeader.begin(), celHeader.end());

  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "invalid_cel_height.bin";
  fs::path outDir = tmpDir / "invalid_cel_height_out";
  fs::create_directories(outDir);

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, false);
  extractor.m_hasPalette = true;
  extractor.m_bigEndian = false;
  extractor.m_maxCelsPerFrame = 1;
  extractor.m_frameSizes = {static_cast<uint32_t>(data.size())};
  extractor.m_packetSizes = {static_cast<uint32_t>(data.size())};
  extractor.m_palette.assign(768, std::byte{0});
  extractor.m_fp.seekg(0, std::ios::beg);

  nlohmann::json frameJson;
  try {
    extractor.exportFrame(0, frameJson);
    FAIL("No exception thrown");
  } catch (const std::runtime_error &e) {
    REQUIRE(std::string(e.what()).find("Dimensions de cel invalides") !=
            std::string::npos);
  }
}
