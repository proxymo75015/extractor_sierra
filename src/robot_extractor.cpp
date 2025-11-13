// ═══════════════════════════════════════════════════════════════════════════
// Implémentation de l'extracteur Robot
// ═══════════════════════════════════════════════════════════════════════════
//
// Ce fichier implémente la logique d'extraction et d'export des frames (images)
// et des pistes audio du format Robot utilisé par ScummVM (versions 4, 5, 6).
//
// Fonctionnalités principales :
// - Lecture et validation des en-têtes Robot
// - Décompression LZS des cels (cellules d'animation)
// - Décodage DPCM-16 de l'audio
// - Gestion des palettes SCI HunkPalette
// - Export PNG et WAV
// ═══════════════════════════════════════════════════════════════════════════
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
// ─────────────────────────────────────────────────────────────────────────────
// Constantes pour le format SCI HunkPalette
// ─────────────────────────────────────────────────────────────────────────────
// Le format HunkPalette est utilisé par les jeux SCI de Sierra pour stocker
// les palettes de couleurs. Structure : en-tête (13 octets) + entrées palette.
constexpr size_t kHunkPaletteHeaderSize = 13;        // Taille de l'en-tête HunkPalette
constexpr size_t kNumPaletteEntriesOffset = 10;      // Offset du nombre d'entrées
constexpr size_t kEntryHeaderSize = 22;              // Taille d'une entrée palette
constexpr size_t kEntryStartColorOffset = 10;        // Offset de la couleur de départ
constexpr size_t kEntryNumColorsOffset = 14;         // Offset du nombre de couleurs
constexpr size_t kEntryUsedOffset = 16;              // Offset du flag "utilisé"
constexpr size_t kEntrySharedUsedOffset = 17;        // Offset du flag "partagé"
constexpr size_t kEntryVersionOffset = 18;           // Offset de la version
constexpr size_t kRawPaletteSize = 1200;             // Taille palette brute (256 couleurs × RGB + alpha)

// ─────────────────────────────────────────────────────────────────────────────
// Fonctions de lecture pour les données de palette SCI
// ─────────────────────────────────────────────────────────────────────────────
// Ces fonctions permettent de lire des entiers depuis un buffer de palette
// avec vérification des limites pour éviter les accès hors-bornes.

/// Lit un octet non signé (uint8_t) depuis un span à un offset donné
uint8_t read_u8(std::span<const std::byte> data, size_t offset) {
  if (offset >= data.size()) {
    throw std::runtime_error("Palette SCI HunkPalette tronquée");
  }
  return std::to_integer<uint8_t>(data[offset]);
}

/// Lit un entier 16 bits non signé (little-endian) depuis un span
uint16_t read_u16(std::span<const std::byte> data, size_t offset) {
  if (offset + 1 >= data.size()) {
    throw std::runtime_error("Palette SCI HunkPalette tronquée");
  }
  const uint16_t lo = std::to_integer<uint8_t>(data[offset]);
  const uint16_t hi = std::to_integer<uint8_t>(data[offset + 1]);
  return static_cast<uint16_t>((hi << 8) | lo);
}

int16_t read_i16(std::span<const std::byte> data, size_t offset) {
  return static_cast<int16_t>(read_u16(data, offset));
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
/// Supprime les échantillons de la "runway" (piste d'approche) audio.
/// La runway contient les 4 premiers échantillons (8 octets) nécessaires
/// à l'initialisation du décodeur DPCM, mais qui ne font pas partie
/// de l'audio final à exporter.
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
    throw std::runtime_error("Unsupported Robot version: " + std::to_string(m_version));
  }

  if (m_xRes < 0 || m_yRes < 0 || m_xRes > m_options.max_x_res ||
      m_yRes > m_options.max_y_res) {
    throw std::runtime_error("Invalid resolution: " + std::to_string(m_xRes) + "x" + std::to_string(m_yRes));
  }
}

void RobotExtractor::parseHeaderFields(bool bigEndian) {
  m_bigEndian = bigEndian;
  m_fileOffset = 0;
  
  // Positionnement au début du fichier pour lecture de l'en-tête
  m_fp.seekg(0);
  
  // Vérification de la signature Robot (doit être 0x16)
  uint16_t signature = read_scalar<uint16_t>(m_fp, false);
  if (signature != 0x16) {
    throw std::runtime_error("Invalid robot file signature: expected 0x16, got 0x" + std::to_string(signature));
  }
  
  m_fp.seekg(4, std::ios::cur); // Sauter 'SOL\0'
  
  m_version = read_scalar<uint16_t>(m_fp, m_bigEndian);
  if (m_version < 4 || m_version > 6) {
    throw std::runtime_error("Unsupported Robot version: " + std::to_string(m_version));
  }

  m_audioBlkSize = read_scalar<uint16_t>(m_fp, m_bigEndian);
  if (m_audioBlkSize > kMaxAudioBlockSize) {
    throw std::runtime_error("Audio block size too large in header: " + std::to_string(m_audioBlkSize) + " (maximum " + std::to_string(kMaxAudioBlockSize) + ")");
  }
  m_primerZeroCompressFlag = read_scalar<int16_t>(m_fp, m_bigEndian);
  if (m_primerZeroCompressFlag != 0 && m_primerZeroCompressFlag != 1) {
    log_warn(m_srcPath,
             "Valeur primerZeroCompress non standard: " +
                 std::to_string(m_primerZeroCompressFlag) +
                 " (attendu 0 ou 1, mais accepté pour compatibilité ScummVM)",
             m_options);
  }
  m_fp.seekg(2, std::ios::cur);
  m_numFrames = read_scalar<uint16_t>(m_fp, m_bigEndian);
  if (m_numFrames == 0) {
    log_warn(m_srcPath, "Nombre de frames nul indiqué dans l'en-tête", m_options);
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
  
  m_hasPalette = read_scalar<uint8_t>(m_fp, m_bigEndian) != 0;
  m_hasAudio = read_scalar<uint8_t>(m_fp, m_bigEndian) != 0;
  
  if (m_hasAudio && m_audioBlkSize < kRobotAudioHeaderSize) {
    throw std::runtime_error("Audio block size too small in header: " + std::to_string(m_audioBlkSize) + " (minimum " + std::to_string(kRobotAudioHeaderSize) + ")");
  }
  m_fp.seekg(2, std::ios::cur);
  m_frameRate = read_scalar<int16_t>(m_fp, m_bigEndian);
  
  if (m_frameRate <= 0) {
    log_warn(m_srcPath,
             "Fréquence d'image invalide (" + std::to_string(m_frameRate) +
                 "), utilisation de 1 fps par défaut",
             m_options);
    m_frameRate = 1;
  }
  m_isHiRes = read_scalar<int16_t>(m_fp, m_bigEndian) != 0;
  m_maxSkippablePackets = read_scalar<int16_t>(m_fp, m_bigEndian);
  m_maxCelsPerFrame = read_scalar<int16_t>(m_fp, m_bigEndian);
  
  if (m_maxCelsPerFrame < 1) {
    log_warn(m_srcPath,
             "Nombre de cels par frame non positif: " +
                 std::to_string(m_maxCelsPerFrame),
             m_options);
  } else if (m_maxCelsPerFrame > 10) {
    log_warn(m_srcPath,
             "Nombre de cels par frame élevé: " +
                 std::to_string(m_maxCelsPerFrame),
             m_options);
  }
  
  m_fixedCelSizes.fill(0);
  m_reservedHeaderSpace.fill(0);
  
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
    return;
  }
  
  StreamExceptionGuard guard(m_fp);
  
  if (m_primerReservedSize != 0) {
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
    const std::uintmax_t primerHeaderPosMax =
        static_cast<std::uintmax_t>(primerHeaderPos);
    if (primerHeaderPosMax > fileSize ||
        reservedSize > fileSize - primerHeaderPosMax) {
      throw std::runtime_error("Primer hors limites");
    }
    
    const std::streamoff afterPrimerHeaderPos = m_fp.tellg();

    if (m_totalPrimerSize == 0) {
      m_evenPrimerSize = 0;
      m_oddPrimerSize = 0;
      m_evenPrimer.clear();
      m_oddPrimer.clear();
      
      const std::streamoff reservedEnd =
          primerHeaderPos + static_cast<std::streamoff>(m_primerReservedSize);
      if (reservedEnd > afterPrimerHeaderPos) {
        m_fp.seekg(reservedEnd, std::ios::beg);
      }
      m_postPrimerPos = m_fp.tellg();
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
      }
      
      if (primerSizesSum > reservedDataSize) {
        log_warn(m_srcPath,
                 "Tailles de primer dépassent l'espace réservé", m_options);
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
      } else {
        m_fp.seekg(afterPrimerDataPos, std::ios::beg);
      }
      m_postPrimerPos = m_fp.tellg();
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
  if (primer.empty()) {
    return;
  }

  StreamExceptionGuard guard(m_fp);

  const size_t headerSize = 10;
  if (primer.size() < headerSize) {
    throw std::runtime_error("Données de canal primer trop courtes");
  }

  const uint8_t zeroCompress = read_u8(primer, 0);
  const uint16_t compSize = read_u16(primer, 2);
  const uint16_t uncompSize = read_u16(primer, 4);
  const int16_t samplePredictor = read_i16(primer, 6);

  const size_t dataSize = primer.size() - headerSize;
  if (compSize > dataSize) {
    throw std::runtime_error("Taille compressée invalide dans le primer");
  }

  std::vector<std::byte> rawData;
  if (zeroCompress != 0) {
    std::vector<std::byte> compressed(
        primer.begin() + headerSize,
        primer.begin() + headerSize + compSize);
    rawData = zero_decompress(compressed, uncompSize);
  } else {
    rawData.assign(primer.begin() + headerSize,
                   primer.begin() + headerSize + compSize);
  }

  if (rawData.size() < kRobotRunwayBytes) {
    log_warn(m_srcPath,
             "Primer sans runway (taille=" + std::to_string(rawData.size()) + 
             "), canal " + std::string(isEven ? "pair" : "impair"),
             m_options);
    return;
  }

  int16_t predictor = samplePredictor;
  auto decompressed = dpcm16_decompress(
      std::span<const std::byte>(rawData.data(), rawData.size()), 
      predictor
  );

  if (decompressed.size() < kRobotRunwaySamples) {
    log_warn(m_srcPath, 
             "Primer décompressé trop court pour contenir le runway", 
             m_options);
    return;
  }

  std::vector<int16_t> samplesAfterRunway(
      decompressed.begin() + kRobotRunwaySamples,
      decompressed.end()
  );
  
  writeInterleaved(samplesAfterRunway, isEven);
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
    // Match ScummVM: always start with carry = 0 for each audio block
    // as stated in robot.h line 238-239: "using an initial sample value of 0"
    int16_t localPredictor = 0;
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

  const uint8_t blockFlags = read_u8(blockBytes, 0);
  const uint8_t blockUnk = read_u8(blockBytes, 1);
  const uint16_t audioPosition = read_u16(blockBytes, 2);
  const uint16_t audioLen = read_u16(blockBytes, 4);
  
  const bool isEvenChannel = (audioPosition % 2) == 0;
  const size_t targetIndex = audioPosition / 2;
  
  const ChannelAudio &sourceChannel = isEvenChannel ? m_evenChannelAudio : m_oddChannelAudio;
  ChannelAudio &targetChannel = isEvenChannel ? m_evenChannelAudio : m_oddChannelAudio;

  if (targetIndex < 0) {
    throw std::runtime_error("Position audio cible invalide");
  }

  if (targetIndex < static_cast<size_t>(sourceChannel.startHalfPos)) {
    log_warn(m_srcPath,
             "Bloc audio avant le début connu à la position " +
                 std::to_string(static_cast<long long>(targetIndex)),
             m_options);
    return;
  }

  if (targetIndex > static_cast<size_t>(sourceChannel.startHalfPos) &&
      !sourceChannel.seenNonPrimerBlock) {
    log_warn(m_srcPath,
             "Bloc audio ignoré avant le premier bloc non-primer à la position " +
                 std::to_string(static_cast<long long>(targetIndex)),
             m_options);
    return;
  }

  AppendPlan plan{};
  AppendPlanStatus status = prepareChannelAppend(
      targetChannel, isEvenChannel, targetIndex, sourceChannel.samples, plan);

  finalizeChannelAppend(targetChannel, isEvenChannel, targetIndex, sourceChannel.samples, plan,
                        status);
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
                               srcPath.string());
    }
    m_fp.seekg(m_paletteSize, std::ios::cur);
    return;
  }

  m_palette.resize(m_paletteSize);
  try {
    read_exact(m_fp, m_palette.data(), static_cast<size_t>(m_paletteSize));
  } catch (const std::runtime_error &) {
    throw std::runtime_error(std::string("Palette tronquée pour ") +
                             srcPath.string());
  }
}

void RobotExtractor::readSizesAndCues(bool allowShortFile) {
  StreamExceptionGuard guard(m_fp);
  
  m_frameSizes.resize(m_numFrames);
  m_packetSizes.resize(m_numFrames);
  
  switch (m_version) {
  case 4:
  case 5:
    for (size_t i = 0; i < m_numFrames; ++i) {
      m_frameSizes[i] = read_scalar<uint16_t>(m_fp, m_bigEndian);
    }
    for (size_t i = 0; i < m_numFrames; ++i) {
      m_packetSizes[i] = read_scalar<uint16_t>(m_fp, m_bigEndian);
    }
    break;
  case 6:
    for (size_t i = 0; i < m_numFrames; ++i) {
      int32_t val = read_scalar<int32_t>(m_fp, m_bigEndian);
      m_frameSizes[i] = static_cast<uint32_t>(val);
    }
    for (size_t i = 0; i < m_numFrames; ++i) {
      int32_t val = read_scalar<int32_t>(m_fp, m_bigEndian);
      m_packetSizes[i] = static_cast<uint32_t>(val);
    }
    break;
  default:
    throw std::runtime_error("Version non supportée: " + std::to_string(m_version));
  }
  
  m_cueTimes.resize(256);
  for (auto &cueTime : m_cueTimes) {
    cueTime = read_scalar<int32_t>(m_fp, m_bigEndian);
  }
  
  m_cueValues.resize(256);
  for (auto &cueValue : m_cueValues) {
    cueValue = read_scalar<uint16_t>(m_fp, m_bigEndian);
  }
  
  constexpr std::streamoff kRobotFrameSize = 2048;
  std::streamoff currentPos = m_fp.tellg();
  std::streamoff bytesRemaining = (currentPos - m_fileOffset) % kRobotFrameSize;
  if (bytesRemaining != 0) {
    std::streamoff padding = kRobotFrameSize - bytesRemaining;
    m_fp.seekg(padding, std::ios::cur);
  }
  
  m_recordPositions.clear();
  m_recordPositions.push_back(m_fp.tellg());
  
  for (size_t i = 0; i < m_numFrames - 1; ++i) {
    std::streamoff nextPos = m_recordPositions[i] + 
                             static_cast<std::streamoff>(m_packetSizes[i]);
    std::streamoff remainder = (nextPos - m_fileOffset) % kRobotFrameSize;
    if (remainder != 0) {
      nextPos += kRobotFrameSize - remainder;
    }
    m_recordPositions.push_back(nextPos);
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

    // Calculate sourceHeight as in ScummVM's expandCel
    const int sourceHeight = (static_cast<int>(h) * static_cast<int>(verticalScale)) / 100;
    if (sourceHeight <= 0) {
      throw std::runtime_error("Facteur d'échelle vertical invalide (sourceHeight <= 0)");
    }
    
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
        if (cel_offset + compSz > m_frameBuffer.size()) {
          throw std::runtime_error("Données de chunk insuffisantes");
        }
        m_celBuffer.insert(m_celBuffer.end(), comp.begin(), comp.begin() + compSz);
      } else {
        throw std::runtime_error("Type de compression inconnu pour le chunk");
      }
      cel_offset += compSz;
    }

    if (m_options.debug_index) {
      log_error(m_srcPath,
                "exportFrame: cel " + std::to_string(i) +
                    " décompressé, taille = " + std::to_string(m_celBuffer.size()),
                m_options);
    }

    // Apply vertical expansion if needed (like ScummVM's expandCel)
    std::vector<std::byte> expandedCelBuffer;
    std::span<const std::byte> pixelData;
    
    if (verticalScale != 100) {
      // Need to expand from sourceHeight to h
      const size_t expandedSize = static_cast<size_t>(w) * h;
      expandedCelBuffer.resize(expandedSize);
      expand_cel(std::span(expandedCelBuffer), 
                 std::span(m_celBuffer), 
                 w, h, verticalScale);
      pixelData = expandedCelBuffer;
    } else {
      // Direct use of decompressed data
      pixelData = m_celBuffer;
    }

    if (parsedPalette.valid && parsedPalette.colorCount > 0) {
      size_t pixelIndex = 0;
      for (size_t j = 0; j < pixelData.size(); ++j) {
        const uint8_t index = std::to_integer<uint8_t>(pixelData[j]);
        if (index < parsedPalette.entries.size() && parsedPalette.entries[index].present) {
          const auto &color = parsedPalette.entries[index];
          m_rgbaBuffer[pixelIndex++] = 0xFF000000 | (static_cast<uint32_t>(color.r) << 16) | (static_cast<uint32_t>(color.g) << 8) | color.b;
        } else {
          m_rgbaBuffer[pixelIndex++] = 0xFF000000;
        }
      }
      m_celBuffer.clear();
      m_celBuffer.shrink_to_fit();
      expandedCelBuffer.clear();
      expandedCelBuffer.shrink_to_fit();
      m_rgbaBuffer.resize(pixelIndex);
    } else {
      std::fill(m_rgbaBuffer.begin(), m_rgbaBuffer.end(), 0xFF000000);
    }

    if (m_options.debug_index) {
      log_error(m_srcPath,
                "exportFrame: écriture du cel " + std::to_string(i) +
                    " au format PNG, taille RGBA = " + std::to_string(m_rgbaBuffer.size()),
                m_options);
    }
    writePng(m_rgbaBuffer, w, h, x, y, verticalScale, m_dstDir, frameJson["cels"][i]);
  }

  return true;
}

void RobotExtractor::exportCel(std::span<const std::byte> celData,
                               const std::filesystem::path &outputPath,
                               const ParsedPalette &pal, size_t celIndex,
                               int frameNo) {
  const uint8_t verticalScaleFactor = read_u8(celData, 1);
  
  if (verticalScaleFactor == 0) {
    throw std::runtime_error("Facteur d'échelle vertical invalide (zéro)");
  }
  
  const uint16_t celWidth = read_u16(celData, 2, m_bigEndian);
  const uint16_t celHeight = read_u16(celData, 4, m_bigEndian);
  
  // Match ScummVM: use int for sourceHeight calculation to avoid overflow
  const int sourceHeight = 
      std::max(1, (static_cast<int>(celHeight) * static_cast<int>(verticalScaleFactor)) / 100);
  
  if (verticalScaleFactor != 100) {
    expand_cel(std::span(m_rgbaBuffer.data(), celWidth * celHeight * 4),
               std::span(decompressedData.data(), celWidth * sourceHeight * 4),
               celWidth, celHeight, verticalScaleFactor);
  } else {
    std::memcpy(m_rgbaBuffer.data(), decompressedData.data(), 
                celWidth * celHeight * 4);
  }
}
