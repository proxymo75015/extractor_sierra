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
  const std::streampos headerStart = m_fp.tellg();
  if (headerStart == std::streampos(-1)) {
    throw std::runtime_error(
        "Impossible de déterminer la position de l'en-tête Robot");
  }

  m_fileOffset = static_cast<std::streamoff>(headerStart);
  
  constexpr std::streamoff versionOffset =
      static_cast<std::streamoff>(sizeof(uint16_t) + 4);
  const std::streampos versionPos = headerStart + versionOffset;

  m_fp.seekg(versionPos);
  if (!m_fp) {
    throw std::runtime_error(
        "Impossible d'accéder au champ version de l'en-tête Robot");
  }

  m_version = read_scalar<uint16_t>(m_fp, m_bigEndian);
  if (m_version < 4 || m_version > 6) {
    throw std::runtime_error("Version Robot non supportée: " +
                             std::to_string(m_version));
  }

  m_fp.clear();
  m_fp.seekg(headerStart);
  if (!m_fp) {
    throw std::runtime_error(
        "Impossible de repositionner le flux au début de l'en-tête Robot");
  }

  const uint16_t sig = read_scalar<uint16_t>(m_fp, false);
  if (sig != kRobotSig) {
    throw std::runtime_error("Signature Robot invalide");
  }
  std::array<char, 4> sol;
  m_fp.read(sol.data(), checked_streamsize(sol.size()));
  if (sol != std::array<char, 4>{'S', 'O', 'L', '\0'}) {
    throw std::runtime_error("Tag SOL invalide");
  }
  const uint16_t versionFromHeader = read_scalar<uint16_t>(m_fp, m_bigEndian);
  if (versionFromHeader != m_version) {
    throw std::runtime_error(
        "Version Robot incohérente entre les lectures successives");
  }
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
  
  // Correction 1: Gérer xRes/yRes == 0 avec valeurs par défaut
  if (m_xRes == 0 || m_yRes == 0) {
    log_warn(m_srcPath, 
             "Résolution nulle détectée, utilisation de valeurs par défaut (640x480)", 
             m_options);
    if (m_xRes == 0) m_xRes = 640;
    if (m_yRes == 0) m_yRes = 480;
  }
  
  m_hasPalette = read_scalar<uint8_t>(m_fp, m_bigEndian) != 0;
  m_hasAudio = read_scalar<uint8_t>(m_fp, m_bigEndian) != 0;
  if (m_hasAudio && m_audioBlkSize < kRobotAudioHeaderSize) {
    throw std::runtime_error("Taille de bloc audio trop petite dans l'en-tête: " +
                             std::to_string(m_audioBlkSize) +
                             " (minimum " +
                             std::to_string(kRobotAudioHeaderSize) + ")");
  }
  m_fp.seekg(2, std::ios::cur);
  m_frameRate = read_scalar<int16_t>(m_fp, m_bigEndian);
  
  // Correction 2: Assouplir la validation frameRate (accepter > 120)
  if (m_frameRate <= 0) {
    log_warn(m_srcPath,
             "Fréquence d'image invalide (" + std::to_string(m_frameRate) +
                 "), utilisation de 1 fps par défaut",
             m_options);
    m_frameRate = 1;
  } else if (m_frameRate > 120) {
    log_warn(m_srcPath,
             "Fréquence d'image élevée détectée: " + std::to_string(m_frameRate) +
                 " fps (inhabituel mais accepté)",
             m_options);
  }
  
  m_isHiRes = read_scalar<int16_t>(m_fp, m_bigEndian) != 0;
  m_maxSkippablePackets = read_scalar<int16_t>(m_fp, m_bigEndian);
  m_maxCelsPerFrame = read_scalar<int16_t>(m_fp, m_bigEndian);
  if (m_maxCelsPerFrame < 1) {
    log_warn(m_srcPath,
             "Nombre de cels par frame non positif: " +
                 std::to_string(m_maxCelsPerFrame) + ", utilisation de 1",
             m_options);
    m_maxCelsPerFrame = 1;
  } else if (m_maxCelsPerFrame > 10) {
    log_warn(m_srcPath,
             "Nombre de cels par frame élevé: " +
                 std::to_string(m_maxCelsPerFrame),
             m_options);
  }
  m_fixedCelSizes.fill(0);
  m_reservedHeaderSpace.fill(0);
  
  // Correction 3: Lire maxCelArea comme int32_t signé (version >= 6)
  if (m_version >= 6) {
    for (int i = 0; i < 4; ++i) {
      int32_t val = read_scalar<int32_t>(m_fp, m_bigEndian);
      if (val < 0) {
        log_warn(m_srcPath, 
                 "maxCelArea négatif détecté (" + std::to_string(val) + "), utilisation de 0", 
                 m_options);
        val = 0;
      }
      m_fixedCelSizes[i] = static_cast<uint32_t>(val);
    }
  }
  
  if (m_version >= 5) {
    for (auto &reserved : m_reservedHeaderSpace) {
      reserved = read_scalar<uint32_t>(m_fp, m_bigEndian);
    }
  }
}

void RobotExtractor::readPrimer() {
  m_primerInvalid = false;
  m_primerProcessed = false;  
  const std::uintmax_t fileSize = m_fileSize;
  if (!m_hasAudio) {
    std::streamoff curPos = m_fp.tellg();
    if (curPos < 0 ||
        static_cast<std::uintmax_t>(curPos) + m_primerReservedSize > fileSize) {
      throw std::runtime_error("Primer hors limites");
    }
    m_fp.seekg(m_primerReservedSize, std::ios::cur);
    m_postPrimerPos = m_fp.tellg();
    m_primerProcessed = true;
    if (m_options.debug_index) {
      log_error(m_srcPath,
                "readPrimer: position après seekg = " +
                    std::to_string(m_fp.tellg()),
                m_options);
    }    
    return;
  }
  StreamExceptionGuard guard(m_fp);
  if (m_primerReservedSize != 0) {
    // Memorize the start of the primer header before reading its fields    
    std::streamoff primerHeaderPos = m_fp.tellg();
    if (primerHeaderPos < 0 ||
        static_cast<std::uintmax_t>(primerHeaderPos) + m_primerReservedSize >
            fileSize) {
      throw std::runtime_error("Primer hors limites");
    }
    m_totalPrimerSize = read_scalar<int32_t>(m_fp, m_bigEndian);
    int16_t compType = read_scalar<int16_t>(m_fp, m_bigEndian);
    m_evenPrimerSize = read_scalar<int32_t>(m_fp, m_bigEndian);
    m_oddPrimerSize = read_scalar<int32_t>(m_fp, m_bigEndian);
    // Record the start of the primer data, just after the header
    m_primerPosition = m_fp.tellg();

    constexpr std::int64_t primerHeaderSize =
        static_cast<std::int64_t>(sizeof(int32_t) + sizeof(int16_t) +
                                  2 * sizeof(int32_t));

    if (m_totalPrimerSize < 0) {
      throw std::runtime_error("totalPrimerSize négatif dans le primer audio");
    }

    if (m_evenPrimerSize < 0 || m_oddPrimerSize < 0) {
      throw std::runtime_error("Tailles de primer audio incohérentes");
    }

    const std::int64_t expectedTotal =
        primerHeaderSize + static_cast<std::int64_t>(m_evenPrimerSize) +
        static_cast<std::int64_t>(m_oddPrimerSize);
    if (expectedTotal != static_cast<std::int64_t>(m_totalPrimerSize)) {
      log_warn(m_srcPath,
               "totalPrimerSize incohérent: attendu " +
                   std::to_string(static_cast<long long>(expectedTotal)) +
                   ", lu " +
                   std::to_string(static_cast<long long>(m_totalPrimerSize)),
               m_options);
    }
    
    if (compType != 0) {
      throw std::runtime_error("Type de compression inconnu: " +
                               std::to_string(compType));
    }

    const std::uint64_t primerHeaderSizeU =
        static_cast<std::uint64_t>(primerHeaderSize);
    const std::uint64_t reservedSize =
        static_cast<std::uint64_t>(m_primerReservedSize);
    const std::uint64_t reservedSpan = reservedSize;
    const std::uintmax_t primerHeaderPosMax =
        static_cast<std::uintmax_t>(primerHeaderPos);
    if (primerHeaderPosMax > fileSize ||
        reservedSpan > fileSize - primerHeaderPosMax) {
      throw std::runtime_error("Primer hors limites");
    }
    const std::streamoff afterPrimerHeaderPos = m_fp.tellg();

    if (m_totalPrimerSize == 0) {
      if (m_options.debug_index) {
        log_error(m_srcPath,
                  "readPrimer: totalPrimerSize nul, aucune donnée primer lue",
                  m_options);
      }
      m_evenPrimerSize = 0;
      m_oddPrimerSize = 0;
      m_evenPrimer.clear();
      m_oddPrimer.clear();
      
      const std::streamoff reservedEnd =
          primerHeaderPos + static_cast<std::streamoff>(m_primerReservedSize);
      if (reservedEnd > afterPrimerHeaderPos) {
        m_fp.seekg(reservedEnd, std::ios::beg);
        m_postPrimerPos = m_fp.tellg();
      } else {
        m_postPrimerPos = afterPrimerHeaderPos;
      }
    } else {
      const std::uint64_t primerSizesSum =
          static_cast<std::uint64_t>(m_evenPrimerSize) +
          static_cast<std::uint64_t>(m_oddPrimerSize);
      const std::uint64_t reservedDataSize =
          reservedSize >= primerHeaderSizeU ? (reservedSize - primerHeaderSizeU) : 0;

      const std::streamoff reservedEnd =
          primerHeaderPos + static_cast<std::streamoff>(m_primerReservedSize);

      if (primerSizesSum != reservedDataSize) {
        log_warn(m_srcPath,
                 "Somme des tailles primer incohérente avec l'espace réservé",
                 m_options);
        if (m_options.debug_index) {
          log_error(m_srcPath,
                    "readPrimer: primer relu malgré un mismatch", m_options);
        }
      }
      
      if (primerSizesSum > reservedDataSize) {
        log_warn(m_srcPath,
                 "Tailles de primer dépassent l'espace réservé", m_options);
      }
      if (primerSizesSum < reservedDataSize && m_options.debug_index) {
        log_error(m_srcPath,
                  "readPrimer: primer plus petit que primerReservedSize",
                  m_options);
      }

      const std::int64_t reservedDataAvailable =
          reservedEnd > afterPrimerHeaderPos
              ? (reservedEnd - afterPrimerHeaderPos)
              : 0;
      std::int64_t reservedDataRemaining = reservedDataAvailable;

      const auto assignPrimer = [&](std::vector<std::byte> &dest,
                                    std::int64_t requestedSize,
                                    const char *channelLabel) {
        if (requestedSize <= 0) {
          dest.clear();
          return;
        }

        const size_t targetSize = static_cast<size_t>(requestedSize);
        dest.assign(targetSize, std::byte{0});

        const std::int64_t toConsume =
            std::min<std::int64_t>(reservedDataRemaining, requestedSize);
        size_t copied = 0;
        if (toConsume > 0) {
          const std::streamsize chunkSize =
              checked_streamsize(static_cast<size_t>(toConsume));
          auto oldMask = m_fp.exceptions();
          m_fp.exceptions(std::ios::goodbit);
          m_fp.read(reinterpret_cast<char *>(dest.data()), chunkSize);
          const std::streamsize got =
              std::max<std::streamsize>(0, m_fp.gcount());
          m_fp.exceptions(oldMask);
          if (m_fp.fail() && !m_fp.bad()) {
            m_fp.clear(m_fp.rdstate() & ~(std::ios::failbit | std::ios::eofbit));
          }
          copied = static_cast<size_t>(got);
        }

        reservedDataRemaining -= toConsume;
        if (reservedDataRemaining < 0) {
          reservedDataRemaining = 0;
        }

        if (copied < targetSize) {
          log_warn(m_srcPath,
                   std::string("Primer audio ") + channelLabel +
                       " tronqué, complétion avec des zéros",
                   m_options);
        }
      };

      assignPrimer(m_evenPrimer, m_evenPrimerSize, "pair");
      assignPrimer(m_oddPrimer, m_oddPrimerSize, "impair");

      const std::int64_t reservedDataConsumed =
          reservedDataAvailable - reservedDataRemaining;
      const std::streamoff afterPrimerDataPos =
          afterPrimerHeaderPos + static_cast<std::streamoff>(reservedDataConsumed);
      if (reservedEnd > afterPrimerDataPos) {
        m_fp.seekg(reservedEnd, std::ios::beg);
        m_postPrimerPos = m_fp.tellg();
      } else {
        m_fp.seekg(afterPrimerDataPos, std::ios::beg);
        m_postPrimerPos = m_fp.tellg();
      }
    }
    if (m_options.debug_index) {
      log_error(m_srcPath,
                "readPrimer: position après seekg = " +
                    std::to_string(m_fp.tellg()),
                m_options);
    }
  } else if (m_primerZeroCompressFlag) {
    m_evenPrimerSize = 19922;
    m_oddPrimerSize = 21024;
    m_evenPrimer.assign(static_cast<size_t>(m_evenPrimerSize), std::byte{0});
    m_oddPrimer.assign(static_cast<size_t>(m_oddPrimerSize), std::byte{0});
    m_postPrimerPos = m_fp.tellg();
  } else {
    m_postPrimerPos = m_fp.tellg();
    m_primerInvalid = true;
  }

  if (m_hasAudio) {
    const auto evenPrimerSize64 = static_cast<int64_t>(m_evenPrimerSize);
    if (evenPrimerSize64 < 0) {
      throw std::runtime_error("Taille de primer audio pair négative");
    }
    if (evenPrimerSize64 > std::numeric_limits<int64_t>::max() / 2) {
      throw std::runtime_error(
          "Décalage audio pair dépasse la capacité de l'entier 64 bits");
    }
    m_audioStartOffset = evenPrimerSize64 * 2;
  }
  
  if (!m_extractAudio) {
    ensurePrimerProcessed();
  }
  
  if (m_options.debug_index) {
    log_error(m_srcPath,
              "readPrimer: position après seekg = " +
                  std::to_string(m_fp.tellg()),
              m_options);
  }
}

void RobotExtractor::ensurePrimerProcessed() {
  if (m_primerProcessed) {
    return;
  }

  auto releasePrimers = [this]() {
    m_evenPrimer.clear();
    m_evenPrimer.shrink_to_fit();
    m_oddPrimer.clear();
    m_oddPrimer.shrink_to_fit();
  };

  if (!m_extractAudio || !m_hasAudio) {
    m_primerProcessed = true;
    releasePrimers();
    return;
  }

  if (m_primerInvalid) {    
    throw std::runtime_error("ReadPrimerData - Flags corrupt");
  }

  // Décompresser les buffers primer pour initialiser les prédicteurs audio.
  if (m_evenPrimerSize > 0) {
    try {
      processPrimerChannel(m_evenPrimer, true);
    } catch (const std::runtime_error &) {
      throw std::runtime_error(std::string("Primer audio pair tronqué pour ") +
                               m_srcPath.string());
    }
  }
  if (m_oddPrimerSize > 0) {
    try {
      processPrimerChannel(m_oddPrimer, false);
    } catch (const std::runtime_error &) {
      throw std::runtime_error(
          std::string("Primer audio impair tronqué pour ") +
          m_srcPath.string());
    }
  }

  releasePrimers();
  m_primerProcessed = true;
}

void RobotExtractor::processPrimerChannel(std::vector<std::byte> &primer,
                                          bool isEven) {
  ChannelAudio &channel = isEven ? m_evenChannelAudio : m_oddChannelAudio;
  if (primer.empty()) {
    if (isEven) {
      m_evenPrimerSize = 0;
    } else {
      m_oddPrimerSize = 0;
    }
    channel.predictor = 0;
    channel.predictorInitialized = false;
    return;
  }
  if (primer.size() < kRobotRunwayBytes) {
    const char *channelLabel = isEven ? "pair" : "impair";
    log_warn(m_srcPath,
             std::string("Primer audio ") + channelLabel +
                 " trop court (" +
                  std::to_string(static_cast<unsigned long long>(primer.size())) +
                 " octets), décompressé malgré tout",
             m_options);
  }
  int16_t predictor = 0;
  auto pcm = dpcm16_decompress(std::span(primer), predictor);
  trim_runway_samples(pcm);
  channel.predictor = predictor;
  channel.predictorInitialized = true;  
  if (!m_extractAudio) {
    return;
  }
  if (!pcm.empty()) {
    const int64_t primerHalfPos = isEven ? 0 : 1;
    appendChannelSamples(isEven, primerHalfPos, pcm, 0, predictor);
  }
}

void RobotExtractor::process_audio_block(std::span<const std::byte> block,
                                         int32_t pos, bool zeroCompressed) {
  ensurePrimerProcessed();
  if (!m_extractAudio) {
    return;
  }
  std::span<const std::byte> blockBytes = block;
  std::vector<std::byte> runwayPadded;
  if (!zeroCompressed && block.size() < kRobotRunwayBytes) {
    const size_t runwayPrefix = kRobotRunwayBytes - block.size();
    runwayPadded.assign(runwayPrefix + block.size(), std::byte{0});
    if (!block.empty()) {
      auto dst = runwayPadded.begin() +
                 static_cast<std::ptrdiff_t>(runwayPrefix);
      std::copy(block.begin(), block.end(), dst);
    }
    blockBytes = runwayPadded;
  }
  struct DecodedChannelSamples {
    std::vector<int16_t> samples;
    int16_t finalPredictor = 0;
    bool predictorValid = false;
  };

  auto decodeChannelSamples = [&](const ChannelAudio &source) {
    DecodedChannelSamples decoded;
    int16_t localPredictor = 0;
    if (source.predictorInitialized) {
      localPredictor = source.predictor;
    }
    decoded.samples = dpcm16_decompress(blockBytes, localPredictor);
    trim_runway_samples(decoded.samples);
    decoded.finalPredictor = localPredictor;
    decoded.predictorValid = !blockBytes.empty();
    return decoded;
  };

  DecodedChannelSamples evenDecoded = decodeChannelSamples(m_evenChannelAudio);
  DecodedChannelSamples oddDecoded = decodeChannelSamples(m_oddChannelAudio);
  
  const int64_t relativePos = static_cast<int64_t>(pos) - m_audioStartOffset;
  const int64_t doubledPos = relativePos * 2;
  if ((doubledPos & 1LL) != 0) {
    throw std::runtime_error("Position audio incohérente");
  }

  const bool isEvenChannel = (doubledPos & 3LL) == 0;
  ChannelAudio &channel =
      isEvenChannel ? m_evenChannelAudio : m_oddChannelAudio;
  const DecodedChannelSamples &decoded =
      isEvenChannel ? evenDecoded : oddDecoded;
  
  auto hasAudibleData = [](const std::vector<int16_t> &samples) {
    return std::any_of(samples.begin(), samples.end(),
                       [](int16_t sample) { return sample != 0; });
  };

  if (pos == 0) {
    const bool hasAudioData = hasAudibleData(evenDecoded.samples) ||
                              hasAudibleData(oddDecoded.samples);
    if (hasAudioData) {
      log_warn(m_srcPath,
               "Bloc audio ignoré en position zéro (données audibles ignorées)",
               m_options);
    } else {
      log_warn(m_srcPath,
               "Bloc audio ignoré en position zéro (données silencieuses)",
               m_options);
    }
    return;
  }
  const std::vector<int16_t> &channelSamples = decoded.samples;
  const int64_t halfPos = static_cast<int64_t>(pos);

  size_t zeroCompressedPrefixSamples = 0;
  if (zeroCompressed) {
    constexpr size_t zeroPrefixSamples =
        kRobotZeroCompressSize / sizeof(int16_t);
    if (zeroPrefixSamples > kRobotRunwaySamples) {
      zeroCompressedPrefixSamples =
          std::min(channelSamples.size(),
                   zeroPrefixSamples - kRobotRunwaySamples);
    }
  }
  
  AppendPlan plan{};
  AppendPlanStatus status = prepareChannelAppend(
      channel, isEvenChannel, halfPos, channelSamples, plan,
      zeroCompressedPrefixSamples);

  auto updatePredictor = [&](ChannelAudio &target,
                             const DecodedChannelSamples &decodedSamples) {
    if (!decodedSamples.predictorValid) {
      return;
    }
    target.predictor = decodedSamples.finalPredictor;
    target.predictorInitialized = true;
  };

  switch (status) {
  case AppendPlanStatus::Ok:
  case AppendPlanStatus::Skip:
    finalizeChannelAppend(channel, isEvenChannel, halfPos, channelSamples, plan,
                          status);
    updatePredictor(channel, decoded);    
    if (status == AppendPlanStatus::Ok) {
      if (channel.hasAcceptedPos) {
        if (static_cast<int32_t>(halfPos) > channel.lastAcceptedPos) {
          channel.lastAcceptedPos = static_cast<int32_t>(halfPos);
        }
      } else {
        channel.lastAcceptedPos = static_cast<int32_t>(halfPos);
        channel.hasAcceptedPos = true;
      }
      channel.seenNonPrimerBlock = true;
    }
    return;
  case AppendPlanStatus::Conflict:
    log_warn(m_srcPath,
             "Bloc audio ignoré en raison d'un conflit à la position " +
                 std::to_string(static_cast<long long>(pos)),
             m_options);
    updatePredictor(channel, decoded);    
    return;
  case AppendPlanStatus::ParityMismatch:
    if (plan.posIsEven) {
      log_warn(m_srcPath,
               "Bloc audio ignoré (position paire reçue pour le canal impair) à la position " +
                   std::to_string(static_cast<long long>(pos)),
               m_options);
      updatePredictor(m_evenChannelAudio, evenDecoded);      
    } else {
      log_warn(m_srcPath,
               "Bloc audio ignoré (position impaire reçue pour le canal pair) à la position " +
                   std::to_string(static_cast<long long>(pos)),
               m_options);
      updatePredictor(m_oddChannelAudio, oddDecoded);      
    }
    return;
  }
}

void RobotExtractor::setAudioStartOffset(int64_t offset) {
  m_audioStartOffset = offset;
}

RobotExtractor::AppendPlanStatus RobotExtractor::prepareChannelAppend(
    ChannelAudio &channel, bool isEven, int64_t halfPos,
    const std::vector<int16_t> &samples, AppendPlan &plan,
    size_t zeroCompressedPrefixSamples) {
  const int64_t originalHalfPos = halfPos;
  const int64_t relativeHalfPos = originalHalfPos - m_audioStartOffset;
  const bool posIsEven = (relativeHalfPos & 1LL) == 0;
  plan.posIsEven = posIsEven;
  if (samples.empty()) {
    return AppendPlanStatus::Skip;
  }
  size_t inputOffset = 0;
  size_t adjustedZeroPrefix = zeroCompressedPrefixSamples;
  int64_t adjustedHalfPos = halfPos;
  if (!channel.startHalfPosInitialized) {
    channel.startHalfPos = halfPos;
    channel.startHalfPosInitialized = true;
    adjustedHalfPos = 0;
  } else if (halfPos < channel.startHalfPos) {
    const int64_t deltaHalf = channel.startHalfPos - halfPos;
    if ((deltaHalf & 1LL) != 0) {
      return AppendPlanStatus::ParityMismatch;
    }    
    const size_t deltaSamples = static_cast<size_t>(deltaHalf / 2);
    if (deltaSamples >= samples.size()) {
      return AppendPlanStatus::Skip;  
    }
    inputOffset = deltaSamples;
    adjustedHalfPos = 0;
    if (adjustedZeroPrefix > inputOffset) {
      adjustedZeroPrefix -= inputOffset;
    } else {
      adjustedZeroPrefix = 0;
    }
  } else {
    adjustedHalfPos = halfPos - channel.startHalfPos;
  }
  AppendPlanStatus status = planChannelAppend(channel, isEven, adjustedHalfPos,
                                             originalHalfPos, samples, plan,
                                             inputOffset);

  if (status != AppendPlanStatus::Skip) {
    size_t prefix = 0;
    if (adjustedZeroPrefix > plan.skipSamples) {
      prefix = adjustedZeroPrefix - plan.skipSamples;
      prefix = std::min(prefix, plan.availableSamples);
    }
    plan.zeroCompressedPrefix = prefix;
  } else {
    plan.zeroCompressedPrefix = 0;
  }

  return status;
}

RobotExtractor::AppendPlanStatus RobotExtractor::planChannelAppend(
    const ChannelAudio &channel, bool isEven, int64_t halfPos,
    int64_t originalHalfPos,
    const std::vector<int16_t> &samples, AppendPlan &plan,
    size_t inputOffset) const {
  if (samples.empty() || inputOffset >= samples.size()) {
    return AppendPlanStatus::Skip;
  }
  const bool packetIsEven =
      ((originalHalfPos - m_audioStartOffset) & 1LL) == 0;
  plan.posIsEven = packetIsEven;
  if (packetIsEven != isEven) {
    return AppendPlanStatus::ParityMismatch;
  }
  plan.inputOffset = inputOffset;
  const size_t effectiveSize = samples.size() - plan.inputOffset;
  int64_t startHalf = halfPos;
  int64_t startSampleSigned = 0;
  if (startHalf >= 0) {
    startSampleSigned = startHalf / 2;
  } else {
    startSampleSigned = (startHalf - 1) / 2;
  }
  if (startSampleSigned < 0) {
    plan.negativeAdjusted = true;
    plan.skipSamples = static_cast<size_t>(-startSampleSigned);
    if (plan.skipSamples >= effectiveSize) {
      plan.negativeIgnored = true;
      return AppendPlanStatus::Skip;
    }
    plan.startSample = 0;
  } else {
    plan.startSample = static_cast<size_t>(startSampleSigned);
  }
  plan.availableSamples = effectiveSize - plan.skipSamples;
  if (plan.availableSamples == 0) {
    return AppendPlanStatus::Skip;
  }
  if (plan.availableSamples > std::numeric_limits<size_t>::max() -
                                   plan.startSample) {
    throw std::runtime_error("Insertion audio dépasse la capacité");
  }
  plan.requiredSize = plan.startSample + plan.availableSamples;
  plan.leadingOverlap = 0;
  while (plan.leadingOverlap < plan.availableSamples) {
    size_t index = plan.startSample + plan.leadingOverlap;
    if (index >= channel.occupied.size()) {
      break;
    }
    if (!channel.occupied[index]) {
      break;
    }
    if (index >= channel.zeroCompressed.size() ||
        channel.zeroCompressed[index] == 0) {
      if (channel.samples[index] !=
          samples[plan.inputOffset + plan.skipSamples + plan.leadingOverlap]) {
        return AppendPlanStatus::Conflict;
      }
      ++plan.leadingOverlap;
      continue;
    }
    // Existing data derived from a zero-compressed block is replaceable.
    if (channel.samples[index] !=
        samples[plan.inputOffset + plan.skipSamples + plan.leadingOverlap]) {
      break;
    }
    ++plan.leadingOverlap;
  }
  if (plan.leadingOverlap == plan.availableSamples) {
    return AppendPlanStatus::Skip;
  }
  plan.trimmedStart = plan.startSample + plan.leadingOverlap;
  return AppendPlanStatus::Ok;
}

void RobotExtractor::finalizeChannelAppend(
    ChannelAudio &channel, bool /*isEven*/, int64_t halfPos,
    const std::vector<int16_t> &samples, const AppendPlan &plan,
    AppendPlanStatus status) {
  if (status == AppendPlanStatus::Skip) {
    if (plan.negativeIgnored) {
      log_warn(m_srcPath,
               "Bloc audio à position négative ignoré: " +
                   std::to_string(static_cast<long long>(halfPos)),
               m_options);
    } else if (plan.negativeAdjusted) {
      log_warn(m_srcPath,
               "Bloc audio à position négative ajusté (" +
                   std::to_string(static_cast<long long>(halfPos)) + ")",
               m_options);
    }
    return;
  }
  if (status != AppendPlanStatus::Ok) {
    return;
  }
  if (plan.negativeAdjusted) {
    log_warn(m_srcPath,
             "Bloc audio à position négative ajusté (" +
                 std::to_string(static_cast<long long>(halfPos)) + ")",
             m_options);
  }
  if (channel.samples.size() < plan.trimmedStart) {
    channel.samples.resize(plan.trimmedStart, 0);
    channel.occupied.resize(plan.trimmedStart, 0);
    channel.zeroCompressed.resize(plan.trimmedStart, 0);    
  }
  if (channel.samples.size() < plan.requiredSize) {
    channel.samples.resize(plan.requiredSize, 0);
    channel.occupied.resize(plan.requiredSize, 0);
    channel.zeroCompressed.resize(plan.requiredSize, 0);    
  }
  for (size_t i = plan.leadingOverlap; i < plan.availableSamples; ++i) {
    size_t index = plan.startSample + i;
    channel.samples[index] =
        samples[plan.inputOffset + plan.skipSamples + i];
    channel.occupied[index] = 1;
    channel.zeroCompressed[index] =
        (i < plan.zeroCompressedPrefix) ? 1 : 0;
  }
}
void RobotExtractor::appendChannelSamples(
    bool isEven, int64_t halfPos, const std::vector<int16_t> &samples,
    size_t zeroCompressedPrefixSamples,
    std::optional<int16_t> finalPredictor) {
  if (samples.empty() && !finalPredictor.has_value()) {
    return;
  }
  ChannelAudio &channel = isEven ? m_evenChannelAudio : m_oddChannelAudio;
  AppendPlan plan{};
  AppendPlanStatus status = prepareChannelAppend(
      channel, isEven, halfPos, samples, plan, zeroCompressedPrefixSamples);
  auto updatePredictor = [&]() {
    if (!finalPredictor.has_value()) {
      if (samples.empty()) {
        return;
      }
      finalPredictor = samples.back();
    }
    channel.predictor = *finalPredictor;
    channel.predictorInitialized = true;
  };  
  switch (status) {
  case AppendPlanStatus::Ok:
  case AppendPlanStatus::Skip:
    finalizeChannelAppend(channel, isEven, halfPos, samples, plan, status);
    updatePredictor();    
    return;
  case AppendPlanStatus::Conflict:
    log_warn(m_srcPath,
             "Bloc audio ignoré en raison d'un conflit à la position " +
                 std::to_string(static_cast<long long>(halfPos)),
             m_options);
    updatePredictor();    
    return;
  case AppendPlanStatus::ParityMismatch:
    if (plan.posIsEven) {
      log_warn(m_srcPath,
               "Bloc audio ignoré (position paire reçue pour le canal impair) à la position " +
                   std::to_string(static_cast<long long>(halfPos)),
               m_options);
    } else {
      log_warn(m_srcPath,
               "Bloc audio ignoré (position impaire reçue pour le canal pair) à la position " +
                   std::to_string(static_cast<long long>(halfPos)),
               m_options);
    }
    return;
  }
}

std::vector<int16_t> RobotExtractor::buildChannelStream(bool isEven) const {
  const ChannelAudio &channel = isEven ? m_evenChannelAudio : m_oddChannelAudio;
  if (channel.samples.empty()) {
    return {};
  }
  const size_t totalSamples = channel.samples.size();
  if (channel.occupied.empty()) {
    return {};
  }
  size_t firstOccupied = 0;
  while (firstOccupied < channel.occupied.size() &&
         channel.occupied[firstOccupied] == 0) {
    ++firstOccupied;
  }
  if (firstOccupied == channel.occupied.size()) {
    return {};
  }
  size_t lastOccupied = channel.occupied.size() - 1;
  while (lastOccupied > firstOccupied &&
         channel.occupied[lastOccupied] == 0) {
    --lastOccupied;
  }
  const size_t outputSize = std::min(totalSamples, lastOccupied + 1);
  std::vector<int16_t> stream(outputSize);
  std::vector<int16_t> working(channel.samples.begin(),
                               channel.samples.begin() +
                                   static_cast<std::ptrdiff_t>(outputSize));
  std::vector<uint8_t> occupied(channel.occupied.begin(),
                                channel.occupied.begin() +
                                    static_cast<std::ptrdiff_t>(outputSize));
  auto fillGapWithSilence = [&](size_t gapStart, size_t gapEnd) {
    if (gapStart >= gapEnd) {
      return;
    }
    std::fill(working.begin() + static_cast<std::ptrdiff_t>(gapStart),
              working.begin() + static_cast<std::ptrdiff_t>(gapEnd), 0);
    std::fill(occupied.begin() + static_cast<std::ptrdiff_t>(gapStart),
              occupied.begin() + static_cast<std::ptrdiff_t>(gapEnd), 1);
  };
  auto interpolateGap = [&](size_t gapStart, size_t gapEnd) {
    if (gapStart == 0) {
      fillGapWithSilence(gapStart, gapEnd);
      return;
    }
    bool hasPrevious = false;
    size_t previousIndex = 0;
    if (gapStart > 0) {
      size_t search = gapStart;
      do {
        --search;
        if (occupied[search]) {
          previousIndex = search;
          hasPrevious = true;
          break;
        }
      } while (search > 0);
      if (!hasPrevious && occupied[0]) {
        previousIndex = 0;
        hasPrevious = true;
      }
    }
    if (!hasPrevious) {
      fillGapWithSilence(gapStart, gapEnd);
      return;
    }
    bool hasNext = false;
    size_t nextIndex = 0;
    for (size_t i = gapEnd; i < outputSize; ++i) {
      if (occupied[i]) {
        nextIndex = i;
        hasNext = true;
        break;
      }
    }
    if (!hasNext) {
      fillGapWithSilence(gapStart, gapEnd);
      return;
    }
    const int32_t previousValue = working[previousIndex];
    const int32_t nextValue = working[nextIndex];
    const size_t distance = nextIndex - previousIndex;
    if (distance <= 1) {
      fillGapWithSilence(gapStart, gapEnd);
      return;
    }
    const size_t gapLength = gapEnd - gapStart;
    for (size_t i = 0; i < gapLength; ++i) {
      const size_t currentIndex = gapStart + i;
      const size_t offset = currentIndex - previousIndex;
      const int32_t interpolated =
          previousValue + static_cast<int32_t>(nextValue - previousValue) *
                                  static_cast<int32_t>(offset) /
                                  static_cast<int32_t>(distance);
      const int32_t clamped = std::clamp<int32_t>(
          interpolated, std::numeric_limits<int16_t>::min(),
          std::numeric_limits<int16_t>::max());
      working[currentIndex] = static_cast<int16_t>(clamped);
      occupied[currentIndex] = 1;
    }
  };
  for (size_t i = 0; i < firstOccupied; ++i) {
    working[i] = 0;
    occupied[i] = 1;
  }
  size_t idx = firstOccupied;
  while (idx < outputSize) {
    if (occupied[idx]) {
      ++idx;
      continue;
    }
    const size_t gapStart = idx;
    size_t gapEnd = gapStart;
    while (gapEnd < outputSize && occupied[gapEnd] == 0) {
      ++gapEnd;
    }
    interpolateGap(gapStart, gapEnd);
    idx = gapEnd;
  }
  std::copy(working.begin(), working.end(), stream.begin());
  return stream;
}

void RobotExtractor::finalizeAudio() {
  if (!m_extractAudio) {
    return;
  }
  ensurePrimerProcessed();
  auto evenStream = buildChannelStream(true);
  auto oddStream = buildChannelStream(false);
  if (evenStream.empty() && oddStream.empty()) {
    return;
  }
  const bool hasEvenData = !evenStream.empty();
  const bool hasOddData = !oddStream.empty();

  int64_t jointMinHalfPos = 0;
  bool jointMinInitialized = false;
  auto considerStart = [&](const ChannelAudio &channel, bool hasData) {
    if (!hasData || !channel.startHalfPosInitialized) {
      return;
    }
    if (!jointMinInitialized) {
      jointMinHalfPos = channel.startHalfPos;
      jointMinInitialized = true;
    } else {
      jointMinHalfPos = std::min(jointMinHalfPos, channel.startHalfPos);
    }
  };
  considerStart(m_evenChannelAudio, hasEvenData);
  considerStart(m_oddChannelAudio, hasOddData);
  if (!jointMinInitialized) {
    jointMinHalfPos = 0;
    jointMinInitialized = true;
  }

  auto applyLeadingSilence = [&](std::vector<int16_t> &stream,
                                 const ChannelAudio &channel, bool isEven) {
    if (stream.empty() || !channel.startHalfPosInitialized) {
      return;
    }
    int64_t relativeHalfPos = channel.startHalfPos - jointMinHalfPos;
    if (relativeHalfPos <= 0) {
      return;
    }
    int64_t adjust = 0;
    if ((relativeHalfPos & 1LL) != 0 && isEven && (jointMinHalfPos & 1LL) != 0) {
      adjust = 1;
    }
    const int64_t leadingSamples64 = (relativeHalfPos + adjust) / 2;
    if (leadingSamples64 <= 0) {
      return;
    }
    if (leadingSamples64 >
        static_cast<int64_t>(std::numeric_limits<size_t>::max())) {
      throw std::runtime_error("Décalage audio dépasse la capacité");
    }
    const size_t leadingSamples = static_cast<size_t>(leadingSamples64);
    stream.insert(stream.begin(), leadingSamples, 0);
  };

  applyLeadingSilence(evenStream, m_evenChannelAudio, true);
  applyLeadingSilence(oddStream, m_oddChannelAudio, false);

  const size_t maxSamples = std::max(evenStream.size(), oddStream.size());
  if (hasEvenData) {
    evenStream.resize(maxSamples, 0);
  }
  if (hasOddData) {
    oddStream.resize(maxSamples, 0);
  }

  std::vector<int16_t> mono;
  mono.reserve(maxSamples * 2);
  for (size_t i = 0; i < maxSamples; ++i) {
    const int16_t evenSample = i < evenStream.size() ? evenStream[i] : 0;
    const int16_t oddSample = i < oddStream.size() ? oddStream[i] : 0;
    mono.push_back(evenSample);
    // Conserver l'ordre pair puis impair tel que décrit dans ScummVM/robot.h.
    mono.push_back(oddSample);
  }
  writeWav(mono, kSampleRate, 0, true, 2, false);
}

RobotExtractor::ParsedPalette
RobotExtractor::parseHunkPalette(std::span<const std::byte> raw) {
  auto parseImpl = [raw]() mutable -> ParsedPalette {
    ParsedPalette parsed;
    if (raw.empty()) {
      parsed.valid = true;
      return parsed;
    }
    if (raw.size() < kHunkPaletteHeaderSize) {
      throw std::runtime_error("Palette SCI HunkPalette trop courte");
    }

    const uint8_t numPalettes = read_u8(raw, kNumPaletteEntriesOffset);
    const size_t offsetTablePos = kHunkPaletteHeaderSize;
    if (numPalettes == 0) {
      if (offsetTablePos < raw.size()) {
        parsed.remapData.assign(raw.begin() + offsetTablePos, raw.end());
        if (parsed.remapData.size() > kRawPaletteSize) {
          parsed.remapData.resize(kRawPaletteSize);
        }
      }
      parsed.valid = true;
      return parsed;
    }
    
    const size_t offsetsBytesDeclared = static_cast<size_t>(2 * numPalettes);
    const size_t bytesAvailableForOffsets =
        raw.size() > offsetTablePos ? raw.size() - offsetTablePos : 0;
    const size_t offsetsInBlob =
        std::min<size_t>(numPalettes, bytesAvailableForOffsets / 2);
    const size_t actualOffsetsBytes = offsetsInBlob * 2;
    const size_t offsetsEnd = offsetTablePos + actualOffsetsBytes;
    const size_t tableEnd = offsetTablePos + offsetsBytesDeclared;

    auto readOffsets = [&](bool assumeBigEndian) {
      std::vector<size_t> offsets;
      offsets.reserve(offsetsInBlob);
      for (size_t i = 0; i < offsetsInBlob; ++i) {
        const size_t pos = offsetTablePos + i * 2;
        const uint16_t entryOffset = assumeBigEndian
                                         ? read_u16_be(raw, pos)
                                         : read_u16(raw, pos);
        offsets.push_back(static_cast<size_t>(entryOffset));
      }
      return offsets;
    };

    auto offsetsLE = readOffsets(false);
    auto offsetsBE = readOffsets(true);
    auto scoreOffsets = [&](const std::vector<size_t> &offsets) {
      size_t valid = 0;
      for (const auto offset : offsets) {
        if (offset >= offsetsEnd && offset <= raw.size()) {
          ++valid;
        }
      }
      return valid;
    };
    const size_t validLE = scoreOffsets(offsetsLE);
    const size_t validBE = scoreOffsets(offsetsBE);

    bool paletteBigEndian = false;
    std::vector<size_t> offsets = offsetsLE;
    if (validBE > validLE) {
      paletteBigEndian = true;
      offsets = offsetsBE;
    }
    
    std::vector<std::byte> converted;
    if (paletteBigEndian) {
      converted.assign(raw.begin(), raw.end());
      for (size_t i = 0; i < offsets.size(); ++i) {
        write_span_le16(converted, offsetTablePos + i * 2,
                        static_cast<uint16_t>(offsets[i]));
      }
      size_t conversionMinEntryOffset = std::numeric_limits<size_t>::max();
      for (const auto offset : offsets) {
        conversionMinEntryOffset = std::min(conversionMinEntryOffset, offset);
      }
      if (tableEnd + sizeof(uint16_t) <= raw.size() &&
          conversionMinEntryOffset >= tableEnd + sizeof(uint16_t)) {
        const uint16_t candidate = read_u16_be(raw, tableEnd);
        if (candidate <= raw.size() && candidate >= conversionMinEntryOffset) {
          write_span_le16(converted, tableEnd, candidate);
        }
      }
      for (const auto offset : offsets) {
        if (offset + kEntryHeaderSize > raw.size()) {
          continue;
        }
        const uint16_t startColor =
            read_u16_be(raw, offset + kEntryStartColorOffset);
        write_span_le16(converted, offset + kEntryStartColorOffset, startColor);        
        const uint16_t numColors =
            read_u16_be(raw, offset + kEntryNumColorsOffset);
        write_span_le16(converted, offset + kEntryNumColorsOffset, numColors);
        const uint32_t version =
            read_u32_be(raw, offset + kEntryVersionOffset);
        write_span_le32(converted, offset + kEntryVersionOffset, version);
      }
      raw = converted;
    }

    struct EntryPointer {
      size_t offset;
      uint8_t index;
    };

    std::vector<EntryPointer> entryPointers;
    entryPointers.reserve(offsets.size());
    for (size_t i = 0; i < offsets.size(); ++i) {
      const size_t entryOffset = offsets[i];
      if (entryOffset < offsetsEnd || entryOffset > raw.size()) {
        continue;
      }
      entryPointers.push_back(
          EntryPointer{entryOffset, static_cast<uint8_t>(i)});
    }

    size_t minEntryOffset = entryPointers.empty()
                                ? raw.size()
                                : std::numeric_limits<size_t>::max();
    for (const auto &ptr : entryPointers) {
      minEntryOffset = std::min(minEntryOffset, ptr.offset);
    }

    bool hasExplicitRemapOffset = false;
    size_t explicitRemapOffset = 0;
    if (tableEnd + sizeof(uint16_t) <= raw.size() &&
        minEntryOffset >= tableEnd + sizeof(uint16_t)) {
      const uint16_t candidate = read_u16(raw, tableEnd);
      if (candidate <= raw.size() && candidate >= minEntryOffset) {
        hasExplicitRemapOffset = true;
        explicitRemapOffset = candidate;
      }
    }
    
    std::stable_sort(entryPointers.begin(), entryPointers.end(),
                     [](const EntryPointer &a, const EntryPointer &b) {
                       if (a.offset == b.offset) {
                         return a.index < b.index;
                       }
                       return a.offset < b.offset;
                     });

    bool firstEntry = true;
    uint16_t firstStart = 0;
    uint16_t maxEnd = 0;
    size_t lastEntryEnd = offsetsEnd;

    for (size_t entryIndex = 0; entryIndex < entryPointers.size(); ++entryIndex) {
      const auto &entryPtr = entryPointers[entryIndex];
      const size_t offset = entryPtr.offset;
      if (offset > raw.size() - kEntryHeaderSize) {
        continue;
      }
      auto entry = raw.subspan(offset);
      size_t entryLimit = raw.size();
      if (entryIndex + 1 < entryPointers.size()) {
        entryLimit = entryPointers[entryIndex + 1].offset;
      } else if (hasExplicitRemapOffset) {
        entryLimit = explicitRemapOffset;
      }
      if (entryLimit > raw.size()) {
        entryLimit = raw.size();
      }
      if (entryLimit < offset) {
        continue;
      }
      size_t entryExtent = entryLimit - offset;
      size_t maxPayloadBytes =
          entryExtent > kEntryHeaderSize ? entryExtent - kEntryHeaderSize : 0;
      const uint8_t startColor = read_u8(entry, kEntryStartColorOffset);
      const uint16_t numColors = read_u16(entry, kEntryNumColorsOffset);
      const bool defaultUsed = read_u8(entry, kEntryUsedOffset) != 0;
      const bool sharedUsed = read_u8(entry, kEntrySharedUsedOffset) != 0;
      const uint32_t version = read_u32(entry, kEntryVersionOffset);
      const size_t perColorBytes = 3 + (sharedUsed ? 0 : 1);
      const size_t paletteCapacity = 256 - startColor;
      size_t availableRecords = maxPayloadBytes / perColorBytes;
      const size_t requestedColors = static_cast<size_t>(numColors);
      const size_t requiredBytes =
          kEntryHeaderSize + requestedColors * perColorBytes;
      if (requestedColors > availableRecords) {
        size_t limitCandidate = raw.size();
        if (hasExplicitRemapOffset) {
          limitCandidate = std::min(limitCandidate, explicitRemapOffset);
        }
        if (offset + requiredBytes <= limitCandidate) {
          entryLimit = std::max(entryLimit, offset + requiredBytes);
          if (entryLimit > limitCandidate) {
            entryLimit = limitCandidate;
          }
          entryExtent = entryLimit - offset;
          maxPayloadBytes =
              entryExtent > kEntryHeaderSize ? entryExtent - kEntryHeaderSize : 0;
          availableRecords = maxPayloadBytes / perColorBytes;
        }
      }
      [[maybe_unused]] const bool truncatedByPayload =
          requestedColors > availableRecords;
      const size_t actualColors =
          std::min({requestedColors, paletteCapacity, availableRecords});

      const size_t consumedColorBytes =
          std::min(maxPayloadBytes, actualColors * perColorBytes);
#if defined(ROBOT_EXTRACTOR_ENABLE_PALETTE_CLAMP_LOG)
      if (truncatedByPayload && actualColors < requestedColors) {
        std::ostringstream oss;
        oss << "Palette HunkPalette entrée "
            << static_cast<unsigned int>(entryPtr.index)
            << " tronquée: " << requestedColors << " couleurs demandées, "
            << actualColors << " disponibles";
        log_warn(std::filesystem::path("HunkPalette"), oss.str(),
                 ExtractorOptions{});
      }
#endif
      auto colorData = entry.subspan(kEntryHeaderSize,
                                     actualColors * perColorBytes);

      size_t pos = 0;
      for (size_t i = 0; i < actualColors; ++i) {
        const size_t paletteIndex = static_cast<size_t>(startColor) + i;
        auto &dest = parsed.entries[paletteIndex];
        dest.present = true;
        if (sharedUsed) {
          dest.used = defaultUsed;
        } else {
          dest.used = std::to_integer<uint8_t>(colorData[pos++]) != 0;
        }
        dest.r = std::to_integer<uint8_t>(colorData[pos++]);
        dest.g = std::to_integer<uint8_t>(colorData[pos++]);
        dest.b = std::to_integer<uint8_t>(colorData[pos++]);
      }

      const size_t entryConsumedBytes =
          std::min(entryExtent, kEntryHeaderSize + consumedColorBytes);

      if (actualColors == 0) {
        lastEntryEnd = std::max(lastEntryEnd, offset + entryConsumedBytes);
        continue;
      }
      
      const uint32_t endColor =
          static_cast<uint32_t>(startColor) + static_cast<uint32_t>(actualColors);

      if (firstEntry) {
        parsed.startColor = startColor;
        parsed.colorCount = static_cast<uint16_t>(actualColors);
        parsed.sharedUsed = sharedUsed;
        parsed.defaultUsed = defaultUsed;
        parsed.version = version;
        firstEntry = false;
        firstStart = startColor;
        maxEnd = static_cast<uint16_t>(endColor);
      } else {
        if (startColor < firstStart) {
          parsed.startColor = startColor;
          parsed.colorCount = static_cast<uint16_t>(maxEnd - startColor);
          firstStart = startColor;
        } else {
          const uint16_t span = static_cast<uint16_t>(endColor - firstStart);
          if (span > parsed.colorCount) {
            parsed.colorCount = span;
          }
        }
        if (endColor > maxEnd) {
          maxEnd = static_cast<uint16_t>(endColor);
          if (maxEnd >= parsed.startColor) {
            parsed.colorCount = static_cast<uint16_t>(maxEnd - parsed.startColor);
          } else {
            parsed.colorCount = 0;
          }
        }
        parsed.sharedUsed = parsed.sharedUsed && sharedUsed;
      }
      
      lastEntryEnd = std::max(lastEntryEnd, offset + entryConsumedBytes);
    }

    size_t remapOffset =
        hasExplicitRemapOffset ? explicitRemapOffset : lastEntryEnd;
    if (remapOffset < lastEntryEnd) {
      remapOffset = lastEntryEnd;
    }

    if (remapOffset < raw.size()) {
      parsed.remapData.assign(raw.begin() + remapOffset, raw.end());
      if (parsed.remapData.size() > kRawPaletteSize) {
        parsed.remapData.resize(kRawPaletteSize);
      }
    }

    parsed.valid = true;
    return parsed;
  };

  try {
    return parseImpl();
  } catch (const std::bad_alloc &) {
    throw;
  } catch (const std::exception &) {
    return ParsedPalette{};
  }
}

void RobotExtractor::readPalette() {
  StreamExceptionGuard guard(m_fp);
  const std::uintmax_t fileSize = m_fileSize;
  if (!m_hasPalette) {
    if (m_paletteSize != 0)
      log_warn(m_srcPath, "paletteSize non nul alors que hasPalette==false",
               m_options);
    std::streamoff curPos = m_fp.tellg();
    if (curPos < 0 ||
        static_cast<std::uintmax_t>(curPos) + m_paletteSize > fileSize) {
      throw std::runtime_error(std::string("Palette hors limites pour ") +
                               m_srcPath.string());
    }
    m_fp.seekg(m_paletteSize, std::ios::cur);
    return;
  }

  m_palette.resize(m_paletteSize);
  try {
    read_exact(m_fp, m_palette.data(), static_cast<size_t>(m_paletteSize));
  } catch (const std::runtime_error &) {
    throw std::runtime_error(std::string("Palette tronquée pour ") +
                             m_srcPath.string());
  }
}

void RobotExtractor::readSizesAndCues(bool allowShortFile) {
  StreamExceptionGuard guard(m_fp);
  if (m_options.debug_index) {
    log_error(m_srcPath,
              "readSizesAndCues: position initiale = " +
                  std::to_string(m_fp.tellg()),
              m_options);
  }

  const std::uintmax_t fileSize = m_fileSize;
  const std::streamoff expectedPos =
      m_postPrimerPos + static_cast<std::streamoff>(m_paletteSize);
  std::streamoff pos = m_fp.tellg();
  if (pos != expectedPos) {
    log_warn(m_srcPath,
             "Flux désaligné avant les tables d'index: position actuelle " +
                 std::to_string(pos) + ", repositionnement à " +
                 std::to_string(expectedPos),
             m_options);
    m_fp.seekg(expectedPos, std::ios::beg);
    if (!m_fp) {
      throw std::runtime_error(
          "Échec du repositionnement avant les tables d'index");
    }
  }

  // Correction 4: Ajouter l'alignement 2048 octets comme dans ScummVM
  constexpr std::streamoff kRobotFrameSize = 2048;
  std::streamoff currentPos = m_fp.tellg();
  std::streamoff bytesRemaining = (currentPos - m_fileOffset) % kRobotFrameSize;
  if (bytesRemaining != 0) {
    std::streamoff alignmentOffset = kRobotFrameSize - bytesRemaining;
    m_fp.seekg(currentPos + alignmentOffset, std::ios::beg);
    if (!m_fp) {
      throw std::runtime_error("Échec de l'alignement avant tables d'index");
    }
    currentPos = m_fp.tellg();
  }

  m_frameSizes.resize(m_numFrames);
  m_packetSizes.resize(m_numFrames);
  constexpr size_t kDebugCount = 5;
  switch (m_version) {
  case 6:
    for (size_t i = 0; i < m_numFrames; ++i) {
      int32_t tmpSigned = read_scalar<int32_t>(m_fp, m_bigEndian);
      if (tmpSigned < 0) {
        throw std::runtime_error("Taille de frame négative");
      }
      uint32_t tmp = static_cast<uint32_t>(tmpSigned);
      m_frameSizes[i] = tmp;
      if (m_options.debug_index && i < kDebugCount) {
        log_error(m_srcPath,
                  "frameSizes[" + std::to_string(i) + "] = " +
                      std::to_string(tmp),
                  m_options);
      }
    }
    for (size_t i = 0; i < m_numFrames; ++i) {
      int32_t tmpSigned = read_scalar<int32_t>(m_fp, m_bigEndian);
      if (tmpSigned < 0) {
        throw std::runtime_error("Taille de paquet négative");
      }
      uint32_t tmp = static_cast<uint32_t>(tmpSigned);
      m_packetSizes[i] = tmp;
      if (m_options.debug_index && i < kDebugCount) {
        log_error(m_srcPath,
                  "packetSizes[" + std::to_string(i) + "] = " +
                      std::to_string(tmp),
                  m_options);
      }
    }
    break;
  default:
    for (size_t i = 0; i < m_numFrames; ++i) {
      uint16_t tmp = read_scalar<uint16_t>(m_fp, m_bigEndian);
      m_frameSizes[i] = tmp;
      if (m_options.debug_index && i < kDebugCount) {
        log_error(m_srcPath,
                  "frameSizes[" + std::to_string(i) + "] = " +
                      std::to_string(tmp),
                  m_options);
      }
    }
    for (size_t i = 0; i < m_numFrames; ++i) {
      uint16_t tmp = read_scalar<uint16_t>(m_fp, m_bigEndian);
      m_packetSizes[i] = tmp;
      if (m_options.debug_index && i < kDebugCount) {
        log_error(m_srcPath,
                  "packetSizes[" + std::to_string(i) + "] = " +
                      std::to_string(tmp),
                  m_options);
      }
    }
    break;
  }
  std::uintmax_t totalFrameSize = 0;
  std::uintmax_t totalPacketSize = 0;
  constexpr std::uintmax_t maxUint = std::numeric_limits<std::uintmax_t>::max();  
  for (size_t i = 0; i < m_frameSizes.size(); ++i) {
    if (m_packetSizes[i] < m_frameSizes[i]) {
      log_warn(m_srcPath,
               "Packet size < frame size (i=" + std::to_string(i) +
                   ", frame=" + std::to_string(m_frameSizes[i]) +
                   ", packet=" + std::to_string(m_packetSizes[i]) +
                   ") — ajustement à la taille de frame",
               m_options);
      m_packetSizes[i] = m_frameSizes[i];
    }
    uint64_t maxSize64 =
        static_cast<uint64_t>(m_frameSizes[i]) +
        (m_hasAudio ? static_cast<uint64_t>(m_audioBlkSize) : 0);
    if (maxSize64 > UINT32_MAX) {
      if (m_options.debug_index) {
        log_error(m_srcPath,
                  "Frame size + audio block size exceeds UINT32_MAX (i=" +
                      std::to_string(i) + ", frame=" +
                      std::to_string(m_frameSizes[i]) + ", packet=" +
                      std::to_string(m_packetSizes[i]) + ")",
                  m_options);
      }      
      throw std::runtime_error(
          "Frame size + audio block size exceeds UINT32_MAX");
    }
    uint32_t maxSize = static_cast<uint32_t>(maxSize64);
    if (m_packetSizes[i] > maxSize) {
      std::string message =
          "Packet size > frame size + audio block size (i=" +
          std::to_string(i) + ", frame=" + std::to_string(m_frameSizes[i]) +
          ", packet=" + std::to_string(m_packetSizes[i]) + ", max=" +
          std::to_string(maxSize) + ")";
      log_warn(m_srcPath, message, m_options);
    }
    const uint32_t frameSize = m_frameSizes[i];
    const uint32_t packetSize = m_packetSizes[i];
    if (frameSize > maxUint - totalFrameSize) {
      throw std::runtime_error(
          "Somme des tailles de frame dépasse la capacité maximale");
    }
    if (packetSize > maxUint - totalPacketSize) {
      throw std::runtime_error(
          "Somme des tailles de paquets dépasse la capacité maximale");
    }
    totalFrameSize += frameSize;
    totalPacketSize += packetSize;    
  }
  for (auto &time : m_cueTimes) {
    time = read_scalar<int32_t>(m_fp, m_bigEndian);
  }
  for (auto &value : m_cueValues) {
    value = read_scalar<uint16_t>(m_fp, m_bigEndian);
  }
  std::streamoff posAfter = m_fp.tellg();
  if (posAfter < 0) {
    throw std::runtime_error(
        "Position de lecture invalide après les tables d'index");
  }
  if (posAfter < m_fileOffset) {
    throw std::runtime_error(
        "Position des tables Robot avant le début déclaré du fichier");
  }
  std::streamoff bytesRemaining = (posAfter - m_fileOffset) % 2048;
  if (bytesRemaining != 0) {
    m_fp.seekg(2048 - bytesRemaining, std::ios::cur);
  }
  std::streamoff frameDataPos = m_fp.tellg();
  if (frameDataPos < 0) {
    throw std::runtime_error("Position de début des frames invalide");
  }
  const std::uintmax_t frameDataOffset = static_cast<std::uintmax_t>(frameDataPos);
  if (frameDataOffset > fileSize) {
    throw std::runtime_error("Les tables d'index dépassent la taille du fichier");
  }
  const std::uintmax_t remainingBytes = fileSize - frameDataOffset;
  if (!allowShortFile) {
    if (totalFrameSize > remainingBytes) {
      throw std::runtime_error(
          "Somme des tailles de frame dépasse les données restantes du fichier");
    }
    if (totalPacketSize > remainingBytes) {
      throw std::runtime_error(
          "Somme des tailles de paquets dépasse les données restantes du fichier");
    }
  }  
}

bool RobotExtractor::exportFrame(int frameNo, nlohmann::json &frameJson) {
  StreamExceptionGuard guard(m_fp);
  std::streamoff curPos = m_fp.tellg();
  if (curPos < 0) {
    throw std::runtime_error("Position de lecture des frames invalide");
  }
  const std::uintmax_t frameOffset = static_cast<std::uintmax_t>(curPos);
  const std::uintmax_t fileSize = m_fileSize;
  if (frameOffset > fileSize) {
    throw std::runtime_error("Position de lecture des frames hors du fichier");
  }
  const std::uintmax_t bytesRemaining = fileSize - frameOffset;
  const uint32_t frameSize = m_frameSizes[frameNo];
  if (frameSize > bytesRemaining) {
    throw std::runtime_error(
        "Taille de frame dépasse les données restantes du fichier");
  }
  m_frameBuffer.resize(frameSize);
  read_exact(m_fp, m_frameBuffer.data(), m_frameBuffer.size());
  uint16_t numCels = 0;
  size_t offset = 0;
  if (m_frameBuffer.size() >= 2) {
    numCels = read_scalar<uint16_t>(
        std::span(m_frameBuffer).subspan(0, 2), m_bigEndian);
    offset = 2;
  } else {
    if (!m_frameBuffer.empty() && m_options.debug_index) {
      log_error(m_srcPath,
                "Frame " + std::to_string(frameNo) +
                    " plus courte que le champ numCels — traitée comme vide",
                m_options);
    }
    offset = m_frameBuffer.size();
  }
  if (numCels > static_cast<uint16_t>(m_maxCelsPerFrame)) {
    log_warn(m_srcPath,
             "Nombre de cels excessif dans la frame " + std::to_string(frameNo) +
                 " (" + std::to_string(numCels) + " > " +
                 std::to_string(m_maxCelsPerFrame) + ")",
             m_options);
    if (numCels > static_cast<uint16_t>(std::numeric_limits<int16_t>::max())) {
      m_maxCelsPerFrame = std::numeric_limits<int16_t>::max();
    } else {
      m_maxCelsPerFrame = static_cast<int16_t>(numCels);
    }
  }

  frameJson["frame"] = frameNo;
  frameJson["cels"] = nlohmann::json::array();
  ParsedPalette parsedPalette;
  bool paletteUsable = false;
  auto dumpPaletteFallback = [&]() {
    if (m_paletteFallbackDumped) {
      return;
    }
    auto fallbackPath = m_dstDir / kPaletteFallbackFilename;
    std::ofstream rawOut(fallbackPath, std::ios::binary);
    if (!rawOut) {
      throw std::runtime_error(
          std::string("Échec de l'ouverture du fichier de palette brut: ") +
          fallbackPath.string());
    }
    if (!m_palette.empty()) {
      rawOut.write(reinterpret_cast<const char *>(m_palette.data()),
                   checked_streamsize(m_palette.size()));
    }
    rawOut.flush();
    if (!rawOut) {
      throw std::runtime_error(
          std::string("Échec de l'écriture de la palette brute: ") +
          fallbackPath.string());
    }
    m_paletteFallbackDumped = true;
  };
  
  if (!m_hasPalette) {
    frameJson["palette_required"] = true;
    log_warn(m_srcPath,
             "Palette manquante, décodage des cels sans PNG pour la frame " +
                 std::to_string(frameNo),
             m_options);
  } else if (m_paletteParseFailed) {
    frameJson["palette_required"] = true;
    frameJson["palette_parse_failed"] = true;
    frameJson["palette_raw"] = kPaletteFallbackFilename;
    dumpPaletteFallback();
  } else {
    parsedPalette = parseHunkPalette(m_palette);
    if (!parsedPalette.valid) {
      log_warn(m_srcPath,
               "Palette HunkPalette invalide, export brut de la palette",
               m_options);
      m_paletteParseFailed = true;
      frameJson["palette_required"] = true;
      frameJson["palette_parse_failed"] = true;
      frameJson["palette_raw"] = kPaletteFallbackFilename;
      dumpPaletteFallback();
    } else {
      paletteUsable = true;
    }
  }
  
  const size_t celLimit = celPixelLimit();
  const size_t rgbaLimit = rgbaBufferLimit();
  
  for (int i = 0; i < numCels; ++i) {
    if (offset + kCelHeaderSize > m_frameBuffer.size()) {
      throw std::runtime_error("En-tête de cel invalide");
    }
    auto celHeader = std::span(m_frameBuffer).subspan(offset, kCelHeaderSize);
    uint8_t verticalScale = std::to_integer<uint8_t>(celHeader[1]);
    uint16_t w = read_scalar<uint16_t>(celHeader.subspan(2, 2), m_bigEndian);
    uint16_t h = read_scalar<uint16_t>(celHeader.subspan(4, 2), m_bigEndian);
    int16_t x = read_scalar<int16_t>(celHeader.subspan(10, 2), m_bigEndian);
    int16_t y = read_scalar<int16_t>(celHeader.subspan(12, 2), m_bigEndian);
    uint16_t dataSize =
        read_scalar<uint16_t>(celHeader.subspan(14, 2), m_bigEndian);
    uint16_t numChunks =
        read_scalar<uint16_t>(celHeader.subspan(16, 2), m_bigEndian);
    offset += kCelHeaderSize;

    if (offset + dataSize > m_frameBuffer.size()) {
      throw std::runtime_error("Cel data exceeds frame buffer");
    }

    const int sourceHeight =
        detail::validate_cel_dimensions(w, h, verticalScale);
    if (static_cast<size_t>(h) > SIZE_MAX / static_cast<size_t>(w)) {
      throw std::runtime_error("Multiplication w*h dépasse SIZE_MAX");
    }
    size_t pixel_count = static_cast<size_t>(w) * h;
    if (pixel_count > celLimit) {
      throw std::runtime_error("Dimensions de cel invalides");
    }
    if (static_cast<size_t>(sourceHeight) > SIZE_MAX / static_cast<size_t>(w)) {
      throw std::runtime_error(
          "Débordement lors du calcul de la taille de cel");
    }
    size_t expected =
        static_cast<size_t>(w) * static_cast<size_t>(sourceHeight);
    if (expected > celLimit) {
      throw std::runtime_error("Cel décompressé dépasse la taille maximale");
    }
    m_celBuffer.clear();
    m_celBuffer.reserve(expected);
    size_t cel_offset = offset;
    for (int j = 0; j < numChunks; ++j) {
      if (cel_offset + 10 > m_frameBuffer.size()) {
        throw std::runtime_error("En-tête de chunk invalide");
      }
      auto chunkHeader = std::span(m_frameBuffer).subspan(cel_offset, 10);
      uint32_t compSz =
          read_scalar<uint32_t>(chunkHeader.subspan(0, 4), m_bigEndian);
      uint32_t decompSz =
          read_scalar<uint32_t>(chunkHeader.subspan(4, 4), m_bigEndian);
      uint16_t compType =
          read_scalar<uint16_t>(chunkHeader.subspan(8, 2), m_bigEndian);
      cel_offset += 10;

      size_t remaining_expected = expected - m_celBuffer.size();
      if (decompSz > remaining_expected) {
        log_error(m_srcPath,
                  "Taille de chunk décompressé excède l'espace "
                  "restant pour le cel " +
                      std::to_string(i) + " dans la frame " +
                      std::to_string(frameNo),
                  m_options);
        if (cel_offset + compSz > m_frameBuffer.size()) {
          throw std::runtime_error("Données de chunk insuffisantes");
        }
        cel_offset += compSz;
        continue;
      }
      if (cel_offset + compSz > m_frameBuffer.size()) {
        throw std::runtime_error("Données de chunk insuffisantes");
      }
      auto comp = std::span(m_frameBuffer).subspan(cel_offset, compSz);
      if (compType == 0) {
        auto decomp =
            lzs_decompress(comp, decompSz, std::span<const std::byte>(m_celBuffer));
        m_celBuffer.insert(m_celBuffer.end(), decomp.begin(), decomp.end());
      } else if (compType == 2) {
        if (compSz < decompSz) {
          throw std::runtime_error(
              "Données de cel malformées: chunk plus petit que la taille décompressée annoncée");
        }
        m_celBuffer.insert(m_celBuffer.end(), comp.begin(),
                           comp.begin() + static_cast<ptrdiff_t>(decompSz));
      } else {
        throw std::runtime_error("Type de compression inconnu: " +
                                 std::to_string(compType));
      }
      cel_offset += compSz;
    }

    size_t bytes_consumed = cel_offset - offset;
    if (bytes_consumed != dataSize) {
      throw std::runtime_error(
          "Données de cel malformées: taille déclarée incohérente");
    }

    if (m_celBuffer.size() != expected) {
      throw std::runtime_error("Cel corrompu: taille de données incohérente");
    }

    if (verticalScale != 100) {
      std::vector<std::byte> expanded(static_cast<size_t>(w) * h);
      expand_cel(expanded, m_celBuffer, w, h, verticalScale);
      m_celBuffer = std::move(expanded);
    }

    if (paletteUsable) {
      // Taille d'une ligne en octets (largeur en pixels * 4 octets RGBA)
      size_t row_size = static_cast<size_t>(w) * 4;
      // Vérifie qu'on peut multiplier la hauteur par la taille d'une ligne
      // sans dépasser SIZE_MAX
      if (row_size != 0 && static_cast<size_t>(h) > SIZE_MAX / row_size) {
        throw std::runtime_error(
            "Débordement lors du calcul de la taille du tampon");
      }
      size_t required = static_cast<size_t>(h) * row_size;
      if (required > rgbaLimit) {
        throw std::runtime_error("Tampon RGBA dépasse la limite");
      }
      if (required > m_rgbaBuffer.capacity()) {
        m_rgbaBuffer.reserve(required);
      }
      m_rgbaBuffer.resize(required);
      bool conversionOk = true;
      uint8_t missingIndex = 0;      
      for (size_t pixel = 0; pixel < m_celBuffer.size(); ++pixel) {
        const uint8_t idx = std::to_integer<uint8_t>(m_celBuffer[pixel]);
        const auto &color = parsedPalette.entries[idx];
        if (!color.present) {
          conversionOk = false;
          missingIndex = idx;
          break;
        }
        m_rgbaBuffer[pixel * 4 + 0] = std::byte{color.r};
        m_rgbaBuffer[pixel * 4 + 1] = std::byte{color.g};
        m_rgbaBuffer[pixel * 4 + 2] = std::byte{color.b};
        m_rgbaBuffer[pixel * 4 + 3] =
            static_cast<std::byte>(color.used ? 255 : 0);
      }

      if (!conversionOk) {
        log_warn(m_srcPath,
                 "Indice de palette hors limites: " +
                     std::to_string(static_cast<unsigned int>(missingIndex)) +
                     ", export PNG abandonné",
                 m_options);
        paletteUsable = false;
        m_paletteParseFailed = true;
        frameJson["palette_required"] = true;
        frameJson["palette_parse_failed"] = true;
        frameJson["palette_raw"] = kPaletteFallbackFilename;
        dumpPaletteFallback();
      } else {
        std::ostringstream oss;
        oss << std::setw(5) << std::setfill('0') << frameNo << "_" << i
            << ".png";
        auto outPath = m_dstDir / oss.str();
        write_png_cross_platform(outPath, w, h, 4, m_rgbaBuffer.data(), w * 4);
      }
    }

    nlohmann::json celJson;
    celJson["index"] = i;
    celJson["x"] = x;
    celJson["y"] = y;
    celJson["width"] = w;
    celJson["height"] = h;
    celJson["vertical_scale"] = verticalScale;
    if (!paletteUsable) {
      celJson["palette_required"] = true;
    }
    frameJson["cels"].push_back(celJson);
    offset = cel_offset;
  }
  auto remaining = static_cast<std::ptrdiff_t>(m_frameBuffer.size()) -
                   static_cast<std::ptrdiff_t>(offset);
  if (remaining != 0) {
    throw std::runtime_error(std::to_string(remaining) +
                             " octets non traités dans la frame");
  }

  if (m_hasAudio) {
    if (m_packetSizes[frameNo] > m_frameSizes[frameNo]) {
      uint32_t audioBlkLen = m_packetSizes[frameNo] - m_frameSizes[frameNo];
      if (audioBlkLen < kRobotAudioHeaderSize) {
        log_error(m_srcPath,
                  "Bloc audio trop court: " +
                      std::to_string(static_cast<unsigned long long>(audioBlkLen)) +
                      " < taille d'en-tête "+
                      std::to_string(static_cast<unsigned long long>(kRobotAudioHeaderSize)),
                  m_options);
        throw std::runtime_error("Bloc audio trop court");
      } else {
        const int64_t expectedAudioBlockSize =
            static_cast<int64_t>(m_audioBlkSize) -
            static_cast<int64_t>(kRobotAudioHeaderSize);
        if (expectedAudioBlockSize < 0) {
          throw std::runtime_error(
              "Taille de bloc audio attendue négative pour la frame " +
              std::to_string(frameNo) + ": " +
              std::to_string(static_cast<long long>(expectedAudioBlockSize)));
        }
        const bool silentAudioBlock = expectedAudioBlockSize == 0;
        int64_t consumed = 0;
        int32_t pos = read_scalar<int32_t>(m_fp, m_bigEndian);
        consumed += 4;
        if (pos < 0) {
          log_warn(m_srcPath,
                   "Bloc audio avec position négative: " +
                       std::to_string(static_cast<long long>(pos)),
                   m_options);
        }
        if (pos == 0) {
          log_warn(m_srcPath, "Bloc audio ignoré en position zéro", m_options);
          int64_t bytesToSkip = static_cast<int64_t>(audioBlkLen) - consumed;
          if (bytesToSkip < 0) {
            throw std::runtime_error(
                "Bloc audio consommé au-delà de sa taille déclarée");
          }
          if (bytesToSkip > 0) {
            m_fp.seekg(static_cast<std::streamoff>(bytesToSkip), std::ios::cur);
            consumed += bytesToSkip;
          }
        } else {
          int32_t size = read_scalar<int32_t>(m_fp, m_bigEndian);
          consumed += 4;
          if (size < 0) {
            throw std::runtime_error("Taille audio invalide");
          }
          if (!silentAudioBlock && size > expectedAudioBlockSize) {
            throw std::runtime_error(
                "Taille de bloc audio inattendue: " + std::to_string(size) +
                " (maximum " +
                std::to_string(static_cast<long long>(expectedAudioBlockSize)) +
                ")");
          }
          if (silentAudioBlock) {
            int64_t bytesToSkip = static_cast<int64_t>(audioBlkLen) - consumed;
            if (bytesToSkip < 0) {
              throw std::runtime_error(
                  "Bloc audio consommé au-delà de sa taille déclarée");
            }
            if (bytesToSkip > 0) {
              m_fp.seekg(static_cast<std::streamoff>(bytesToSkip), std::ios::cur);
              consumed += bytesToSkip;
            }
          } else {
            std::vector<std::byte> block;
            bool zeroCompressed = false;            
            if (size == expectedAudioBlockSize) {
              const size_t expectedSize =
                  expectedAudioBlockSize > 0
                      ? static_cast<size_t>(expectedAudioBlockSize)
                      : size_t{0};
              block.resize(expectedSize);
              if (!block.empty()) {
                m_fp.read(reinterpret_cast<char *>(block.data()),
                          checked_streamsize(block.size()));
              }
              consumed += static_cast<int64_t>(block.size());
            } else {
              const size_t bytesToRead =
                  size > 0 ? static_cast<size_t>(size) : size_t{0};
              std::vector<std::byte> truncated(bytesToRead);
              if (!truncated.empty()) {
                m_fp.read(reinterpret_cast<char *>(truncated.data()),
                          checked_streamsize(truncated.size()));
              }
              consumed += static_cast<int64_t>(bytesToRead);
              const size_t zeroPrefix = kRobotZeroCompressSize;
              if (bytesToRead > std::numeric_limits<size_t>::max() - zeroPrefix) {
                throw std::runtime_error("Audio block too large");
              }
              block.assign(zeroPrefix + truncated.size(), std::byte{0});
              if (!truncated.empty()) {
                auto dst =
                    block.begin() + static_cast<std::ptrdiff_t>(zeroPrefix);
                std::copy(truncated.begin(), truncated.end(), dst);
              }
              zeroCompressed = true;              
            }
            if (!block.empty()) {
              process_audio_block(block, pos, zeroCompressed);
            }
          }
        }
        int64_t remainingBytes = static_cast<int64_t>(audioBlkLen) - consumed;
        if (remainingBytes < 0) {
          throw std::runtime_error(
              "Bloc audio consommé au-delà de sa taille déclarée");
        }
        if (remainingBytes > 0) {
          m_fp.seekg(static_cast<std::streamoff>(remainingBytes), std::ios::cur);
        }      
      }
    }
  }
  return true;
}

size_t RobotExtractor::celPixelLimit() const {
  size_t limit = 0;
  for (uint32_t area : m_fixedCelSizes) {
    limit = std::max(limit, static_cast<size_t>(area));
  }

  if (limit == 0 && m_xRes > 0 && m_yRes > 0) {
    const auto width = static_cast<size_t>(m_xRes);
    const auto height = static_cast<size_t>(m_yRes);
    if (height <= SIZE_MAX / width) {
      limit = width * height;
    }
  }

  if (limit == 0) {
    const std::uintmax_t fileSize = m_fileSize;
    if (fileSize >= static_cast<std::uintmax_t>(std::numeric_limits<size_t>::max())) {
      limit = std::numeric_limits<size_t>::max();
    } else {
      limit = static_cast<size_t>(fileSize);
    }
  }

  return limit;
}

size_t RobotExtractor::rgbaBufferLimit() const {
  const size_t pixelLimit = celPixelLimit();
  if (pixelLimit > SIZE_MAX / 4) {
    return SIZE_MAX / 4;
  }
  return pixelLimit * 4;
}

void RobotExtractor::writeWav(const std::vector<int16_t> &samples,
                              uint32_t sampleRate, size_t blockIndex,
                              bool isEvenChannel, uint16_t numChannels,
                              bool appendChannelSuffix) {
  if (sampleRate == 0) {
    throw std::runtime_error("Fréquence d'échantillonnage nulle");
  }
  if (numChannels == 0) {
    throw std::runtime_error("Nombre de canaux audio nul");
  }
  if (samples.size() > std::numeric_limits<size_t>::max() / sizeof(int16_t)) {
    throw std::runtime_error("Nombre d'échantillons audio dépasse la limite, "
                             "fichier WAV corrompu potentiel");
  }
  if (samples.size() % numChannels != 0) {
    throw std::runtime_error("Flux PCM intercalé mal formé");
  }
  size_t data_size = samples.size() * sizeof(int16_t);
  if (data_size > 0xFFFFFFFFu - 36) {
    throw std::runtime_error(
        "Taille de données audio trop grande pour un fichier WAV: " +
        std::to_string(data_size));
  }
  constexpr uint16_t kBitsPerSample = 16;
  const uint16_t kNumChannels = numChannels;
  const uint16_t kBlockAlign =
      static_cast<uint16_t>((kNumChannels * kBitsPerSample) / 8);

  if (sampleRate > std::numeric_limits<uint32_t>::max() / kBlockAlign) {
    throw std::runtime_error("Fréquence d'échantillonnage trop élevée: " +
                             std::to_string(sampleRate));
  }

  uint32_t byte_rate = sampleRate * kBlockAlign;
  uint32_t riff_size = 36 + static_cast<uint32_t>(data_size);

  std::array<char, 44> header{};

  header[0] = 'R';
  header[1] = 'I';
  header[2] = 'F';
  header[3] = 'F';
  write_le32(header.data() + 4, riff_size);
  header[8] = 'W';
  header[9] = 'A';
  header[10] = 'V';
  header[11] = 'E';
  header[12] = 'f';
  header[13] = 'm';
  header[14] = 't';
  header[15] = ' ';
  write_le32(header.data() + 16, 16); // fmt chunk size
  write_le16(header.data() + 20, 1);  // PCM
  write_le16(header.data() + 22, kNumChannels);
  write_le32(header.data() + 24, sampleRate);
  write_le32(header.data() + 28, byte_rate);
  write_le16(header.data() + 32, kBlockAlign);
  write_le16(header.data() + 34, kBitsPerSample);
  header[36] = 'd';
  header[37] = 'a';
  header[38] = 't';
  header[39] = 'a';
  write_le32(header.data() + 40, static_cast<uint32_t>(data_size));
  std::ostringstream wavName;
  wavName << "frame_" << std::setw(5) << std::setfill('0') << blockIndex
          << ((appendChannelSuffix && kNumChannels == 1)
                  ? (isEvenChannel ? "_even" : "_odd")
                  : "")
          << ".wav";
  auto outPath = m_dstDir / wavName.str();
  auto [fsOutPath, outPathStr] = to_long_path(outPath);
  std::ofstream wavFile(fsOutPath, std::ios::binary);
  if (!wavFile) {
    throw std::runtime_error("Échec de l'ouverture du fichier WAV: " +
                             outPathStr);
  }
  try {
    wavFile.write(header.data(), checked_streamsize(header.size()));

    std::vector<char> serializedSamples(data_size);
    char *dst = serializedSamples.data();
    for (int16_t sample : samples) {
      write_le16(dst, static_cast<uint16_t>(sample));
      dst += sizeof(uint16_t);
    }

    if (!serializedSamples.empty()) {
      wavFile.write(serializedSamples.data(),
                    checked_streamsize(serializedSamples.size()));
    }
    wavFile.flush();
    if (!wavFile) {
      throw std::runtime_error("Échec de l'écriture du fichier WAV: " +
                               outPathStr);
    }
  } catch (...) {
    wavFile.close();
    std::error_code ec;
    std::filesystem::remove(fsOutPath, ec);
    throw;
  }
  wavFile.close();
  if (wavFile.fail()) {
    throw std::runtime_error("Échec de la fermeture du fichier WAV: " +
                             outPathStr);
  }
}

void RobotExtractor::extract() {
  m_evenChannelAudio.samples.clear();
  m_evenChannelAudio.occupied.clear();
  m_evenChannelAudio.zeroCompressed.clear();
  m_evenChannelAudio.startHalfPos = 0;
  m_evenChannelAudio.startHalfPosInitialized = false;
  m_evenChannelAudio.predictor = 0;
  m_evenChannelAudio.predictorInitialized = false;
  m_oddChannelAudio.samples.clear();
  m_oddChannelAudio.occupied.clear();
  m_oddChannelAudio.zeroCompressed.clear();
  m_oddChannelAudio.startHalfPos = 0;
  m_oddChannelAudio.startHalfPosInitialized = false;
  m_oddChannelAudio.predictor = 0;
  m_oddChannelAudio.predictorInitialized = false;
  m_audioStartOffset = 0;
  readHeader();
  readPrimer();
  readPalette();
  readSizesAndCues();
  nlohmann::json jsonDoc;
  jsonDoc["version"] = "1.0.0";
  jsonDoc["frames"] = nlohmann::json::array();
  for (int i = 0; i < m_numFrames; ++i) {
    auto pos = m_fp.tellg();
    std::streamoff posOff = pos;
    nlohmann::json frameJson;
    if (exportFrame(i, frameJson)) {
      jsonDoc["frames"].push_back(frameJson);
    }
    const auto packetOff = static_cast<std::streamoff>(m_packetSizes[i]);
    if (posOff > std::numeric_limits<std::streamoff>::max() - packetOff) {
      throw std::runtime_error(
          "Position de paquet dépasse std::streamoff::max");
    }
    std::streamoff expectedPos = posOff + packetOff;
    std::streamoff actualPos = m_fp.tellg();
    if (actualPos < 0) {
      throw std::runtime_error(
          "Position de lecture après frame invalide");
    }
    if (expectedPos < actualPos) {
      log_warn(m_srcPath,
               "Position de paquet avant la position actuelle (frame=" +
                   std::to_string(i) + ", attendu=" +
                   std::to_string(static_cast<long long>(expectedPos)) +
                   ", actuel=" +
                   std::to_string(static_cast<long long>(actualPos)) +
                   ")",
               m_options);
    } else if (expectedPos > actualPos) {
      m_fp.seekg(expectedPos, std::ios::beg);
      if (!m_fp) {
        throw std::runtime_error(
            "Échec du repositionnement à la fin du paquet");
      }
    }
  }

  finalizeAudio();
  
  auto tmpPath = m_dstDir / "metadata.json.tmp";
  std::string tmpPathStr = tmpPath.string();
  struct TempFileGuard {
    std::filesystem::path path;
    bool active{true};
    ~TempFileGuard() noexcept {
      if (active) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
      }
    }
    void release() noexcept { active = false; }
  } guard{tmpPath};

  {
    std::ofstream jsonFile(tmpPath, std::ios::binary);
    if (!jsonFile) {
      throw std::runtime_error(
          std::string("Échec de l'ouverture du fichier JSON temporaire: ") +
          tmpPathStr);
    }
    jsonFile << std::setw(2) << jsonDoc;
    jsonFile.flush();
    if (!jsonFile) {
      throw std::runtime_error(
          std::string("Échec de l'écriture du fichier JSON temporaire: ") +
          tmpPathStr);
    }
    jsonFile.close();
    if (jsonFile.fail()) {
      throw std::runtime_error(
          std::string("Échec de la fermeture du fichier JSON temporaire: ") +
          tmpPathStr);
    }
  }
  auto finalPath = m_dstDir / "metadata.json";
  std::error_code ec;
  std::filesystem::rename(tmpPath, finalPath, ec);
  if (ec) {
    std::filesystem::copy_file(
        tmpPath, finalPath, std::filesystem::copy_options::overwrite_existing,
        ec);
    if (ec) {
      throw std::runtime_error("Échec de la copie de " + tmpPathStr + " vers " +
                               finalPath.string() + ": " + ec.message());
    }
    std::filesystem::remove(tmpPath, ec);
    if (ec) {
      throw std::runtime_error(
          "Échec de la suppression du fichier temporaire " + tmpPathStr + ": " +
          ec.message());
    }
  }
  guard.release();
}

} // namespace robot

#ifndef ROBOT_EXTRACTOR_NO_MAIN
int main(int argc, char *argv[]) {
  bool extractAudio = false;
  robot::ExtractorOptions options;
  std::vector<std::string> files;
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    bool known = false;
    if (arg == "--audio") {
      extractAudio = true;
      known = true;
    } else if (arg == "--quiet") {
      options.quiet = true;
      known = true;
    } else if (arg == "--force-be") {
      options.force_be = true;
      known = true;
    } else if (arg == "--force-le") {
      options.force_le = true;
      known = true;
    } else if (arg == "--debug-index") {
      options.debug_index = true;
      known = true;      
    }
    if (!known)
      files.push_back(arg);
  }
  if (options.force_be && options.force_le) {
    std::cerr << "Les options --force-be et --force-le sont mutuellement "
                 "exclusives\n";
    return 1;
  }
  if (files.size() != 2) {
    std::cerr << "Usage: " << argv[0]
              << " [--audio] [--quiet] [--force-be | --force-le]"
                 " [--debug-index] <input.rbt> <output_dir>\n";
    return 1;
  }
  try {
    std::filesystem::create_directories(files[1]);
    robot::RobotExtractor extractor(files[0], files[1], extractAudio, options);
    extractor.extract();
  } catch (const std::exception &e) {
    robot::log_error(files[0], e.what(), options);
    return 1;
  }
  return 0;
}
#endif
