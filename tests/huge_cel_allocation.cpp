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
        if (required > extractor.m_rgbaBuffer.capacity()) {
            size_t growth = required / 2;
            size_t max = std::numeric_limits<size_t>::max();
            size_t new_capacity;
            if (required > max - growth) {
                new_capacity = max;
            } else {
                new_capacity = required + growth;
            }
            extractor.m_rgbaBuffer.reserve(new_capacity);
        }
    }());
}
