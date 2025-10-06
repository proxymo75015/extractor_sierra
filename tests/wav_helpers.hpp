#pragma once

#include <array>
#include <cstdint>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

struct WavLayout {
  uint16_t numChannels = 0;
  uint32_t sampleRate = 0;
  uint32_t byteRate = 0;
  uint16_t blockAlign = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataSize = 0;
};

inline WavLayout read_wav_layout(std::ifstream &wav,
                                 uint16_t expectedChannels = 2,
                                 uint16_t expectedBitsPerSample = 16) {
  WavLayout layout{};
  std::array<unsigned char, 44> header{};
  wav.read(reinterpret_cast<char *>(header.data()),
           static_cast<std::streamsize>(header.size()));
  REQUIRE(wav.gcount() == static_cast<std::streamsize>(header.size()));

  REQUIRE(header[0] == 'R');
  REQUIRE(header[1] == 'I');
  REQUIRE(header[2] == 'F');
  REQUIRE(header[3] == 'F');
  REQUIRE(header[8] == 'W');
  REQUIRE(header[9] == 'A');
  REQUIRE(header[10] == 'V');
  REQUIRE(header[11] == 'E');
  REQUIRE(header[12] == 'f');
  REQUIRE(header[13] == 'm');
  REQUIRE(header[14] == 't');
  REQUIRE(header[15] == ' ');

  layout.numChannels = static_cast<uint16_t>(header[22]) |
                       (static_cast<uint16_t>(header[23]) << 8);
  layout.sampleRate = static_cast<uint32_t>(header[24]) |
                      (static_cast<uint32_t>(header[25]) << 8) |
                      (static_cast<uint32_t>(header[26]) << 16) |
                      (static_cast<uint32_t>(header[27]) << 24);
  layout.byteRate = static_cast<uint32_t>(header[28]) |
                    (static_cast<uint32_t>(header[29]) << 8) |
                    (static_cast<uint32_t>(header[30]) << 16) |
                    (static_cast<uint32_t>(header[31]) << 24);
  layout.blockAlign = static_cast<uint16_t>(header[32]) |
                      (static_cast<uint16_t>(header[33]) << 8);
  layout.bitsPerSample = static_cast<uint16_t>(header[34]) |
                         (static_cast<uint16_t>(header[35]) << 8);
  layout.dataSize = static_cast<uint32_t>(header[40]) |
                    (static_cast<uint32_t>(header[41]) << 8) |
                    (static_cast<uint32_t>(header[42]) << 16) |
                    (static_cast<uint32_t>(header[43]) << 24);

  if (expectedChannels != 0) {
    REQUIRE(layout.numChannels == expectedChannels);
  }
  if (expectedBitsPerSample != 0) {
    REQUIRE(layout.bitsPerSample == expectedBitsPerSample);
  }
  REQUIRE(layout.bitsPerSample % 8 == 0);
  const uint16_t bytesPerSample =
      static_cast<uint16_t>(layout.bitsPerSample / 8);
  REQUIRE(layout.blockAlign == layout.numChannels * bytesPerSample);
  REQUIRE(layout.byteRate == layout.sampleRate * layout.blockAlign);

  return layout;
}
