#pragma once

#include "utilities.hpp"
#include <array>
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

#ifdef ROBOT_EXTRACTOR_TESTING
#define private public
#endif

inline void expand_cel(std::span<std::byte> target,
                       std::span<const std::byte> source, uint16_t w,
                       uint16_t h, uint8_t scale) {
  if (scale < 1) {
    throw std::runtime_error("Scale invalide");
  }    
  if (scale > 200) {
    throw std::runtime_error("Scale trop grand");
  }
  const int sourceHeight = static_cast<int>(h) * scale / 100;
  if (sourceHeight <= 0) {
    throw std::runtime_error("Hauteur source invalide");
  }
  if (source.size() != static_cast<size_t>(w) * sourceHeight) {
    throw std::runtime_error(
        "Taille source incorrecte pour l'expansion verticale");
  }
  if (target.size() != static_cast<size_t>(w) * h) {
    throw std::runtime_error(
        "Taille cible incorrecte pour l'expansion verticale");
  }

  const std::byte *srcBase = source.data();
  std::byte *destBase = target.data();
  const size_t rowBytes = static_cast<size_t>(w);

  if (sourceHeight <= static_cast<int>(h)) {
    int destY = static_cast<int>(h);
    int remainder = 0;
    std::byte *destPtr = destBase + static_cast<size_t>(h) * rowBytes;
    for (int srcY = sourceHeight - 1; srcY >= 0; --srcY) {
      remainder += h;
      int repeat = remainder / sourceHeight;
      remainder %= sourceHeight;
      const std::byte *srcPtr = srcBase + static_cast<size_t>(srcY) * rowBytes;      
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
  } else {
    int srcY = sourceHeight;
    int remainder = 0;
    std::byte *destPtr = destBase +
                         (static_cast<size_t>(h) - 1) * rowBytes;    
    for (int destY = static_cast<int>(h) - 1; destY >= 0; --destY) {
      remainder += sourceHeight;
      int step = remainder / h;
      remainder %= h;
      srcY -= step;
      if (srcY < 0) {
        throw std::runtime_error("Réduction de cel hors limites");
      }
      const std::byte *srcPtr =
          srcBase + static_cast<size_t>(srcY) * rowBytes;
      std::memcpy(destPtr, srcPtr, rowBytes);
      destPtr -= rowBytes;
    }
    if (srcY != 0) {
      throw std::runtime_error("Réduction de cel incohérente");
    }
  }
}

class RobotExtractor {
public:
  RobotExtractor(const std::filesystem::path &srcPath,
                 const std::filesystem::path &dstDir, bool extractAudio,
                 ExtractorOptions options = {});
  void extract();

private:
  static constexpr uint16_t kRobotSig = 0x16;
  static constexpr uint16_t kMaxFrames = 10000;
  static constexpr size_t kMaxCelPixels = 1024 * 1024;
  static constexpr size_t kMaxFrameSize = 10 * 1024 * 1024;
  static constexpr uint16_t kMaxAudioBlockSize = 65535;
  static constexpr size_t kMaxCuePoints = 256;

  void readHeader();
  void parseHeaderFields();
  void readPrimer();
  void readPalette();
  void readSizesAndCues();
  bool exportFrame(int frameNo, nlohmann::json &frameJson);
  void writeWav(const std::vector<int16_t> &samples, uint32_t sampleRate,
                int blockIndex, bool isEvenChannel);

  std::filesystem::path m_srcPath;
  std::filesystem::path m_dstDir;
  std::ifstream m_fp;
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
  std::array<int32_t, 4> m_maxCelArea;
  std::vector<uint32_t> m_frameSizes;
  std::vector<uint32_t> m_packetSizes;
  std::array<int32_t, kMaxCuePoints> m_cueTimes;
  std::array<int16_t, kMaxCuePoints> m_cueValues;
  std::vector<std::byte> m_palette;
  std::streamsize m_evenPrimerSize;
  std::streamsize m_oddPrimerSize;
  int32_t m_totalPrimerSize;
  std::streamoff m_primerPosition;
  std::vector<std::byte> m_evenPrimer;
  std::vector<std::byte> m_oddPrimer;
  std::vector<std::byte> m_frameBuffer;
  std::vector<std::byte> m_celBuffer;
  std::vector<std::byte> m_rgbaBuffer;
  int16_t m_audioPredictorEven = 0;
  int16_t m_audioPredictorOdd = 0;
  int m_evenAudioIndex = 0;
  int m_oddAudioIndex = 0;
};

#ifdef ROBOT_EXTRACTOR_TESTING
#undef private
#endif

} // namespace robot
