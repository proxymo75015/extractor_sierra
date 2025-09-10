#include "robot_extractor.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <limits>

namespace fs = std::filesystem;

TEST_CASE("Les cels énormes déclenchent une exception d'allocation") {
    fs::path tmp = fs::temp_directory_path() / "dummy.bin";
    std::ofstream(tmp).put('\0');
    robot::RobotExtractor extractor(tmp, fs::temp_directory_path(), false);
    extractor.m_rgbaBuffer.clear();

    size_t required = std::numeric_limits<size_t>::max() - 1;

    REQUIRE_THROWS([&]() {
        if (required > robot::RobotExtractor::kMaxCelPixels * 4) {
            throw std::runtime_error("Tampon RGBA dépasse la limite");
        }        
        if (required > extractor.m_rgbaBuffer.capacity()) {
            extractor.m_rgbaBuffer.reserve(required);
        }
        extractor.m_rgbaBuffer.resize(required);
    }());
}
