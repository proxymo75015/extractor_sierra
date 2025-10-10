#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;

namespace {

constexpr size_t kZeroPrefixBytes = robot::kRobotZeroCompressSize;
constexpr size_t kRunwaySamples = robot::kRobotRunwaySamples;

std::vector<int16_t> decompress_truncated_block(
    const std::vector<uint8_t> &raw) {
  std::vector<std::byte> block(kZeroPrefixBytes + raw.size(), std::byte{0});
  for (size_t i = 0; i < raw.size(); ++i) {
    block[kZeroPrefixBytes + i] = std::byte{raw[i]};
  }
  int16_t predictor = 0;
  auto samples = robot::dpcm16_decompress(std::span(block), predictor);
  if (samples.size() <= kRunwaySamples) {
    return {};
  }
  return {samples.begin() + static_cast<std::ptrdiff_t>(kRunwaySamples),
          samples.end()};
}

} // namespace

TEST_CASE("Audio routing places odd-position blocks on odd channel") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "offset_two_routing.rbt";
  fs::path outDir = tmpDir / "offset_two_routing_out";

  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);

  {
    std::ofstream dummy(input, std::ios::binary);
    dummy.put(0);
  }

  robot::RobotExtractor extractor(input, outDir, true);

  constexpr int32_t blockPos = 3;

  std::vector<uint8_t> raw = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65};
  auto expectedSamples = decompress_truncated_block(raw);
  REQUIRE_FALSE(expectedSamples.empty());

  std::vector<std::byte> block(kZeroPrefixBytes + raw.size(), std::byte{0});
  for (size_t i = 0; i < raw.size(); ++i) {
    block[kZeroPrefixBytes + i] = std::byte{raw[i]};
  }

  robot::RobotExtractorTester::processAudioBlock(extractor, std::span(block),
                                                 blockPos);

  const auto evenStream =
      robot::RobotExtractorTester::buildChannelStream(extractor, true);
  const auto oddStream =
      robot::RobotExtractorTester::buildChannelStream(extractor, false);

  REQUIRE(evenStream.empty());
  REQUIRE_FALSE(oddStream.empty());

  const int64_t expectedStartSampleSigned =
      blockPos >= 0 ? blockPos / 2 : (static_cast<int64_t>(blockPos) - 1) / 2;
  REQUIRE(expectedStartSampleSigned >= 0);
  const size_t expectedStartSample =
      static_cast<size_t>(expectedStartSampleSigned);
  REQUIRE(oddStream.size() >= expectedStartSample + expectedSamples.size());

  for (size_t i = 0; i < expectedSamples.size(); ++i) {
    CAPTURE(i);
    CAPTURE(expectedStartSample);
    REQUIRE(oddStream[expectedStartSample + i] == expectedSamples[i]);
  }
}
