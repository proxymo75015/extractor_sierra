#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <array>
#include "utilities.hpp"

using robot::detect_endianness;

TEST_CASE("detect_endianness uses version field") {
    namespace fs = std::filesystem;
    // Little-endian sample
    fs::path lePath = fs::temp_directory_path() / "robot_le.rbt";
    {
        std::array<uint8_t, 8> data{};
        data[6] = 0x05; // version 5 little-endian
        data[7] = 0x00;
        std::ofstream out(lePath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    {
        std::ifstream in(lePath, std::ios::binary);
        REQUIRE_FALSE(detect_endianness(in));
    }
    fs::remove(lePath);

    // Big-endian sample
    fs::path bePath = fs::temp_directory_path() / "robot_be.rbt";
    {
        std::array<uint8_t, 8> data{};
        data[6] = 0x00;
        data[7] = 0x05; // version 5 big-endian
        std::ofstream out(bePath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    {
        std::ifstream in(bePath, std::ios::binary);
        REQUIRE(detect_endianness(in));
    }
    fs::remove(bePath);
}
