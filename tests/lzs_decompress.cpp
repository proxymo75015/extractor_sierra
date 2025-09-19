#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

#include "utilities.hpp"

TEST_CASE("LZS Robot chunk decompression") {
    SECTION("Seven-bit offsets reproduce newly written bytes") {
        // Bytes mimic a Robot (.rbt) cel block compressed with STACpack and
        // exercise the 7-bit offset path used by ScummVM.
        constexpr std::array<std::byte, 4> compressed = {
            std::byte{0x20}, std::byte{0x90}, std::byte{0xB0}, std::byte{0x58}};
        constexpr size_t expected_size = 7;
        std::vector<std::byte> result;
        REQUIRE_NOTHROW(result =
                            robot::lzs_decompress(compressed, expected_size));
        const std::array<std::byte, expected_size> expected = {
            std::byte{0x41}, std::byte{0x42}, std::byte{0x41},
            std::byte{0x42}, std::byte{0x41}, std::byte{0x42},
            std::byte{0x41}};
        std::vector<std::byte> expected_vec(expected.begin(), expected.end());
        REQUIRE(result == expected_vec);
    }

    SECTION("Eleven-bit offsets mirror ScummVM behaviour") {
        // This payload continues the regression coverage with the 11-bit offset
        // variant used by Robot's STACpack compression.
        constexpr std::array<std::byte, 5> compressed = {
            std::byte{0x20}, std::byte{0x90}, std::byte{0xA0},
            std::byte{0x04}, std::byte{0x80}};
        constexpr size_t expected_size = 5;
        std::vector<std::byte> result;
        REQUIRE_NOTHROW(result =
                            robot::lzs_decompress(compressed, expected_size));
        const std::array<std::byte, expected_size> expected = {
            std::byte{0x41}, std::byte{0x42}, std::byte{0x41},
            std::byte{0x42}, std::byte{0x41}};
        std::vector<std::byte> expected_vec(expected.begin(), expected.end());
        REQUIRE(result == expected_vec);
    }
}
