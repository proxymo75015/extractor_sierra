#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "robot_extractor.hpp"
#include "palette_helpers.hpp"

namespace fs = std::filesystem;
using robot::RobotExtractorTester;

static void push16(std::vector<uint8_t> &v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>(x >> 8));
}

static void expect_invalid_vertical_scale(uint8_t verticalScale,
                                          const std::string &caseName) {
  std::vector<uint8_t> data;
  data.reserve(2 + 22);
  push16(data, 1); // numCels

  std::vector<uint8_t> celHeader(22, 0);
  celHeader[1] = verticalScale; // verticalScale invalide
  celHeader[2] = 1; // width
  celHeader[4] = 1; // height
  data.insert(data.end(), celHeader.begin(), celHeader.end());

  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / (caseName + ".bin");
  fs::path outDir = tmpDir / (caseName + "_out");
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
  RobotExtractorTester::palette(extractor) =
      test_palette::build_flat_palette(0, 0, 0);
  RobotExtractorTester::file(extractor).seekg(0, std::ios::beg);

  nlohmann::json frameJson;
  try {
    RobotExtractorTester::exportFrame(extractor, 0, frameJson);
    FAIL("No exception thrown");
  } catch (const std::runtime_error &e) {
    const std::string message = e.what();
    INFO("exception message: " << message);
    if (verticalScale == 0) {
      REQUIRE(message.find(
                  "Facteur d'échelle vertical invalide (valeur attendue entre 1 et 100)") !=
              std::string::npos);
    } else {
      REQUIRE_FALSE(message.empty());
    }
  }
}

TEST_CASE("Vertical scale of zero throws") {
  expect_invalid_vertical_scale(0, "invalid_vertical_scale_zero");
}

TEST_CASE("Vertical scale above 100 throws") {
  expect_invalid_vertical_scale(101, "invalid_vertical_scale_above_limit");
}

TEST_CASE("Vertical scale far above limit still throws") {
  expect_invalid_vertical_scale(200, "invalid_vertical_scale_high");
}
