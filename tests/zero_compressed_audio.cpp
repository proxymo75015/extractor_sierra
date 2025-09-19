#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

#include "robot_extractor.hpp"
#include "utilities.hpp"

namespace fs = std::filesystem;

static void push16(std::vector<uint8_t> &v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>(x >> 8));
}

static void push32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);

static std::vector<uint8_t> build_header() {
  std::vector<uint8_t> h;
  push16(h, 0x16); // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);   // version
  push16(h, 24);  // audio block size
  push16(h, 0);   // primerZeroCompressFlag
  push16(h, 0);   // skip
  push16(h, 1);   // numFrames
  push16(h, 0);   // paletteSize
  push16(h, static_cast<uint16_t>(kPrimerHeaderSize));
  push16(h, 1);   // xRes
  push16(h, 1);   // yRes
  h.push_back(0); // hasPalette
  h.push_back(1); // hasAudio
  push16(h, 0);   // skip
  push16(h, 60);  // frameRate
  push16(h, 0);   // isHiRes
  push16(h, 0);   // maxSkippablePackets
  push16(h, 1);   // maxCelsPerFrame
  for (int i = 0; i < 4; ++i)
    push32(h, 0); // champs supplémentaires
  for (int i = 0; i < 2; ++i)
    push32(h, 0); // zone réservée
  return h;
}

static std::vector<uint8_t> build_primer_header() {
  std::vector<uint8_t> p;
  push32(p, kPrimerHeaderSize);
  push16(p, 0); // compType
  push32(p, 0); // even size
  push32(p, 0); // odd size
  return p;
}

static std::vector<uint8_t>
build_robot_with_audio(int32_t position, uint32_t declaredSize,
                       std::span<const uint8_t> audioBytes) {
  auto data = build_header();
  auto primer = build_primer_header();
  data.insert(data.end(), primer.begin(), primer.end());

  push16(data, 2);  // frame size
  push16(data, 26); // packet size (frame + audio block 24)

  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  data.push_back(0); // numCels low byte
  data.push_back(0); // numCels high byte

  constexpr uint32_t kAudioBlockLen = 24;
  constexpr uint32_t kAudioHeaderLen = 8;
  REQUIRE(declaredSize <= kAudioBlockLen - kAudioHeaderLen);
  REQUIRE(audioBytes.size() == declaredSize);
  push32(data, static_cast<uint32_t>(position));
  push32(data, declaredSize);
  data.insert(data.end(), audioBytes.begin(), audioBytes.end());
  uint32_t padding = kAudioBlockLen - kAudioHeaderLen - declaredSize;
  data.insert(data.end(), padding, 0);  

  return data;
}

TEST_CASE("Zero-compressed audio block expands runway and payload") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "zero_compressed_audio.rbt";
  fs::path outDir = tmpDir / "zero_compressed_audio_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);
  if (fs::exists(input)) {
    fs::remove(input);
  }

  constexpr int kRunwayBytes = 8;
  std::array<uint8_t, 2> audioPayload{0x10, 0x32};
  std::vector<uint8_t> payload(audioPayload.begin(), audioPayload.end());
  auto data = build_robot_with_audio(
      1, static_cast<uint32_t>(payload.size()),
      std::span<const uint8_t>(payload));
  
  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  auto wavOdd = outDir / "frame_00000_odd.wav";
  REQUIRE(fs::exists(wavOdd));

  std::ifstream wavFile(wavOdd, std::ios::binary);
  REQUIRE(wavFile.is_open());

  std::array<char, 44> header{};
  wavFile.read(header.data(), static_cast<std::streamsize>(header.size()));
  REQUIRE(wavFile.gcount() == static_cast<std::streamsize>(header.size()));
  REQUIRE(std::string_view(header.data(), 4) == "RIFF");
  REQUIRE(std::string_view(header.data() + 8, 4) == "WAVE");
  REQUIRE(std::string_view(header.data() + 12, 4) == "fmt ");
  REQUIRE(std::string_view(header.data() + 36, 4) == "data");

  uint32_t dataSize = static_cast<uint8_t>(header[40]) |
                      (static_cast<uint32_t>(static_cast<uint8_t>(header[41]))
                       << 8) |
                      (static_cast<uint32_t>(static_cast<uint8_t>(header[42]))
                       << 16) |
                      (static_cast<uint32_t>(static_cast<uint8_t>(header[43]))
                       << 24);
  std::vector<int16_t> samples(dataSize / 2);
  if (dataSize > 0) {
    wavFile.read(reinterpret_cast<char *>(samples.data()),
                 static_cast<std::streamsize>(dataSize));
    REQUIRE(wavFile.gcount() == static_cast<std::streamsize>(dataSize));
  }

  int16_t predictor = 0;
  std::array<std::byte, 8> zeroRunway{};
  robot::dpcm16_decompress_last(std::span(zeroRunway), predictor);

  std::array<std::byte, 2> payloadBytes{std::byte{audioPayload[0]},
                                        std::byte{audioPayload[1]}};
  constexpr size_t kExpectedPayloadBytes = 8;
  std::vector<std::byte> audioBytes;
  audioBytes.reserve(kExpectedPayloadBytes);
  audioBytes.insert(audioBytes.end(), payloadBytes.begin(), payloadBytes.end());
  if (audioBytes.size() < kExpectedPayloadBytes) {
    audioBytes.resize(kExpectedPayloadBytes, std::byte{0});
  }
  auto expectedSamples =
      robot::dpcm16_decompress(std::span(audioBytes), predictor);

  REQUIRE(expectedSamples.size() == samples.size());
  for (size_t i = 0; i < samples.size(); ++i) {
    REQUIRE(expectedSamples[i] == samples[i]);
  }
}

TEST_CASE("Audio block with zero position is skipped") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "zero_position_audio.rbt";
  fs::path outDir = tmpDir / "zero_position_audio_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);
  if (fs::exists(input)) {
    fs::remove(input);
  }

  constexpr int kRunwayBytes = 8;
  std::array<uint8_t, 8> audioPayload{0x01, 0x23, 0x45, 0x67,
                                      0x89, 0xAB, 0xCD, 0xEF};
  std::vector<uint8_t> audioBlock(kRunwayBytes + audioPayload.size(), 0);
  std::copy(audioPayload.begin(), audioPayload.end(),
            audioBlock.begin() + kRunwayBytes);
  auto data = build_robot_with_audio(
      0, static_cast<uint32_t>(audioBlock.size()),
      std::span<const uint8_t>(audioBlock));

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  auto wavEven = outDir / "frame_00000_even.wav";
  auto wavOdd = outDir / "frame_00000_odd.wav";
  REQUIRE_FALSE(fs::exists(wavEven));
  REQUIRE_FALSE(fs::exists(wavOdd));
}
