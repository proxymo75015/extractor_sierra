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
    REQUIRE(message.find("Facteur d'échelle vertical invalide") !=
            std::string::npos);
  }
}

TEST_CASE("Vertical scale of zero throws") {
  expect_invalid_vertical_scale(0, "invalid_vertical_scale_zero");
}

// CORRECTION: ScummVM accepte les valeurs > 100
TEST_CASE("Vertical scale above 100 succeeds") {
  expect_valid_vertical_scale(101, "invalid_vertical_scale_above_limit");
}

TEST_CASE("Vertical scale far above limit succeeds") {
  expect_valid_vertical_scale(200, "invalid_vertical_scale_high");
}

// Les fonctions expect_valid_vertical_scale doivent vérifier le succès, pas l'échec
static void expect_valid_vertical_scale(uint8_t verticalScale,
                                        const std::string &caseName) {
  std::vector<uint8_t> data;
  data.reserve(2 + 22);
  push16(data, 1); // numCels

  const uint16_t w = 1;
  const uint16_t h = 1;
  std::vector<uint8_t> celHeader(22, 0);
  celHeader[1] = verticalScale; // verticalScale valide
  celHeader[2] = static_cast<uint8_t>(w & 0xFF); // width
  celHeader[3] = static_cast<uint8_t>(w >> 8);
  celHeader[4] = static_cast<uint8_t>(h & 0xFF); // height
  celHeader[5] = static_cast<uint8_t>(h >> 8);

  const uint32_t sourceHeight =
      std::max<uint32_t>(1u, static_cast<uint32_t>(h) * verticalScale / 100u);
  const uint32_t expectedPixels = static_cast<uint32_t>(w) * sourceHeight;
  const uint16_t dataSize =
      static_cast<uint16_t>(10u + expectedPixels);
  celHeader[14] = static_cast<uint8_t>(dataSize & 0xFF);
  celHeader[15] = static_cast<uint8_t>(dataSize >> 8);
  celHeader[16] = 1; // numChunks
  data.insert(data.end(), celHeader.begin(), celHeader.end());

  std::vector<uint8_t> chunkHeader(10, 0);
  chunkHeader[0] = static_cast<uint8_t>(expectedPixels & 0xFF);
  chunkHeader[1] = static_cast<uint8_t>((expectedPixels >> 8) & 0xFF);
  chunkHeader[4] = chunkHeader[0];
  chunkHeader[5] = chunkHeader[1];
  chunkHeader[8] = 2; // compression brute
  data.insert(data.end(), chunkHeader.begin(), chunkHeader.end());

  data.insert(data.end(), expectedPixels, 0);

  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / (caseName + ".bin");
  fs::path outDir = tmpDir / (caseName + "_out");
  fs::create_directories(outDir);

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, false);

  // CORRECTION: S'attendre à un succès
  REQUIRE_NOTHROW(extractor.extract());

  // Vérifier que les fichiers sont créés
  REQUIRE(fs::exists(outDir / "metadata.json"));

  fs::remove(input);
  fs::remove_all(outDir);
}
