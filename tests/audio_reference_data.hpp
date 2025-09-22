#pragma once

#include <array>
#include <cstdint>

namespace test_audio_reference {

inline constexpr std::array<int16_t, 16> kScummVmTruncatedEvenBlock = {
    -3070, -3068, -3070, -3072, -3264, -3456, -3648, -3840,
    -4032, -4224, -4416, -4608, -4800, -4992, -5184, -5376,
};

inline constexpr std::array<int16_t, 16> kScummVmFollowupEvenBlock = {
    -1598, -1790, -1822, -1886, -1894, -1910, -1912, -1916,
    -1912, -1910, -1894, -1886, -1822, -1790, -1598, -1470,
};

inline constexpr std::array<int16_t, 16> kScummVmOddPayloadEvenBlock = {
    -3070, -3068, -3260, -3452, -3644, -3836, -4028, -4220,
    -4412, -4604, -4796, -4988, -5180, -5372, -5564, -5756,
};

} // namespace test_audio_reference
