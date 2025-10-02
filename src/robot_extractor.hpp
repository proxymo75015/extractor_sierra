#pragma once

#include "utilities.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
namespace robot {

inline constexpr size_t kRobotZeroCompressSize = 2048;
inline constexpr size_t kRobotRunwayBytes = 8;
constexpr size_t kRobotRunwaySamples = kRobotRunwayBytes;
inline constexpr size_t kRobotAudioHeaderSize = 8;

inline bool rangesOverlap(const std::byte *aBegin, const std::byte *aEnd,
                          const std::byte *bBegin, const std::byte *bEnd) {
  return !(aEnd <= bBegin || bEnd <= aBegin);
}

namespace detail {
inline int validate_cel_dimensions(uint16_t w, uint16_t h, uint8_t scale) {
  if (w == 0 || h == 0) {
    throw std::runtime_error("Dimensions de cel invalides");
  }
  if (scale == 0) {
    throw std::runtime_error(
        "Facteur d'échelle vertical invalide (valeur attendue entre 1 et 100)");
  }
  const int sourceHeight = static_cast<int>(h) * static_cast<int>(scale) / 100;
  if (sourceHeight <= 0) {
    throw std::runtime_error("Facteur d'échelle vertical invalide");
  }
  return sourceHeight;
}

// Valide les paramètres d'expansion et retourne la hauteur source calculée.
inline int validate_expand_params(std::span<std::byte> target,
                                  std::span<const std::byte> source, uint16_t w,
                                  uint16_t h, uint8_t scale) {
  const int sourceHeight = validate_cel_dimensions(w, h, scale);
  const size_t wSize = static_cast<size_t>(w);
  if (static_cast<size_t>(sourceHeight) > SIZE_MAX / wSize) {
    throw std::runtime_error(
        "Multiplication de la taille source dépasse SIZE_MAX");
  }
  if (static_cast<size_t>(h) > SIZE_MAX / wSize) {
    throw std::runtime_error(
        "Multiplication de la taille cible dépasse SIZE_MAX");
  }
  if (source.size() != wSize * static_cast<size_t>(sourceHeight)) {
    throw std::runtime_error(
        "Taille source incorrecte pour l'expansion verticale");
  }
  if (target.size() != wSize * static_cast<size_t>(h)) {
    throw std::runtime_error(
        "Taille cible incorrecte pour l'expansion verticale");
  }

  return sourceHeight;
}

} // namespace detail

// Ajuste la hauteur d'une cel en l'agrandissant ou la réduisant.
// Précondition : les plages de target et source ne doivent pas se chevaucher.
inline void expand_cel(std::span<std::byte> target,
                       std::span<const std::byte> source, uint16_t w,
                       uint16_t h, uint8_t scale) {
  const std::byte *tBegin = target.data();
  const std::byte *tEnd = tBegin + target.size();
  const std::byte *sBegin = source.data();
  const std::byte *sEnd = sBegin + source.size();
  if (rangesOverlap(tBegin, tEnd, sBegin, sEnd))
    throw std::runtime_error("target and source must not overlap");

  const int sourceHeight =
      detail::validate_expand_params(target, source, w, h, scale);

  const std::byte *srcBase = source.data();
  std::byte *destBase = target.data();
  const size_t rowBytes = static_cast<size_t>(w);
  std::byte *const destEnd = destBase + target.size();
  int remainder = 0;

  for (int remaining = sourceHeight; remaining > 0; --remaining) {
    remainder += static_cast<int>(h);
    int linesToDraw = remainder / sourceHeight;
    remainder %= sourceHeight;

    for (int i = 0; i < linesToDraw; ++i) {
      if (destBase >= destEnd) {
        throw std::runtime_error("Expansion de cel hors limites");
      }
      std::memcpy(destBase, srcBase, rowBytes);
      destBase += rowBytes;
    }

    srcBase += rowBytes;
  }

  if (destBase != destEnd) {
    throw std::runtime_error("Expansion de cel incohérente");
  }
}

class RobotExtractor {
public:
  RobotExtractor(const std::filesystem::path &srcPath,
                 const std::filesystem::path &dstDir, bool extractAudio,
                 ExtractorOptions options = {});
  void extract();

  struct PaletteEntry {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    bool used = false;
    bool present = false;
  };

  struct ParsedPalette {
    bool valid = false;
    uint8_t startColor = 0;
    uint16_t colorCount = 0;
    bool sharedUsed = false;
    bool defaultUsed = false;
    uint32_t version = 0;
    std::array<PaletteEntry, 256> entries{};
    std::vector<std::byte> remapData;
  };

private:
#ifdef ROBOT_EXTRACTOR_TESTING
  friend struct RobotExtractorTester;
#endif
  static constexpr uint16_t kRobotSig = 0x16;
  static constexpr uint16_t kMaxFrames = 10000;
  static constexpr uint16_t kMaxAudioBlockSize = 65535;
  static constexpr size_t kMaxCuePoints = 256;
  static constexpr size_t kCelHeaderSize = 22;
  static constexpr uint32_t kChannelSampleRate = 11025;
  static constexpr uint32_t kSampleRate = 22050;
  static constexpr const char *kPaletteFallbackFilename = "palette.raw";

  void readHeader();
  void parseHeaderFields(bool bigEndian);
  void readPrimer();
  void readPalette();
  void ensurePrimerProcessed();
  void processPrimerChannel(std::vector<std::byte> &primer, bool isEven);
  void process_audio_block(std::span<const std::byte> block, int32_t pos);
  void readSizesAndCues();
  bool exportFrame(int frameNo, nlohmann::json &frameJson);
  void writeWav(const std::vector<int16_t> &samples, uint32_t sampleRate,
                size_t blockIndex, bool isEvenChannel);
  void appendChannelSamples(bool isEven, int64_t halfPos,
                            const std::vector<int16_t> &samples);
  size_t celPixelLimit() const;
  size_t rgbaBufferLimit() const;
  struct ChannelAudio;
  struct AppendPlan {
    size_t skipSamples = 0;
    size_t startSample = 0;
    size_t availableSamples = 0;
    size_t leadingOverlap = 0;
    size_t trimmedStart = 0;
    size_t requiredSize = 0;
    bool negativeAdjusted = false;
    bool negativeIgnored = false;
    bool posIsEven = true;
  };
  enum class AppendPlanStatus { Skip, Ok, Conflict, ParityMismatch };
  AppendPlanStatus planChannelAppend(const ChannelAudio &channel, bool isEven,
                                     int64_t halfPos,
                                     const std::vector<int16_t> &samples,
                                     AppendPlan &plan) const;
  void finalizeChannelAppend(ChannelAudio &channel, bool isEven,
                             int64_t halfPos,
                             const std::vector<int16_t> &samples,
                             const AppendPlan &plan,
                             AppendPlanStatus status);
  void finalizeAudio();
  std::vector<int16_t> buildChannelStream(bool isEven) const;

  static ParsedPalette parseHunkPalette(std::span<const std::byte> raw);

  std::filesystem::path m_srcPath;
  std::filesystem::path m_dstDir;
  std::ifstream m_fp;
  std::uintmax_t m_fileSize;
  bool m_bigEndian = false;
  bool m_extractAudio;
  ExtractorOptions m_options;
  uint16_t m_version;
  uint16_t m_audioBlkSize;
  int16_t m_primerZeroCompressFlag;
  uint16_t m_numFrames;
  uint16_t m_paletteSize;
  uint16_t m_primerReservedSize;
  int16_t m_xRes;
  int16_t m_yRes;
  bool m_hasPalette;
  bool m_hasAudio;
  int16_t m_frameRate;
  bool m_isHiRes;
  int16_t m_maxSkippablePackets;
  int16_t m_maxCelsPerFrame;
  // Header fields present in Robot versions 5 and 6.
  std::array<uint32_t, 4> m_fixedCelSizes{};
  std::array<uint32_t, 2> m_reservedHeaderSpace{};
  std::vector<uint32_t> m_frameSizes;
  std::vector<uint32_t> m_packetSizes;
  std::array<int32_t, kMaxCuePoints> m_cueTimes;
  std::array<uint16_t, kMaxCuePoints> m_cueValues;
  std::vector<std::byte> m_palette;
  std::streamoff m_postHeaderPos = 0;
  std::streamoff m_postPrimerPos = 0;
  std::streamsize m_evenPrimerSize = 0;
  std::streamsize m_oddPrimerSize = 0;
  int32_t m_totalPrimerSize = 0;
  std::streamoff m_primerPosition = 0;
  std::vector<std::byte> m_evenPrimer;
  std::vector<std::byte> m_oddPrimer;
  bool m_primerInvalid = false;
  bool m_primerProcessed = false;
  std::vector<std::byte> m_frameBuffer;
  std::vector<std::byte> m_celBuffer;
  std::vector<std::byte> m_rgbaBuffer;
  bool m_paletteParseFailed = false;
  bool m_paletteFallbackDumped = false;
  struct ChannelAudio {
    std::vector<int16_t> samples;
    std::vector<uint8_t> occupied;
  };
  ChannelAudio m_evenChannelAudio;
  ChannelAudio m_oddChannelAudio;
  int64_t m_audioStartOffset = 0;
  bool m_audioStartOffsetInitialized = false;
#ifdef ROBOT_EXTRACTOR_TESTING
  bool m_forceParityMismatchForNextAttempt = false;
#endif
};

#ifdef ROBOT_EXTRACTOR_TESTING
struct RobotExtractorTester {
  static std::vector<uint32_t> &frameSizes(RobotExtractor &r) {
    return r.m_frameSizes;
  }
  static std::vector<uint32_t> &packetSizes(RobotExtractor &r) {
    return r.m_packetSizes;
  }
  static std::ifstream &file(RobotExtractor &r) { return r.m_fp; }
  static std::streamoff &primerPosition(RobotExtractor &r) {
    return r.m_primerPosition;
  }
  static std::streamoff &postHeaderPos(RobotExtractor &r) {
    return r.m_postHeaderPos;
  }
  static std::streamoff &postPrimerPos(RobotExtractor &r) {
    return r.m_postPrimerPos;
  }
  static std::vector<std::byte> &evenPrimer(RobotExtractor &r) {
    return r.m_evenPrimer;
  }
  static std::vector<std::byte> &oddPrimer(RobotExtractor &r) {
    return r.m_oddPrimer;
  }
  static std::streamsize &evenPrimerSize(RobotExtractor &r) {
    return r.m_evenPrimerSize;
  }
  static std::streamsize &oddPrimerSize(RobotExtractor &r) {
    return r.m_oddPrimerSize;
  }
  static bool &hasPalette(RobotExtractor &r) { return r.m_hasPalette; }
  static bool &bigEndian(RobotExtractor &r) { return r.m_bigEndian; }
  static int16_t &maxCelsPerFrame(RobotExtractor &r) {
    return r.m_maxCelsPerFrame;
  }
  static uint16_t &numFrames(RobotExtractor &r) { return r.m_numFrames; }
  static std::vector<std::byte> &palette(RobotExtractor &r) {
    return r.m_palette;
  }
  static int16_t &xRes(RobotExtractor &r) { return r.m_xRes; }
  static int16_t &yRes(RobotExtractor &r) { return r.m_yRes; }
  static std::vector<std::byte> &rgbaBuffer(RobotExtractor &r) {
    return r.m_rgbaBuffer;
  }
  static std::array<uint32_t, 4> &fixedCelSizes(RobotExtractor &r) {
    return r.m_fixedCelSizes;
  }
  static std::vector<std::byte> &celBuffer(RobotExtractor &r) {
    return r.m_celBuffer;
  }
  static size_t celPixelLimit(const RobotExtractor &r) {
    return r.celPixelLimit();
  }
  static size_t rgbaBufferLimit(const RobotExtractor &r) {
    return r.rgbaBufferLimit();
  }
  static constexpr uint16_t maxAudioBlockSize() {
    return RobotExtractor::kMaxAudioBlockSize;
  }
  static constexpr uint16_t maxFrames() { return RobotExtractor::kMaxFrames; }
  static void readHeader(RobotExtractor &r) { r.readHeader(); }
  static void readPrimer(RobotExtractor &r) { r.readPrimer(); }
  static void readPalette(RobotExtractor &r) { r.readPalette(); }
  static void readSizesAndCues(RobotExtractor &r) { r.readSizesAndCues(); }
  static bool exportFrame(RobotExtractor &r, int frameNo,
                          nlohmann::json &frameJson) {
    return r.exportFrame(frameNo, frameJson);
  }
  static void finalizeAudio(RobotExtractor &r) { r.finalizeAudio(); }
  static std::vector<int16_t> buildChannelStream(RobotExtractor &r,
                                                 bool isEven) {
    return r.buildChannelStream(isEven);
  }
  static int64_t &audioStartOffset(RobotExtractor &r) {
    return r.m_audioStartOffset;
  }
  static bool &audioStartOffsetInitialized(RobotExtractor &r) {
    return r.m_audioStartOffsetInitialized;
  }
  static bool &forceParityMismatchForNextAttempt(RobotExtractor &r) {
    return r.m_forceParityMismatchForNextAttempt;
  }
  static RobotExtractor::ParsedPalette parsePalette(const RobotExtractor &r) {
    return RobotExtractor::parseHunkPalette(r.m_palette);
  }
  static void processAudioBlock(RobotExtractor &r,
                                std::span<const std::byte> block, int32_t pos) {
    r.process_audio_block(block, pos);
  }
  static void writeWav(RobotExtractor &r, const std::vector<int16_t> &samples,
                       uint32_t sampleRate, size_t blockIndex,
                       bool isEvenChannel) {
    r.writeWav(samples, sampleRate, blockIndex, isEvenChannel);
  }
};
#endif

} // namespace robot
