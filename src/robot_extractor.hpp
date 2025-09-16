#pragma once

#include "utilities.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace robot {

inline bool rangesOverlap(const std::byte *aBegin, const std::byte *aEnd,
                          const std::byte *bBegin, const std::byte *bEnd) {
  return !(aEnd <= bBegin || bEnd <= aBegin);
}

namespace detail {
inline int validate_cel_dimensions(uint16_t w, uint16_t h, uint8_t scale) {
  if (w == 0 || h == 0) {
    throw std::runtime_error("Dimensions de cel invalides");
  }
  if (scale < 1 || scale > 200) {
    throw std::runtime_error("Facteur d'échelle vertical invalide");
  }
  const int sourceHeight = static_cast<int>(h) * scale / 100;
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

// Agrandit une image lorsque la source est plus petite que la cible.
inline void expand_up(std::byte *destBase, const std::byte *srcBase,
                      size_t rowBytes, uint16_t h, int sourceHeight) {
  int destY = static_cast<int>(h);
  int remainder = 0;
  std::byte *destPtr = destBase + static_cast<size_t>(h) * rowBytes;
  for (int srcY = sourceHeight - 1; srcY >= 0; --srcY) {
    // remainder accumule le numérateur de h/sourceHeight afin de savoir
    // quand répéter une ligne source supplémentaire.
    remainder += h;
    int repeat = remainder / sourceHeight;
    remainder %= sourceHeight;
    const std::byte *srcPtr = srcBase + static_cast<size_t>(srcY) * rowBytes;
    // Répéter la ligne source autant de fois que nécessaire dans la cible.
    for (int i = 0; i < repeat; ++i) {
      destPtr -= rowBytes;
      --destY;
      if (destY < 0) {
        throw std::runtime_error("Expansion de cel hors limites");
      }
      std::memcpy(destPtr, srcPtr, rowBytes);
    }
  }
  if (destY != 0) {
    throw std::runtime_error("Expansion de cel incohérente");
  }
}

// Réduit une image lorsque la source est plus grande que la cible.
inline void expand_down(std::byte *destBase, const std::byte *srcBase,
                        size_t rowBytes, uint16_t h, int sourceHeight) {
  int srcY = sourceHeight;
  int remainder = 0;
  for (int destY = static_cast<int>(h) - 1; destY >= 0; --destY) {
    // remainder accumule le numérateur de sourceHeight/h afin de déterminer
    // combien de lignes source sauter.
    remainder += sourceHeight;
    int step = remainder / h;
    remainder %= h;
    srcY -= step;
    if (srcY < 0) {
      throw std::runtime_error("Réduction de cel hors limites");
    }
    const std::byte *srcPtr = srcBase + static_cast<size_t>(srcY) * rowBytes;
    std::byte *destPtr = destBase + static_cast<size_t>(destY) * rowBytes;
    std::memcpy(destPtr, srcPtr, rowBytes);
  }
  if (srcY != 0) {
    throw std::runtime_error("Réduction de cel incohérente");
  }
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

  if (sourceHeight <= static_cast<int>(h)) {
    detail::expand_up(destBase, srcBase, rowBytes, h, sourceHeight);
  } else {
    detail::expand_down(destBase, srcBase, rowBytes, h, sourceHeight);
  }
}

class RobotExtractor {
public:
  RobotExtractor(const std::filesystem::path &srcPath,
                 const std::filesystem::path &dstDir, bool extractAudio,
                 ExtractorOptions options = {});
  void extract();

private:
#ifdef ROBOT_EXTRACTOR_TESTING
  friend struct RobotExtractorTester;
#endif
  static constexpr uint16_t kRobotSig = 0x16;
  static constexpr uint16_t kMaxFrames = 10000;
  static constexpr size_t kMaxCelPixels = 1024 * 1024;
  static constexpr size_t kMaxFrameSize = 10 * 1024 * 1024;
  static constexpr uint16_t kMaxAudioBlockSize = 65535;
  static constexpr size_t kMaxCuePoints = 256;
  static constexpr size_t kCelHeaderSize = 22;
  static constexpr uint32_t kSampleRate = 11025;
  static constexpr size_t kAudioRunwayBytes = 8;

  void readHeader();
  void parseHeaderFields(bool bigEndian);
  void readPrimer();
  void readPalette();
  void processPrimerChannel(std::vector<std::byte> &primer, int16_t &predictor,
                            bool isEven);
  void process_audio_block(std::span<const std::byte> block, bool isEven);
  void readSizesAndCues();
  bool exportFrame(int frameNo, nlohmann::json &frameJson);
  void writeWav(const std::vector<int16_t> &samples, uint32_t sampleRate,
                size_t blockIndex, bool isEvenChannel);

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
  std::vector<uint32_t> m_frameSizes;
  std::vector<uint32_t> m_packetSizes;
  std::array<int32_t, kMaxCuePoints> m_cueTimes;
  std::array<int16_t, kMaxCuePoints> m_cueValues;
  std::vector<std::byte> m_palette;
  std::streamoff m_postHeaderPos = 0;
  std::streamoff m_postPrimerPos = 0;
  std::streamsize m_evenPrimerSize = 0;
  std::streamsize m_oddPrimerSize = 0;
  int32_t m_totalPrimerSize = 0;
  std::streamoff m_primerPosition = 0;
  std::vector<std::byte> m_evenPrimer;
  std::vector<std::byte> m_oddPrimer;
  std::vector<std::byte> m_frameBuffer;
  std::vector<std::byte> m_celBuffer;
  std::vector<std::byte> m_rgbaBuffer;
  int16_t m_audioPredictorEven = 0;
  int16_t m_audioPredictorOdd = 0;
  size_t m_evenAudioIndex = 0;
  size_t m_oddAudioIndex = 0;
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
  static std::vector<std::byte> &evenPrimer(RobotExtractor &r) {
    return r.m_evenPrimer;
  }
  static std::vector<std::byte> &oddPrimer(RobotExtractor &r) {
    return r.m_oddPrimer;
  }
  static bool &hasPalette(RobotExtractor &r) { return r.m_hasPalette; }
  static bool &bigEndian(RobotExtractor &r) { return r.m_bigEndian; }
  static int16_t &maxCelsPerFrame(RobotExtractor &r) {
    return r.m_maxCelsPerFrame;
  }
  static std::vector<std::byte> &palette(RobotExtractor &r) {
    return r.m_palette;
  }
  static int16_t &xRes(RobotExtractor &r) { return r.m_xRes; }
  static int16_t &yRes(RobotExtractor &r) { return r.m_yRes; }
  static std::vector<std::byte> &rgbaBuffer(RobotExtractor &r) {
    return r.m_rgbaBuffer;
  }
  static constexpr size_t maxCelPixels() { return RobotExtractor::kMaxCelPixels; }
  static constexpr uint16_t maxAudioBlockSize() {
    return RobotExtractor::kMaxAudioBlockSize;
  }
  static constexpr size_t maxFrameSize() {
    return RobotExtractor::kMaxFrameSize;
  }
  static constexpr uint16_t maxFrames() { return RobotExtractor::kMaxFrames; }
  static void readHeader(RobotExtractor &r) { r.readHeader(); }
  static void readPrimer(RobotExtractor &r) { r.readPrimer(); }
  static void readPalette(RobotExtractor &r) { r.readPalette(); }
  static bool exportFrame(RobotExtractor &r, int frameNo,
                          nlohmann::json &frameJson) {
    return r.exportFrame(frameNo, frameJson);
  }
  static void writeWav(RobotExtractor &r, const std::vector<int16_t> &samples,
                       uint32_t sampleRate, size_t blockIndex,
                       bool isEvenChannel) {
    r.writeWav(samples, sampleRate, blockIndex, isEvenChannel);
  }
};
#endif

} // namespace robot
