#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

using robot::RobotExtractor;
using robot::RobotExtractorTester;

namespace fs = std::filesystem;

TEST_CASE("Version 4 signature is rejected") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "robot_v4_signature.rbt";
  fs::path outDir = tmpDir / "robot_v4_signature_out";

  std::vector<uint8_t> data = {0x3d, 0x00, 'S', 'O', 'L', '\0', 0x05, 0x00};
  {
    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
  }

  std::error_code ec;
  fs::remove_all(outDir, ec);
  fs::create_directories(outDir);

  RobotExtractor extractor(input, outDir, false);
  REQUIRE_THROWS_WITH(RobotExtractorTester::readHeader(extractor),
                      Catch::Matchers::ContainsSubstring(
                          "Version Robot non supportée: 4"));
}
