#pragma once

#include <array>
#include <cstdint>

namespace test_audio_reference {

inline constexpr std::array<int16_t, 16> kScummVmTruncatedEvenBlock = {
    -3038, -3036, -3038, -3040, -3232, -3424, -3616, -3808,
    -4000, -4192, -4384, -4576, -4768, -4960, -5152, -5344,
};

inline constexpr std::array<int16_t, 16> kScummVmFollowupEvenBlock = {
    -6942, -7134, -7166, -7230, -7238, -7254, -7256, -7260,
    -7256, -7254, -7238, -7230, -7166, -7134, -6942, -6814,
};

inline constexpr std::array<int16_t, 16> kScummVmOddPayloadEvenBlock = {
    -3038, -3036, -3228, -3420, -3612, -3804, -3996, -4188,
    -4380, -4572, -4764, -4956, -5148, -5340, -5532, -5724,
};

} // namespace test_audio_reference
