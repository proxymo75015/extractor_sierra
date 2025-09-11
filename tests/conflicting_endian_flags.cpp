#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;

TEST_CASE("force_be et force_le simultanés provoquent une erreur") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "conflict.rbt";
    {
        std::ofstream out(input, std::ios::binary);
    }
    fs::path outDir = tmpDir / "conflict_out";
    fs::create_directories(outDir);

    robot::ExtractorOptions opt;
    opt.force_be = true;
    opt.force_le = true;

    robot::RobotExtractor extractor(input, outDir, false, opt);
    REQUIRE_THROWS_AS(extractor.extract(), std::runtime_error);
}
