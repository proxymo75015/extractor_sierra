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

TEST_CASE("Hunk palette respects offset table ordering and explicit remap") {
  fs::path tmpDir = fs::temp_directory_path() / "palette_parser";
  fs::create_directories(tmpDir);
  fs::path input = tmpDir / "offset_table.rbt";
  fs::path outDir = tmpDir / "offset_table_out";
  fs::create_directories(outDir);
  if (!fs::exists(input)) {
    std::ofstream out(input, std::ios::binary);
  }

  test_palette::EntrySpec lateEntry;
  lateEntry.offset = 70;
  lateEntry.startColor = 20;
  lateEntry.sharedUsed = false;
  lateEntry.defaultUsed = true;
  lateEntry.version = 3;
  lateEntry.colors = {{true, 100, 110, 120}, {false, 130, 140, 150}};

  test_palette::EntrySpec earlyEntry;
  earlyEntry.offset = 40;
  earlyEntry.startColor = 5;
  earlyEntry.sharedUsed = true;
  earlyEntry.defaultUsed = false;
  earlyEntry.version = 2;
  earlyEntry.colors = {{true, 10, 20, 30}, {true, 40, 50, 60}, {true, 70, 80, 90}};

  std::vector<std::byte> remap = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
  auto raw = test_palette::build_hunk_palette_with_offsets(
      {lateEntry, earlyEntry}, remap, static_cast<uint16_t>(108));

  robot::RobotExtractor extractor(input, outDir, false);
  RobotExtractorTester::palette(extractor) = raw;
  RobotExtractorTester::bigEndian(extractor) = false;
  auto parsed = RobotExtractorTester::parsePalette(extractor);

  REQUIRE(parsed.startColor == 5);
  REQUIRE(parsed.colorCount == 17);
  REQUIRE_FALSE(parsed.sharedUsed);
  REQUIRE_FALSE(parsed.defaultUsed);

  for (uint8_t i = 5; i < 8; ++i) {
    INFO("shared block index " << static_cast<int>(i));
    REQUIRE(parsed.entries[i].present);
    REQUIRE_FALSE(parsed.entries[i].used);
  }
  REQUIRE(parsed.entries[5].r == 10);
  REQUIRE(parsed.entries[6].g == 50);
  REQUIRE(parsed.entries[7].b == 90);

  REQUIRE(parsed.entries[20].present);
  REQUIRE(parsed.entries[20].used);
  REQUIRE(parsed.entries[20].r == 100);
  REQUIRE(parsed.entries[20].g == 110);
  REQUIRE(parsed.entries[20].b == 120);

  REQUIRE(parsed.entries[21].present);
  REQUIRE_FALSE(parsed.entries[21].used);
  REQUIRE(parsed.entries[21].r == 130);
  REQUIRE(parsed.entries[21].g == 140);
  REQUIRE(parsed.entries[21].b == 150);

  REQUIRE(parsed.remapData == remap);
}

TEST_CASE("Hunk palette clamps entries that exceed palette capacity") {
  fs::path tmpDir = fs::temp_directory_path() / "palette_parser";
  fs::create_directories(tmpDir);
  fs::path input = tmpDir / "overflow.rbt";
  fs::path outDir = tmpDir / "overflow_out";
  fs::create_directories(outDir);
  if (!fs::exists(input)) {
    std::ofstream out(input, std::ios::binary);
  }

  std::vector<test_palette::Color> colors;
  for (int i = 0; i < 10; ++i) {
    colors.push_back(test_palette::Color{
        (i % 2) == 0,
        static_cast<uint8_t>(10 * i + 1),
        static_cast<uint8_t>(10 * i + 2),
        static_cast<uint8_t>(10 * i + 3),
    });
  }

  auto raw = test_palette::build_hunk_palette(colors, 250, false);

  robot::RobotExtractor extractor(input, outDir, false);
  RobotExtractorTester::palette(extractor) = raw;
  RobotExtractorTester::bigEndian(extractor) = false;
  auto parsed = RobotExtractorTester::parsePalette(extractor);

  REQUIRE(parsed.startColor == 250);
  REQUIRE(parsed.colorCount == 6);
  REQUIRE_FALSE(parsed.sharedUsed);
  REQUIRE(parsed.defaultUsed);

  for (size_t i = 0; i < 6; ++i) {
    const size_t paletteIndex = 250 + i;
    INFO("palette index " << paletteIndex);
    REQUIRE(parsed.entries[paletteIndex].present);
    REQUIRE(parsed.entries[paletteIndex].used == colors[i].used);
    REQUIRE(parsed.entries[paletteIndex].r == colors[i].r);
    REQUIRE(parsed.entries[paletteIndex].g == colors[i].g);
    REQUIRE(parsed.entries[paletteIndex].b == colors[i].b);
  }

  REQUIRE_FALSE(parsed.entries[249].present);
  REQUIRE(parsed.entries[255].present);
  REQUIRE(parsed.entries[255].r == colors[5].r);
  REQUIRE(parsed.entries[255].g == colors[5].g);
  REQUIRE(parsed.entries[255].b == colors[5].b);
}
