#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include "audio_decompression_helpers.hpp"
#include "robot_extractor.hpp"

namespace fs = std::filesystem;

TEST_CASE("Audio blocks reset DPCM predictor between blocks") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "predictor_reset.rbt";
  fs::path outDir = tmpDir / "predictor_reset_out";

  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);

  {
    std::ofstream dummy(input, std::ios::binary);
    dummy.put(0);
  }

  robot::RobotExtractor extractor(input, outDir, true);

  const std::vector<uint8_t> block1 = {0x52, 0x63, 0x74, 0x85, 0x96, 0xA7,
                                       0xB8, 0xC9, 0x10, 0x21, 0x32, 0x43};
  const std::vector<uint8_t> block2 = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC,
                                       0xDE, 0xF0, 0x98, 0xBA, 0xDC, 0xFE};

  int16_t predictor = 0;
  auto expectedBlock1 = audio_test::decompress_without_runway(block1, predictor);
  REQUIRE_FALSE(expectedBlock1.empty());

  predictor = 0;
  auto expectedBlock2 = audio_test::decompress_without_runway(block2, predictor);
  REQUIRE_FALSE(expectedBlock2.empty());

  int16_t continuingPredictor = 0;
  auto continuedBlock1 =
      audio_test::decompress_without_runway(block1, continuingPredictor);
  auto continuedBlock2 =
      audio_test::decompress_without_runway(block2, continuingPredictor);

  REQUIRE(continuedBlock1 == expectedBlock1);
  REQUIRE(continuedBlock2 != expectedBlock2);

  auto block1Bytes = audio_test::to_bytes(block1);
  auto block2Bytes = audio_test::to_bytes(block2);

  constexpr int32_t block1Pos = 2;
  const int32_t block2Pos =
      block1Pos + static_cast<int32_t>(expectedBlock1.size() * 2);

  robot::RobotExtractorTester::processAudioBlock(
      extractor, std::span<const std::byte>(block1Bytes.data(), block1Bytes.size()),
      block1Pos);
  robot::RobotExtractorTester::processAudioBlock(
      extractor, std::span<const std::byte>(block2Bytes.data(), block2Bytes.size()),
      block2Pos);

  auto evenStream = robot::RobotExtractorTester::buildChannelStream(extractor, true);
  auto oddStream = robot::RobotExtractorTester::buildChannelStream(extractor, false);

  REQUIRE(oddStream.empty());

  std::vector<int16_t> expectedCombined;
  expectedCombined.reserve(expectedBlock1.size() + expectedBlock2.size());
  expectedCombined.insert(expectedCombined.end(), expectedBlock1.begin(),
                          expectedBlock1.end());
  expectedCombined.insert(expectedCombined.end(), expectedBlock2.begin(),
                          expectedBlock2.end());

  REQUIRE(evenStream == expectedCombined);
}
