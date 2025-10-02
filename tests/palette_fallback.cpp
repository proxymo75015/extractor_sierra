#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "palette_helpers.hpp"
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

static void appendCel(std::vector<uint8_t> &data, uint8_t pixelValue) {
  const uint16_t w = 1;
  const uint16_t h = 1;
  const uint16_t dataSize = 10 + 1;

  const size_t headerStart = data.size();
  data.resize(headerStart + 22);
  auto *celHeader = data.data() + static_cast<std::ptrdiff_t>(headerStart);
  celHeader[1] = 100;
  celHeader[2] = static_cast<uint8_t>(w & 0xFF);
  celHeader[3] = static_cast<uint8_t>(w >> 8);
  celHeader[4] = static_cast<uint8_t>(h & 0xFF);
  celHeader[5] = static_cast<uint8_t>(h >> 8);
  celHeader[14] = static_cast<uint8_t>(dataSize & 0xFF);
  celHeader[15] = static_cast<uint8_t>(dataSize >> 8);
  celHeader[16] = 1;
  celHeader[17] = 0;

  push32(data, 1);
  push32(data, 1);
  push16(data, 2);
  data.push_back(pixelValue);
}

TEST_CASE("Malformed palettes fall back to raw dump") {
  std::vector<uint8_t> frameData;
  push16(frameData, 1);
  appendCel(frameData, 0x11);

  std::vector<test_palette::Color> colors{{true, 10, 20, 30},
                                          {true, 40, 50, 60}};
  auto rawPalette = test_palette::build_hunk_palette(colors, 0, false);

  const uint16_t invalidOffset =
      static_cast<uint16_t>(rawPalette.size() + 64);
  test_palette::write_u16(rawPalette, test_palette::kHunkPaletteHeaderSize,
                          invalidOffset, false);

  fs::path tmpDir = fs::temp_directory_path() / "palette_fallback";
  fs::create_directories(tmpDir);
  fs::path input = tmpDir / "frame.bin";
  fs::path outDir = tmpDir / "out";
  fs::create_directories(outDir);

  {
    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(frameData.data()),
              static_cast<std::streamsize>(frameData.size()));
  }

  robot::RobotExtractor extractor(input, outDir, false);
  RobotExtractorTester::hasPalette(extractor) = true;
  RobotExtractorTester::palette(extractor) = rawPalette;
  RobotExtractorTester::bigEndian(extractor) = false;
  RobotExtractorTester::maxCelsPerFrame(extractor) = 1;
  RobotExtractorTester::frameSizes(extractor) =
      {static_cast<uint32_t>(frameData.size())};
  RobotExtractorTester::packetSizes(extractor) =
      {static_cast<uint32_t>(frameData.size())};
  RobotExtractorTester::file(extractor).seekg(0, std::ios::beg);

  nlohmann::json frameJson;
  bool exported = false;
  REQUIRE_NOTHROW(exported = RobotExtractorTester::exportFrame(extractor, 0, frameJson));
  REQUIRE(exported);

  REQUIRE(frameJson.contains("palette_parse_failed"));
  REQUIRE(frameJson["palette_parse_failed"].get<bool>());
  REQUIRE(frameJson.contains("palette_raw"));
  REQUIRE(frameJson["palette_raw"].get<std::string>() == "palette.raw");
  REQUIRE(frameJson["cels"].size() == 1);
  REQUIRE(frameJson["cels"][0]["palette_required"].get<bool>());

  auto palettePath = outDir / "palette.raw";
  REQUIRE(fs::exists(palettePath));
  REQUIRE_FALSE(fs::exists(outDir / "00000_0.png"));

  std::ifstream paletteFile(palettePath, std::ios::binary);
  REQUIRE(paletteFile);
  std::vector<char> fileBytes((std::istreambuf_iterator<char>(paletteFile)),
                              std::istreambuf_iterator<char>());
  std::vector<std::byte> diskBytes(fileBytes.size());
  for (size_t i = 0; i < fileBytes.size(); ++i) {
    diskBytes[i] = std::byte{static_cast<unsigned char>(fileBytes[i])};
  }

  REQUIRE(diskBytes == RobotExtractorTester::palette(extractor));
}
