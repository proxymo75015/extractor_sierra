#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;

constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);

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
  push16(h, 0x16); // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);   // version
  push16(h, 24);  // audio block size
  push16(h, 0);   // primerZeroCompressFlag
  push16(h, 0);   // skip
  push16(h, 2);   // numFrames
  push16(h, 0);   // paletteSize
  push16(h, static_cast<uint16_t>(kPrimerHeaderSize));
  push16(h, 1);   // xRes
  push16(h, 1);   // yRes
  h.push_back(0); // hasPalette
  h.push_back(1); // hasAudio
  push16(h, 0);   // skip
  push16(h, 60);  // frameRate
  push16(h, 0);   // isHiRes
  push16(h, 0);   // maxSkippablePackets
  push16(h, 1);   // maxCelsPerFrame
  for (int i = 0; i < 4; ++i)
    push32(h, 0); // champs supplémentaires
  for (int i = 0; i < 2; ++i)
    push32(h, 0); // zone réservée
  return h;
}

static std::vector<uint8_t> build_primer_header() {
  std::vector<uint8_t> p;
  push32(p, kPrimerHeaderSize);
  push16(p, 0); // compType
  push32(p, 0); // even size
  push32(p, 0); // odd size
  return p;
}

TEST_CASE("Audio blocks at pos 2 and 6 use the odd channel") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "odd_channel_route.rbt";
  fs::path outDir = tmpDir / "odd_channel_route_out";
  fs::create_directories(outDir);

  auto data = build_header();
  auto primer = build_primer_header();
  data.insert(data.end(), primer.begin(), primer.end());

  // Frame sizes (2 frames) and packet sizes including audio blocks
  push16(data, 2);
  push16(data, 2);
  push16(data, 26);
  push16(data, 26);

  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  // Pad to 2048-byte boundary
  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  // Frame 0: numCels = 0
  data.push_back(0);
  data.push_back(0);
  push32(data, 2);  // pos routes to odd channel when using bit 1
  push32(data, 10); // compressed data size
  for (int i = 0; i < 10; ++i)
    data.push_back(static_cast<uint8_t>(i));
  for (int i = 0; i < 6; ++i)
    data.push_back(0); // padding to expected block length

  // Frame 1: numCels = 0
  data.push_back(0);
  data.push_back(0);
  push32(data, 6);  // second block must also use odd channel
  push32(data, 10); // compressed data size
  for (int i = 0; i < 10; ++i)
    data.push_back(static_cast<uint8_t>(0x20 + i));
  for (int i = 0; i < 6; ++i)
    data.push_back(0);

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  auto odd0 = outDir / "frame_00000_odd.wav";
  auto odd1 = outDir / "frame_00001_odd.wav";
  auto even0 = outDir / "frame_00000_even.wav";

  REQUIRE(fs::exists(odd0));
  REQUIRE(fs::exists(odd1));
  REQUIRE_FALSE(fs::exists(even0));
}
