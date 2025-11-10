#include "robot_extractor.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <cmath>
#include <vector>

#include "utilities.hpp"

namespace {
constexpr size_t kHunkPaletteHeaderSize = 13;
constexpr size_t kNumPaletteEntriesOffset = 10;
constexpr size_t kEntryHeaderSize = 22;
constexpr size_t kEntryStartColorOffset = 10;
constexpr size_t kEntryNumColorsOffset = 14;
constexpr size_t kEntryUsedOffset = 16;
constexpr size_t kEntrySharedUsedOffset = 17;
constexpr size_t kEntryVersionOffset = 18;
constexpr size_t kRawPaletteSize = 1200;

uint8_t read_u8(std::span<const std::byte> data, size_t offset) {
  if (offset >= data.size()) {
    throw std::runtime_error("Palette SCI HunkPalette tronquée");
  }
  return std::to_integer<uint8_t>(data[offset]);
}

uint16_t read_u16(std::span<const std::byte> data, size_t offset) {
  if (offset + 1 >= data.size()) {
    throw std::runtime_error("Palette SCI HunkPalette tronquée");
  }
  const uint16_t lo = std::to_integer<uint8_t>(data[offset]);
  const uint16_t hi = std::to_integer<uint8_t>(data[offset + 1]);
  return static_cast<uint16_t>((hi << 8) | lo);
}

uint16_t read_u16_be(std::span<const std::byte> data, size_t offset) {
  if (offset + 1 >= data.size()) {
    throw std::runtime_error("Palette SCI HunkPalette tronquée");
  }
  const uint16_t hi = std::to_integer<uint8_t>(data[offset]);
  const uint16_t lo = std::to_integer<uint8_t>(data[offset + 1]);
  return static_cast<uint16_t>((hi << 8) | lo);
}

uint32_t read_u32(std::span<const std::byte> data, size_t offset) {
  if (offset + 3 >= data.size()) {
    throw std::runtime_error("Palette SCI HunkPalette tronquée");
  }
  const uint32_t b0 = std::to_integer<uint8_t>(data[offset]);
  const uint32_t b1 = std::to_integer<uint8_t>(data[offset + 1]);
  const uint32_t b2 = std::to_integer<uint8_t>(data[offset + 2]);
  const uint32_t b3 = std::to_integer<uint8_t>(data[offset + 3]);
  return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
}

uint32_t read_u32_be(std::span<const std::byte> data, size_t offset) {
  if (offset + 3 >= data.size()) {
    throw std::runtime_error("Palette SCI HunkPalette tronquée");
  }
  const uint32_t b0 = std::to_integer<uint8_t>(data[offset]);
  const uint32_t b1 = std::to_integer<uint8_t>(data[offset + 1]);
  const uint32_t b2 = std::to_integer<uint8_t>(data[offset + 2]);
  const uint32_t b3 = std::to_integer<uint8_t>(data[offset + 3]);
  return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

void write_span_le16(std::vector<std::byte> &data, size_t offset, uint16_t value) {
  if (offset + 1 >= data.size()) {
    throw std::runtime_error("Palette SCI HunkPalette tronquée");
  }
  data[offset] = std::byte{static_cast<uint8_t>(value & 0xFF)};
  data[offset + 1] = std::byte{static_cast<uint8_t>(value >> 8)};
}

void write_span_le32(std::vector<std::byte> &data, size_t offset, uint32_t value) {
  if (offset + 3 >= data.size()) {
    throw std::runtime_error("Palette SCI HunkPalette tronquée");
  }
  data[offset] = std::byte{static_cast<uint8_t>(value & 0xFF)};
  data[offset + 1] = std::byte{static_cast<uint8_t>((value >> 8) & 0xFF)};
  data[offset + 2] = std::byte{static_cast<uint8_t>((value >> 16) & 0xFF)};
  data[offset + 3] = std::byte{static_cast<uint8_t>((value >> 24) & 0xFF)};
}
} // namespace

namespace robot {
namespace {
void trim_runway_samples(std::vector<int16_t> &samples) {
  if (samples.size() <= kRobotRunwaySamples) {
    samples.clear();
    return;
  }
  const auto runwayEnd =
      samples.begin() + static_cast<std::ptrdiff_t>(kRobotRunwaySamples);
  samples.erase(samples.begin(), runwayEnd);
}

} // namespace

RobotExtractor::RobotExtractor(const std::filesystem::path &srcPath,
                               const std::filesystem::path &dstDir,
                               bool extractAudio, ExtractorOptions options)
    : m_srcPath(srcPath), m_dstDir(dstDir), m_extractAudio(extractAudio),
      m_options(options) {
  m_fp.open(srcPath, std::ios::binary);
  if (!m_fp.is_open()) {
    throw std::runtime_error(std::string("Impossible d'ouvrir ") +
                             srcPath.string());
  }
  std::error_code ec;
  m_fileSize = std::filesystem::file_size(srcPath, ec);
  if (ec) {
    throw std::runtime_error(std::string("Impossible d'obtenir la taille de ") +
                             srcPath.string() + ": " + ec.message());
  }
}

void RobotExtractor::readHeader() {
  StreamExceptionGuard guard(m_fp);
  if (m_options.force_be && m_options.force_le) {
    throw std::runtime_error(
        "Options force_be et force_le mutuellement exclusives");
  }
  if (m_options.force_be) {
    m_bigEndian = true;
  } else if (m_options.force_le) {
    m_bigEndian = false;
  } else {
    m_bigEndian = detect_endianness(m_fp);
  }

  parseHeaderFields(m_bigEndian);

  m_postHeaderPos = m_fp.tellg();
  
  if (m_version < 4 || m_version > 6) {
    throw std::runtime_error("Version Robot non supportée: " +
                             std::to_string(m_version));
  }

  if (m_xRes < 0 || m_yRes < 0 || m_xRes > m_options.max_x_res ||
      m_yRes > m_options.max_y_res) {
    throw std::runtime_error("Résolution invalide: " + std::to_string(m_xRes) +
                             "x" + std::to_string(m_yRes));
  }
}

void RobotExtractor::parseHeaderFields(bool bigEndian) {
  m_bigEndian = bigEndian;
  m_fileOffset = 0;
  
  // Lire signature (déjà lue pour l'endianness)
  m_fp.seekg(0);
  uint16_t signature = read_scalar<uint16_t>(m_fp, false); // toujours LE
  if (signature != 0x16) {
    throw std::runtime_error("Signature Robot invalide: 0x" + 
                             std::to_string(signature));
  }
  
  // Sauter 'SOL\0' (4 octets)
  m_fp.seekg(4, std::ios::cur);
  
  // Lire version avec l'endianness détectée
  m_version = read_scalar<uint16_t>(m_fp, m_bigEndian);
  if (m_version < 4 || m_version > 6) {
    throw std::runtime_error("Version Robot non supportée: " +
                             std::to_string(m_version));
  }

  // Continuer la lecture selon ScummVM/robot.cpp:532+
  m_audioBlkSize = read_scalar<uint16_t>(m_fp, m_bigEndian);
  if (m_audioBlkSize > kMaxAudioBlockSize) {
    throw std::runtime_error("Taille de bloc audio invalide dans l'en-tête: " +
                             std::to_string(m_audioBlkSize) +
                             " (maximum " +
                             std::to_string(kMaxAudioBlockSize) + ")");
  }
  m_primerZeroCompressFlag = read_scalar<int16_t>(m_fp, m_bigEndian);
  if (m_primerZeroCompressFlag != 0 && m_primerZeroCompressFlag != 1) {
    log_warn(m_srcPath,
             "Valeur primerZeroCompress inattendue: " +
                 std::to_string(m_primerZeroCompressFlag) +
                 " (attendu 0 ou 1, mais accepté pour compatibilité)",
             m_options);
  }
  m_fp.seekg(2, std::ios::cur);
  m_numFrames = read_scalar<uint16_t>(m_fp, m_bigEndian);
  if (m_numFrames == 0) {
    log_warn(m_srcPath,
             "Nombre de frames nul indiqué dans l'en-tête", m_options);
  } else if (m_numFrames > kMaxFrames) {
    log_warn(m_srcPath,
             "Nombre de frames élevé dans l'en-tête: " +
                 std::to_string(m_numFrames) + " (limite conseillée " +
                 std::to_string(kMaxFrames) + ")",
             m_options);
  }
  m_paletteSize = read_scalar<uint16_t>(m_fp, m_bigEndian);
  m_primerReservedSize = read_scalar<uint16_t>(m_fp, m_bigEndian);
  m_xRes = read_scalar<int16_t>(m_fp, m_bigEndian);
  m_yRes = read_scalar<int16_t>(m_fp, m_bigEndian);
  
  // ORDRE CRITIC
