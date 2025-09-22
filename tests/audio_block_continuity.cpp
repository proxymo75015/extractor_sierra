#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include "robot_extractor.hpp"
#include "utilities.hpp"

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);
constexpr uint16_t kAudioBlockSize = 24;
constexpr size_t kRunwayBytes = 8;
constexpr size_t kRunwaySamples = kRunwayBytes * 2;

void push16(std::vector<uint8_t> &v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>(x >> 8));
}

void push32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

std::vector<uint8_t> build_header(uint16_t primerReserved) {
  std::vector<uint8_t> h;
  push16(h, 0x16); // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);                 // version
  push16(h, kAudioBlockSize);   // audio block size
  push16(h, 0);                 // primerZeroCompressFlag
  push16(h, 0);                 // skip
  push16(h, 1);                 // numFrames
  push16(h, 0);                 // paletteSize
  push16(h, primerReserved);    // primerReservedSize
  push16(h, 1);                 // xRes
  push16(h, 1);                 // yRes
  h.push_back(0);               // hasPalette
  h.push_back(1);               // hasAudio
  push16(h, 0);                 // skip
  push16(h, 60);                // frameRate
  push16(h, 0);                 // isHiRes
  push16(h, 0);                 // maxSkippablePackets
  push16(h, 1);                 // maxCelsPerFrame
  for (int i = 0; i < 4; ++i)
    push32(h, 0); // champs supplémentaires
  for (int i = 0; i < 2; ++i)
    push32(h, 0); // zone réservée
  return h;
}

std::vector<uint8_t> build_primer_header(uint32_t total, uint32_t evenSize,
                                         uint32_t oddSize) {
  std::vector<uint8_t> p;
  push32(p, total);
  push16(p, 0); // compType
  push32(p, evenSize);
  push32(p, oddSize);
  return p;
}

std::vector<std::byte> to_bytes(const std::vector<uint8_t> &src) {
  std::vector<std::byte> out;
  out.reserve(src.size());
  for (uint8_t b : src) {
    out.push_back(static_cast<std::byte>(b));
  }
  return out;
}

std::vector<int16_t> read_wav_samples(const fs::path &wavPath) {
  std::ifstream wav(wavPath, std::ios::binary);
  REQUIRE(wav);
  wav.seekg(0, std::ios::end);
  auto fileSize = wav.tellg();
  REQUIRE(fileSize >= 44);
  wav.seekg(40, std::ios::beg);
  std::array<unsigned char, 4> dataSizeBytes{};
  wav.read(reinterpret_cast<char *>(dataSizeBytes.data()),
           static_cast<std::streamsize>(dataSizeBytes.size()));
  REQUIRE(wav);
  uint32_t dataBytes = static_cast<uint32_t>(dataSizeBytes[0]) |
                       (static_cast<uint32_t>(dataSizeBytes[1]) << 8) |
                       (static_cast<uint32_t>(dataSizeBytes[2]) << 16) |
                       (static_cast<uint32_t>(dataSizeBytes[3]) << 24);
  REQUIRE(dataBytes % 2 == 0);
  wav.seekg(44, std::ios::beg);
  std::vector<int16_t> samples;
  samples.reserve(dataBytes / 2);
  for (uint32_t i = 0; i < dataBytes / 2; ++i) {
    std::array<unsigned char, 2> sampleBytes{};
    wav.read(reinterpret_cast<char *>(sampleBytes.data()),
             static_cast<std::streamsize>(sampleBytes.size()));
    REQUIRE(wav);
    uint16_t lo = sampleBytes[0];
    uint16_t hi = static_cast<uint16_t>(sampleBytes[1]) << 8;
    samples.push_back(static_cast<int16_t>(lo | hi));
  }
  return samples;
}

} // namespace

TEST_CASE("Audio block preserves runway samples and primer continuity") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "continuity.rbt";
  fs::path outDir = tmpDir / "continuity_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);

  std::vector<uint8_t> primerData = {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA,
                                     0xDC, 0xFE, 0x13, 0x57, 0x9B, 0xDF};
  std::vector<uint8_t> blockPayload = {0x21, 0x43, 0x65, 0x87,
                                       0xA9, 0xCB, 0xED, 0x0F};
  std::vector<uint8_t> blockData(primerData.end() - kRunwayBytes,
                                 primerData.end());
  blockData.insert(blockData.end(), blockPayload.begin(), blockPayload.end());

  auto primerBytes = to_bytes(primerData);
  auto blockBytes = to_bytes(blockData);

  int16_t primerPredictor = 0;
  auto primerFull =
      robot::dpcm16_decompress(std::span<const std::byte>(primerBytes),
                               primerPredictor);
  REQUIRE(primerFull.size() >= kRunwaySamples);
  std::vector<int16_t> expectedPrimer;
  if (primerFull.size() > kRunwaySamples) {
    expectedPrimer.assign(primerFull.begin() +
                              static_cast<std::ptrdiff_t>(kRunwaySamples),
                          primerFull.end());
  }

  int16_t blockPredictor = 0;
  auto blockFull =
      robot::dpcm16_decompress(std::span<const std::byte>(blockBytes),
                               blockPredictor);
  REQUIRE(blockFull.size() >= kRunwaySamples);
  std::vector<int16_t> expectedBlock;
  if (blockFull.size() > kRunwaySamples) {
    expectedBlock.assign(blockFull.begin() +
                             static_cast<std::ptrdiff_t>(kRunwaySamples),
                         blockFull.end());
  }


  const uint16_t primerReserved =
      static_cast<uint16_t>(kPrimerHeaderSize + primerData.size());

  auto data = build_header(primerReserved);
  auto primerHeader =
      build_primer_header(kPrimerHeaderSize + primerData.size(),
                          static_cast<uint32_t>(primerData.size()), 0);
  data.insert(data.end(), primerHeader.begin(), primerHeader.end());
  data.insert(data.end(), primerData.begin(), primerData.end());

  push16(data, 2);  // frame size
  push16(data, 26); // packet size (frame + audio block)

  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  data.push_back(0); // numCels low byte
  data.push_back(0); // numCels high byte

  push32(data, 4);   // pos (even channel)
  push32(data, 16);  // block size without metadata
  data.insert(data.end(), blockData.begin(), blockData.end());

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  auto primerWav = outDir / "frame_00000_even.wav";
  auto blockWav = outDir / "frame_00001_even.wav";
  REQUIRE(fs::exists(primerWav));
  REQUIRE(fs::exists(blockWav));

  auto actualPrimer = read_wav_samples(primerWav);
  auto actualBlock = read_wav_samples(blockWav);

  REQUIRE(actualPrimer == expectedPrimer);
  REQUIRE(actualBlock == expectedBlock);

  std::vector<int16_t> concatenatedActual;
  concatenatedActual.reserve(actualPrimer.size() + actualBlock.size());
  concatenatedActual.insert(concatenatedActual.end(), actualPrimer.begin(),
                            actualPrimer.end());
  concatenatedActual.insert(concatenatedActual.end(), actualBlock.begin(),
                            actualBlock.end());

  std::vector<int16_t> concatenatedExpected;
  concatenatedExpected.reserve(expectedPrimer.size() + expectedBlock.size());
  concatenatedExpected.insert(concatenatedExpected.end(),
                              expectedPrimer.begin(), expectedPrimer.end());
  concatenatedExpected.insert(concatenatedExpected.end(), expectedBlock.begin(),
                              expectedBlock.end());

  REQUIRE(concatenatedActual == concatenatedExpected);
}
