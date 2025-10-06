#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <span>
#include <vector>

#include "utilities.hpp"

namespace {

constexpr std::array<std::byte, 12> kPrimerBytes = {
    std::byte{0x10}, std::byte{0x32}, std::byte{0x54}, std::byte{0x76},
    std::byte{0x98}, std::byte{0xBA}, std::byte{0xDC}, std::byte{0xFE},
    std::byte{0x13}, std::byte{0x57}, std::byte{0x9B}, std::byte{0xDF}};

constexpr std::array<int16_t, 12> kPrimerExpected = {
    240, 888, 1808, 5136, 4768, 4056, 3072, -9216, -8928, -7984, -8400, -9408};

constexpr std::array<std::byte, 8> kStreamBytes = {
    std::byte{0x21}, std::byte{0x43}, std::byte{0x65}, std::byte{0x87},
    std::byte{0xA9}, std::byte{0xCB}, std::byte{0xED}, std::byte{0x0F}};

constexpr std::array<int16_t, 8> kStreamExpected = {
    -8896, -8112, -6832, -6928, -7504, -8352, -10144, -9920};

} // namespace

TEST_CASE("DPCM16 primer decoding matches ScummVM reference output") {
    int16_t predictor = 0;
    auto decoded = robot::dpcm16_decompress(std::span(kPrimerBytes), predictor);
    const std::vector<int16_t> expected(kPrimerExpected.begin(), kPrimerExpected.end());

    REQUIRE(decoded == expected);
    REQUIRE(predictor == expected.back());
}

TEST_CASE("DPCM16 streaming blocks remain aligned with ScummVM") {
    int16_t predictor = 0;
    auto primerDecoded = robot::dpcm16_decompress(std::span(kPrimerBytes), predictor);
    const std::vector<int16_t> primerExpected(kPrimerExpected.begin(),
                                              kPrimerExpected.end());
    REQUIRE(primerDecoded == primerExpected);

    auto streamDecoded = robot::dpcm16_decompress(std::span(kStreamBytes), predictor);
    const std::vector<int16_t> streamExpected(kStreamExpected.begin(),
                                              kStreamExpected.end());
    REQUIRE(streamDecoded == streamExpected);
    REQUIRE(predictor == streamExpected.back());

    int16_t predictorLast = kPrimerExpected.back();
    robot::dpcm16_decompress_last(std::span(kStreamBytes), predictorLast);
    REQUIRE(predictorLast == streamExpected.back());
}
