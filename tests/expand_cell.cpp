#include <catch2/catch_test_macros.hpp>
#include <vector>
#include "robot_extractor.hpp"

using robot::expand_cel;

TEST_CASE("expand_cel enlarges image when scale > 100") {
    const uint16_t w = 3;
    const uint16_t h = 2;
    const uint8_t scale = 200; // 2x height
    const int newH = (h * scale) / 100;

    std::vector<std::byte> source{
        std::byte{1}, std::byte{2}, std::byte{3},
        std::byte{4}, std::byte{5}, std::byte{6}
    };

    std::vector<std::byte> expected{
        std::byte{1}, std::byte{2}, std::byte{3},
        std::byte{1}, std::byte{2}, std::byte{3},
        std::byte{4}, std::byte{5}, std::byte{6},
        std::byte{4}, std::byte{5}, std::byte{6}
    };

    std::vector<std::byte> target(static_cast<size_t>(w) * newH);
    expand_cel(target, source, w, h, scale);

    REQUIRE(target == expected);
}

TEST_CASE("expand_cel reduces image when scale < 100") {
    const uint16_t w = 3;
    const uint16_t h = 4;
    const uint8_t scale = 50; // half height
    const int newH = (h * scale) / 100;

    std::vector<std::byte> source{
        std::byte{10}, std::byte{11}, std::byte{12},
        std::byte{20}, std::byte{21}, std::byte{22},
        std::byte{30}, std::byte{31}, std::byte{32},
        std::byte{40}, std::byte{41}, std::byte{42}
    };

    std::vector<std::byte> expected{
        std::byte{10}, std::byte{11}, std::byte{12},
        std::byte{30}, std::byte{31}, std::byte{32}
    };

    std::vector<std::byte> target(static_cast<size_t>(w) * newH);
    expand_cel(target, source, w, h, scale);

    REQUIRE(target == expected);
}
