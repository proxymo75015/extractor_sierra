#include <catch2/catch_test_macros.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"
#include "audio_decompression_helpers.hpp"

using robot::RobotExtractor;
using robot::RobotExtractorTester;

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

static std::vector<uint8_t> build_header(uint16_t primerSize) {
  std::vector<uint8_t> h;
  push16(h, 0x16); // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);          // version
  push16(h, 8);          // audio block size
  push16(h, 0);          // primerZeroCompressFlag
  push16(h, 0);          // skip
  push16(h, 1);          // numFrames
  push16(h, 0);          // paletteSize
  push16(h, primerSize); // primerReservedSize
  push16(h, 1);          // xRes
  push16(h, 1);          // yRes
  h.push_back(0);        // hasPalette
  h.push_back(1);        // hasAudio
  push16(h, 0);          // skip
  push16(h, 60);         // frameRate
  push16(h, 0);          // isHiRes
  push16(h, 0);          // maxSkippablePackets
  push16(h, 1);          // maxCelsPerFrame
  for (int i = 0; i < 4; ++i)
    push32(h, 0); // champs supplémentaires
  for (int i = 0; i < 2; ++i)
    push32(h, 0); // zone réservée
  return h;
}

static std::vector<uint8_t>
build_primer_header(uint32_t total, uint32_t evenSize, uint32_t oddSize) {
  std::vector<uint8_t> p;
  push32(p, total);
  push16(p, 0); // compType
  push32(p, evenSize);
  push32(p, oddSize);
  return p;
}

TEST_CASE("Primer mismatch preserves primer data while realigning stream") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "primer_reserved_mismatch.rbt";
  fs::path outDir = tmpDir / "primer_reserved_mismatch_out";
  fs::remove_all(outDir);
  fs::create_directories(outDir);

  constexpr uint32_t kEvenPrimerSize = 12;
  constexpr uint32_t kOddPrimerSize = 12;
  const uint32_t totalPrimerSize =
      kPrimerHeaderSize + kEvenPrimerSize + kOddPrimerSize;
  auto primerHeader =
      build_primer_header(totalPrimerSize, kEvenPrimerSize, kOddPrimerSize);
  const uint16_t reservedSize =
      static_cast<uint16_t>(primerHeader.size() + kEvenPrimerSize +
                            kOddPrimerSize + 10); // intentionally larger
  auto data = build_header(reservedSize);
  data.insert(data.end(), primerHeader.begin(), primerHeader.end());
  const std::array<uint8_t, kEvenPrimerSize> evenPrimerBytes{
      0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x12, 0x34, 0x56, 0x78};
  data.insert(data.end(), evenPrimerBytes.begin(), evenPrimerBytes.end());
  const std::array<uint8_t, kOddPrimerSize> oddPrimerBytes{
      0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44};
  data.insert(data.end(), oddPrimerBytes.begin(), oddPrimerBytes.end());
  const size_t fillerSize = static_cast<size_t>(reservedSize) -
                            primerHeader.size() - evenPrimerBytes.size() -
                            oddPrimerBytes.size();
  data.insert(data.end(), fillerSize, 0xAB); // filler primer bytes
  data.push_back(0xCC); // sentinel after reserved area
  data.push_back(0xDD);

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(RobotExtractorTester::readHeader(extractor));
  auto &file = RobotExtractorTester::file(extractor);
  const std::streamoff primerHeaderPos = file.tellg();

  REQUIRE_NOTHROW(RobotExtractorTester::readPrimer(extractor));

  const std::streamoff expectedPos =
      primerHeaderPos + static_cast<std::streamoff>(reservedSize);
  REQUIRE(file.tellg() == expectedPos);
  
  REQUIRE(RobotExtractorTester::evenPrimerSize(extractor) ==
          static_cast<std::streamsize>(kEvenPrimerSize));
  REQUIRE(RobotExtractorTester::oddPrimerSize(extractor) ==
          static_cast<std::streamsize>(kOddPrimerSize));

  const auto &evenPrimer = RobotExtractorTester::evenPrimer(extractor);
  const auto &oddPrimer = RobotExtractorTester::oddPrimer(extractor);
  REQUIRE(evenPrimer.size() == evenPrimerBytes.size());
  REQUIRE(oddPrimer.size() == oddPrimerBytes.size());
  for (size_t i = 0; i < evenPrimer.size(); ++i) {
    REQUIRE(evenPrimer[i] == std::byte{evenPrimerBytes[i]});
  }
  for (size_t i = 0; i < oddPrimer.size(); ++i) {
    REQUIRE(oddPrimer[i] == std::byte{oddPrimerBytes[i]});
  }

  const std::vector<uint8_t> evenPrimerVec(evenPrimerBytes.begin(),
                                           evenPrimerBytes.end());
  const std::vector<uint8_t> oddPrimerVec(oddPrimerBytes.begin(),
                                          oddPrimerBytes.end());
  int16_t evenBlockPredictor = 0;
  const auto evenExpected =
      audio_test::decompress_without_runway(evenPrimerVec, evenBlockPredictor);
  int16_t oddBlockPredictor = 0;
  const auto oddExpected =
      audio_test::decompress_without_runway(oddPrimerVec, oddBlockPredictor);
  
  RobotExtractorTester::finalizeAudio(extractor);

  const auto evenStream =
      RobotExtractorTester::buildChannelStream(extractor, true);
  const auto oddStream =
      RobotExtractorTester::buildChannelStream(extractor, false);
  REQUIRE(evenStream == evenExpected);
  REQUIRE(oddStream == oddExpected);

  fs::path stereoWav = outDir / "frame_00000.wav";
  if (!evenExpected.empty() || !oddExpected.empty()) {
    REQUIRE(fs::exists(stereoWav));
  } else {
    REQUIRE_FALSE(fs::exists(stereoWav));
  }

  int nextByte = file.peek();
  REQUIRE(nextByte == 0xCC);
}
