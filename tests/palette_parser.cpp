#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "palette_helpers.hpp"
#include "robot_extractor.hpp"

namespace fs = std::filesystem;
using robot::RobotExtractorTester;

TEST_CASE("Hunk palette with per-color used flags is parsed") {
  fs::path tmpDir = fs::temp_directory_path() / "palette_parser";
  fs::create_directories(tmpDir);
  fs::path input = tmpDir / "per_color.rbt";
  fs::path outDir = tmpDir / "per_color_out";
  fs::create_directories(outDir);
  if (!fs::exists(input)) {
    std::ofstream out(input, std::ios::binary);
  }

  std::vector<test_palette::Color> colors{
      {true, 10, 20, 30},
      {false, 40, 50, 60},
  };
  auto raw = test_palette::build_hunk_palette(colors, 3, false);

  robot::RobotExtractor extractor(input, outDir, false);
  RobotExtractorTester::palette(extractor) = raw;
  RobotExtractorTester::bigEndian(extractor) = false;
  auto parsed = RobotExtractorTester::parsePalette(extractor);

  REQUIRE(parsed.startColor == 3);
  REQUIRE(parsed.entries[3].present);
  REQUIRE(parsed.entries[3].used);
  REQUIRE(parsed.entries[3].r == 10);
  REQUIRE(parsed.entries[3].g == 20);
  REQUIRE(parsed.entries[3].b == 30);

  REQUIRE(parsed.entries[4].present);
  REQUIRE_FALSE(parsed.entries[4].used);
  REQUIRE(parsed.entries[4].r == 40);
  REQUIRE(parsed.entries[4].g == 50);
  REQUIRE(parsed.entries[4].b == 60);
}

TEST_CASE("Hunk palette preserves remap data and shared flags") {
  fs::path tmpDir = fs::temp_directory_path() / "palette_parser";
  fs::create_directories(tmpDir);
  fs::path input = tmpDir / "shared_used.rbt";
  fs::path outDir = tmpDir / "shared_used_out";
  fs::create_directories(outDir);
  if (!fs::exists(input)) {
    std::ofstream out(input, std::ios::binary);
  }

  std::vector<test_palette::Color> colors{
      {true, 1, 2, 3},
      {true, 4, 5, 6},
      {true, 7, 8, 9},
  };
  std::vector<std::byte> remap = {std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
  auto raw = test_palette::build_hunk_palette(colors, 10, true, false, remap);

  robot::RobotExtractor extractor(input, outDir, false);
  RobotExtractorTester::palette(extractor) = raw;
  RobotExtractorTester::bigEndian(extractor) = false;
  auto parsed = RobotExtractorTester::parsePalette(extractor);

  REQUIRE(parsed.startColor == 10);
  REQUIRE(parsed.sharedUsed);
  REQUIRE_FALSE(parsed.defaultUsed);
  for (size_t i = 10; i < 13; ++i) {
    INFO("palette index " << i);
    REQUIRE(parsed.entries[i].present);
    REQUIRE_FALSE(parsed.entries[i].used);
  }
  REQUIRE(parsed.remapData == remap);
}
