#include "robot_extractor.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <limits>

namespace fs = std::filesystem;
using robot::RobotExtractorTester;

TEST_CASE("Les cels énormes déclenchent une exception d'allocation") {
    fs::path tmp = fs::temp_directory_path() / "dummy.bin";
    std::ofstream(tmp).put('\0');
    robot::RobotExtractor extractor(tmp, fs::temp_directory_path(), false);
    RobotExtractorTester::rgbaBuffer(extractor).clear();

    size_t required = std::numeric_limits<size_t>::max() - 1;

    REQUIRE_THROWS([&]() {
        if (required > RobotExtractorTester::rgbaBufferLimit(extractor)) {
            throw std::runtime_error("Tampon RGBA dépasse la limite");
        }
        if (required > RobotExtractorTester::rgbaBuffer(extractor).capacity()) {
            RobotExtractorTester::rgbaBuffer(extractor).reserve(required);
        }
        RobotExtractorTester::rgbaBuffer(extractor).resize(required);        
    }());
}
