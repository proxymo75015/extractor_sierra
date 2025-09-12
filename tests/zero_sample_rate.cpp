#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;

TEST_CASE("Sample rate zero triggers exception") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "zero_sample_rate.rbt";
    fs::path outDir = tmpDir / "zero_sample_rate_out";
    fs::create_directories(outDir);

    // Create minimal input file required by constructor
    std::ofstream(input).close();

    robot::RobotExtractor extractor(input, outDir, true);

    std::vector<int16_t> samples{0, 1};
    REQUIRE_THROWS_AS(extractor.writeWav(samples, 0, 0, true), std::runtime_error);
}
