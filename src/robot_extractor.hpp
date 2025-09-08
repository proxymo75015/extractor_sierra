#pragma once

#include "utilities.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <span>

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
  if (scale > 100) {
    throw std::runtime_error("Scale invalide");
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

  int destY = static_cast<int>(h);
  int remainder = 0;
  for (int srcY = sourceHeight - 1; srcY >= 0; --srcY) {
    remainder += h;
    int repeat = remainder / sourceHeight;
    remainder %= sourceHeight;
    for (int i = 0; i < repeat; ++i) {
      --destY;
      if (destY < 0) {
        throw std::runtime_error("Expansion de cel hors limites");
      }
      std::copy_n(source.begin() + static_cast<size_t>(srcY) * w, w,
                  target.begin() + static_cast<size_t>(destY) * w);
    }
  }
  if (destY != 0) {
    throw std::runtime_error("Expansion de cel incohérente");
  }
}

class RobotExtractor {
public:
  RobotExtractor(const std::filesystem::path &srcPath,
                 const std::filesystem::path &dstDir, bool extractAudio);
  void extract();

private:
  static constexpr uint16_t kRobotSig = 0x16;
  static constexpr uint16_t kMaxFrames = 10000;
  static constexpr size_t kMaxCelPixels = 1024 * 1024;
  static constexpr size_t kMaxFrameSize = 10 * 1024 * 1024;
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
