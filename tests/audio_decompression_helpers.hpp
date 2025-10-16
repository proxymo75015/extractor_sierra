#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <span>
#include <stdexcept>
#include <vector>

#include "robot_extractor.hpp"

namespace audio_test {

inline std::vector<std::byte>
to_bytes(const std::vector<uint8_t> &src) {
  std::vector<std::byte> out;
  out.reserve(src.size());
  for (uint8_t b : src) {
    out.push_back(static_cast<std::byte>(b));
  }
  return out;
}

inline std::vector<int16_t>
decompress_without_runway(std::span<const std::byte> bytes,
                          int16_t &predictor) {
  auto pcm = robot::dpcm16_decompress(bytes, predictor);
  if (pcm.size() <= robot::kRobotRunwaySamples) {
    return {};
  }
  return {pcm.begin() + static_cast<std::ptrdiff_t>(robot::kRobotRunwaySamples),
          pcm.end()};
}

inline std::vector<int16_t>
decompress_without_runway(const std::vector<uint8_t> &src,
                          int16_t &predictor) {
  auto bytes = to_bytes(src);
  return decompress_without_runway(
      std::span<const std::byte>(bytes.data(), bytes.size()), predictor);
}

inline std::vector<int16_t>
decompress_without_runway(const std::vector<std::byte> &src,
                          int16_t &predictor) {
  return decompress_without_runway(
      std::span<const std::byte>(src.data(), src.size()), predictor);
}

struct ChannelExpectation {
  bool initialized = false;
  int64_t startHalfPos = 0;
  std::vector<int16_t> samples;
};

inline void append_expected(ChannelExpectation &channel, int32_t halfPos,
                            const std::vector<int16_t> &samples) {
  if (samples.empty()) {
    return;
  }
  int64_t pos = static_cast<int64_t>(halfPos);
  if (!channel.initialized) {
    channel.startHalfPos = pos;
    channel.initialized = true;
  } else if (pos < channel.startHalfPos) {
    const int64_t deltaHalf = channel.startHalfPos - pos;
    if ((deltaHalf & 1LL) != 0) {
      throw std::runtime_error("Parity mismatch in expected audio layout");
    }
    const size_t deltaSamples = static_cast<size_t>(deltaHalf / 2);
    channel.samples.insert(channel.samples.begin(), deltaSamples, 0);
    channel.startHalfPos = pos;
  }
  int64_t adjustedHalf = pos - channel.startHalfPos;
  int64_t startSampleSigned =
      adjustedHalf >= 0 ? adjustedHalf / 2 : (adjustedHalf - 1) / 2;
  size_t skip = 0;
  if (startSampleSigned < 0) {
    skip = static_cast<size_t>(-startSampleSigned);
    if (skip >= samples.size()) {
      return;
    }
    startSampleSigned = 0;
  }
  const size_t startSample = static_cast<size_t>(startSampleSigned);
  const size_t available = samples.size() - skip;
  const size_t requiredSize = startSample + available;
  if (channel.samples.size() < requiredSize) {
    channel.samples.resize(requiredSize, 0);
  }
  std::copy(samples.begin() + static_cast<std::ptrdiff_t>(skip), samples.end(),
            channel.samples.begin() + static_cast<std::ptrdiff_t>(startSample));
}

} // namespace audio_test
