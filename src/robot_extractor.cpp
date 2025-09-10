#include "robot_extractor.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <system_error>
#include <span>
#include <sstream>
#include <stdexcept>

#include "stb_image_write.h"
#include "utilities.hpp"

namespace robot {
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
}

void RobotExtractor::readHeader() {
  StreamExceptionGuard guard(m_fp);
  auto headerStart = m_fp.tellg();

  if (m_options.force_be) {
    m_bigEndian = true;
  } else if (m_options.force_le) {
    m_bigEndian = false;
  } else {
    uint16_t sigLE = read_scalar<uint16_t>(m_fp, false);
    if (sigLE == kRobotSig || sigLE == 0x3d) {
      m_bigEndian = false;
    } else {
      m_fp.seekg(headerStart);
      uint16_t sigBE = read_scalar<uint16_t>(m_fp, true);
      if (sigBE == kRobotSig || sigBE == 0x3d) {
        m_bigEndian = true;
      } else {
        throw std::runtime_error("Signature Robot invalide");
      }
    }
    m_fp.seekg(headerStart);
  }

  parseHeaderFields();

  auto version_invalid = [&]() { return m_version < 4 || m_version > 6; };

  if (version_invalid()) {
    m_bigEndian = !m_bigEndian;
    m_fp.seekg(headerStart);
    parseHeaderFields();

    if (version_invalid()) {
      throw std::runtime_error("Version Robot non supportée: " +
                               std::to_string(m_version));
    }
  }

  auto resolution_invalid = [&]() {
    return m_xRes < 0 || m_yRes < 0 || m_xRes > m_options.max_x_res ||
           m_yRes > m_options.max_y_res;
  };

  if (resolution_invalid()) {
    log_warn(m_srcPath, "Résolution suspecte, inversion endianness...",
             m_options);
    m_bigEndian = !m_bigEndian;
    m_fp.seekg(headerStart);
    parseHeaderFields();
    if (resolution_invalid()) {
      throw std::runtime_error(
          "Résolution invalide: " + std::to_string(m_xRes) + "x" +
          std::to_string(m_yRes));
    }
  }

  if (version_invalid()) {
    throw std::runtime_error("Version Robot non supportée: " +
                             std::to_string(m_version));
  }
}

void RobotExtractor::parseHeaderFields() {
  uint16_t sig = read_scalar<uint16_t>(m_fp, m_bigEndian);
  if (sig != kRobotSig && sig != 0x3d) {
    throw std::runtime_error("Signature Robot invalide");
  }
  std::array<char, 4> sol;
  m_fp.read(sol.data(), checked_streamsize(sol.size()));
  if (sol != std::array<char, 4>{'S', 'O', 'L', '\0'}) {
    throw std::runtime_error("Tag SOL invalide");
  }
  if (sig == 0x3d) {
    m_version = 4;
    m_fp.seekg(2, std::ios::cur);
  } else {
    m_version = read_scalar<uint16_t>(m_fp, m_bigEndian);
  }
  m_audioBlkSize = read_scalar<uint16_t>(m_fp, m_bigEndian);
  if (m_audioBlkSize > kMaxAudioBlockSize) {
    throw std::runtime_error("Taille de bloc audio excessive");
  }
  m_primerZeroCompressFlag = read_scalar<int16_t>(m_fp, m_bigEndian);
  m_fp.seekg(2, std::ios::cur);
  m_numFrames = read_scalar<uint16_t>(m_fp, m_bigEndian);
  if (m_numFrames == 0 || m_numFrames > kMaxFrames) {
    throw std::runtime_error("Nombre de frames invalide: " +
                             std::to_string(m_numFrames));
  }
  m_paletteSize = read_scalar<uint16_t>(m_fp, m_bigEndian);
  m_primerReservedSize = read_scalar<uint16_t>(m_fp, m_bigEndian);
  m_xRes = read_scalar<int16_t>(m_fp, m_bigEndian);
  m_yRes = read_scalar<int16_t>(m_fp, m_bigEndian);
  m_hasPalette = read_scalar<uint8_t>(m_fp, m_bigEndian) != 0;
  m_hasAudio = read_scalar<uint8_t>(m_fp, m_bigEndian) != 0;
  if (m_hasAudio && m_audioBlkSize < 8) {
    throw std::runtime_error("Taille de bloc audio invalide");
  }
  m_fp.seekg(2, std::ios::cur);
  m_frameRate = read_scalar<int16_t>(m_fp, m_bigEndian);
  if (m_frameRate <= 0 || m_frameRate > 120) {
    throw std::runtime_error("Fréquence d'image invalide: " +
                             std::to_string(m_frameRate));
  }
  m_isHiRes = read_scalar<int16_t>(m_fp, m_bigEndian) != 0;
  m_maxSkippablePackets = read_scalar<int16_t>(m_fp, m_bigEndian);
  m_maxCelsPerFrame = read_scalar<int16_t>(m_fp, m_bigEndian);
  if (m_version == 4) {
    m_maxCelsPerFrame = 1;
  }
  if (m_maxCelsPerFrame < 1 || m_maxCelsPerFrame > 10) {
    throw std::runtime_error("Nombre de cels par frame invalide: " +
                             std::to_string(m_maxCelsPerFrame));
  }
  for (auto &area : m_maxCelArea) {
    area = read_scalar<int32_t>(m_fp, m_bigEndian);
  }
  m_fp.seekg(8, std::ios::cur);
}

void RobotExtractor::readPrimer() {
  if (!m_hasAudio) {
    m_fp.seekg(m_primerReservedSize, std::ios::cur);
    return;
  }
  StreamExceptionGuard guard(m_fp);
  if (m_primerReservedSize != 0) {
    m_primerPosition = m_fp.tellg();
    m_totalPrimerSize = read_scalar<int32_t>(m_fp, m_bigEndian);
    int16_t compType = read_scalar<int16_t>(m_fp, m_bigEndian);
    m_evenPrimerSize = read_scalar<int32_t>(m_fp, m_bigEndian);
    m_oddPrimerSize = read_scalar<int32_t>(m_fp, m_bigEndian);

    if (m_totalPrimerSize < 0 ||
        m_totalPrimerSize > static_cast<int32_t>(m_primerReservedSize) ||
        m_evenPrimerSize < 0 || m_oddPrimerSize < 0 ||
        m_evenPrimerSize > m_totalPrimerSize ||
        m_oddPrimerSize > m_totalPrimerSize ||
        m_evenPrimerSize + m_oddPrimerSize > m_totalPrimerSize ||
        m_evenPrimerSize + m_oddPrimerSize >
            static_cast<int32_t>(m_primerReservedSize)) {
      throw std::runtime_error("Tailles de primer audio incohérentes");
    }
      
    if (compType != 0) {
      throw std::runtime_error("Type de compression inconnu: " +
                               std::to_string(compType));
    }

    if (m_evenPrimerSize + m_oddPrimerSize !=
        static_cast<std::streamsize>(m_primerReservedSize)) {
      m_fp.seekg(m_primerPosition + m_primerReservedSize, std::ios::beg);
      m_evenPrimerSize = 0;
      m_oddPrimerSize = 0;
      m_evenPrimer.clear();
      m_oddPrimer.clear();
      // Les données audio seront traitées sans primer.
    } else {
      m_evenPrimer.resize(static_cast<size_t>(m_evenPrimerSize));
      m_oddPrimer.resize(static_cast<size_t>(m_oddPrimerSize));
      if (m_evenPrimerSize > 0) {
        try {
          read_exact(m_fp, m_evenPrimer.data(),
                     static_cast<size_t>(m_evenPrimerSize));
        } catch (const std::runtime_error &) {
          throw std::runtime_error(
              std::string("Primer audio pair tronqué pour ") +
              m_srcPath.string());
        }
      }
      if (m_oddPrimerSize > 0) {
        try {
          read_exact(m_fp, m_oddPrimer.data(),
                     static_cast<size_t>(m_oddPrimerSize));
        } catch (const std::runtime_error &) {
          throw std::runtime_error(
              std::string("Primer audio impair tronqué pour ") +
              m_srcPath.string());
        }
      }
    }
  } else if (m_primerZeroCompressFlag) {
    m_evenPrimerSize = 19922;
    m_oddPrimerSize = 21024;
    m_evenPrimer.assign(static_cast<size_t>(m_evenPrimerSize), std::byte{0});
    m_oddPrimer.assign(static_cast<size_t>(m_oddPrimerSize), std::byte{0});
  } else {
    throw std::runtime_error("Flags de primer audio corrompus");
  }

  // Décompresser les buffers primer pour initialiser les prédicteurs audio
  constexpr size_t kRunwayBytes = 8;
  const size_t runwaySamples = kRunwayBytes * 2; // 8 bytes => 16 samples

  if (m_evenPrimerSize > 0) {
    if (m_extractAudio) {
      auto evenPcm =
          dpcm16_decompress(std::span(m_evenPrimer), m_audioPredictorEven);
      if (evenPcm.size() >= runwaySamples) {
        evenPcm.erase(evenPcm.begin(),
                      evenPcm.begin() +
                          static_cast<std::ptrdiff_t>(runwaySamples));
      } else {
        evenPcm.clear();
      }
      writeWav(evenPcm, 11025, m_evenAudioIndex++, true);
    } else {
      dpcm16_decompress_last(std::span(m_evenPrimer), m_audioPredictorEven);
    }
  }
  if (m_oddPrimerSize > 0) {
    if (m_extractAudio) {
      auto oddPcm =
          dpcm16_decompress(std::span(m_oddPrimer), m_audioPredictorOdd);
      if (oddPcm.size() >= runwaySamples) {
        oddPcm.erase(
            oddPcm.begin(),
            oddPcm.begin() + static_cast<std::ptrdiff_t>(runwaySamples));
      } else {
        oddPcm.clear();
      }
      writeWav(oddPcm, 11025, m_oddAudioIndex++, false);
    } else {
      dpcm16_decompress_last(std::span(m_oddPrimer), m_audioPredictorOdd);
    }
  }
  m_evenPrimer.clear();
  m_evenPrimer.shrink_to_fit();
  m_oddPrimer.clear();
  m_oddPrimer.shrink_to_fit();
  m_evenPrimerSize = 0;
  m_oddPrimerSize = 0;
}

void RobotExtractor::readPalette() {
  if (m_paletteSize % 3 != 0) {
    throw std::runtime_error(
        std::string(
            "Taille de palette non multiple de 3 dans l'en-tête pour ") +
        m_srcPath.string() + ": " + std::to_string(m_paletteSize) +
        " octets (maximum 1200)");
  }
  if (m_paletteSize > 1200) {
    throw std::runtime_error(
        std::string("Taille de palette excessive dans l'en-tête pour ") +
        m_srcPath.string() + ": " + std::to_string(m_paletteSize) +
        " octets (maximum 1200)");
  }

    if (!m_hasPalette) {
    m_fp.seekg(m_paletteSize, std::ios::cur);
    if (!m_fp) {
      throw std::runtime_error(std::string("Palette tronquée pour ") +
                               m_srcPath.string());
    }
    return;
  }
  if (m_paletteSize < 768) {
    throw std::runtime_error(
        std::string("Taille de palette insuffisante dans l'en-tête pour ") +
        m_srcPath.string() + ": " + std::to_string(m_paletteSize) +
        " octets (768 requis, multiple de 3, maximum 1200)");
  }

  StreamExceptionGuard guard(m_fp);
  m_palette.resize(m_paletteSize);
  try {
    read_exact(m_fp, m_palette.data(), static_cast<size_t>(m_paletteSize));
  } catch (const std::runtime_error &) {
    throw std::runtime_error(std::string("Palette tronquée pour ") +
                             m_srcPath.string());
  }
}

void RobotExtractor::readSizesAndCues() {
  StreamExceptionGuard guard(m_fp);
  m_frameSizes.resize(m_numFrames);
  m_packetSizes.resize(m_numFrames);
  for (auto &size : m_frameSizes) {
    uint32_t tmp;
    if (m_version >= 6) {
      int32_t tmpSigned = read_scalar<int32_t>(m_fp, m_bigEndian);
      if (tmpSigned < 0) {
        throw std::runtime_error("Taille de frame négative");
      }
      tmp = static_cast<uint32_t>(tmpSigned);
    } else {
      tmp = read_scalar<uint16_t>(m_fp, m_bigEndian);
    }
    if (tmp < 2) {
      throw std::runtime_error("Frame size too small");
    }
    if (tmp > kMaxFrameSize) {
      throw std::runtime_error("Taille de frame excessive");
    }
    size = tmp;
  }
  for (auto &size : m_packetSizes) {
    uint32_t tmp;
    if (m_version >= 6) {
      int32_t tmpSigned = read_scalar<int32_t>(m_fp, m_bigEndian);
      if (tmpSigned < 0) {
        throw std::runtime_error("Taille de paquet négative");
      }
      tmp = static_cast<uint32_t>(tmpSigned);
    } else {
      tmp = read_scalar<uint16_t>(m_fp, m_bigEndian);
    }
    size = tmp;
  }
  for (size_t i = 0; i < m_frameSizes.size(); ++i) {
    if (m_packetSizes[i] < m_frameSizes[i]) {
      throw std::runtime_error("Packet size < frame size");
    }
    uint32_t maxSize = m_frameSizes[i] +
                       (m_hasAudio ? static_cast<uint32_t>(m_audioBlkSize) : 0);
    if (m_packetSizes[i] > maxSize) {
      throw std::runtime_error("Packet size > frame size + audio block size");
    }
  }
  for (auto &time : m_cueTimes) {
    time = read_scalar<int32_t>(m_fp, m_bigEndian);
  }
  for (auto &value : m_cueValues) {
    value = read_scalar<int16_t>(m_fp, m_bigEndian);
  }
  std::streamoff pos = m_fp.tellg();
  std::streamoff bytesRemaining = pos % 2048;
  if (bytesRemaining != 0) {
    m_fp.seekg(2048 - bytesRemaining, std::ios::cur);
  }
}

bool RobotExtractor::exportFrame(int frameNo, nlohmann::json &frameJson) {
  StreamExceptionGuard guard(m_fp);
  if (m_frameSizes[frameNo] > kMaxFrameSize) {
    throw std::runtime_error("Taille de frame excessive");
  }
  m_frameBuffer.resize(m_frameSizes[frameNo]);
  read_exact(m_fp, m_frameBuffer.data(), m_frameBuffer.size());
  uint16_t numCels = read_scalar<uint16_t>(
      std::span(m_frameBuffer).subspan(0, 2), m_bigEndian);
  if (numCels > m_maxCelsPerFrame) {
    log_warn(m_srcPath,
             "Nombre de cels excessif dans la frame " + std::to_string(frameNo),
             m_options);
    return false;
  }

  frameJson["frame"] = frameNo;
  frameJson["cels"] = nlohmann::json::array();

  if (m_hasPalette) {
    // Palette size has been validated in readPalette(); assume it is correct
    // here.
    size_t offset = 2;
    for (int i = 0; i < numCels; ++i) {
      if (offset + 22 > m_frameBuffer.size()) {
        throw std::runtime_error("En-tête de cel invalide");
      }
      auto celHeader = std::span(m_frameBuffer).subspan(offset, 22);
      uint8_t verticalScale = static_cast<uint8_t>(celHeader[1]);
      uint16_t w = read_scalar<uint16_t>(celHeader.subspan(2, 2), m_bigEndian);
      uint16_t h = read_scalar<uint16_t>(celHeader.subspan(4, 2), m_bigEndian);
      int16_t x = read_scalar<int16_t>(celHeader.subspan(10, 2), m_bigEndian);
      int16_t y = read_scalar<int16_t>(celHeader.subspan(12, 2), m_bigEndian);
      uint16_t dataSize =
          read_scalar<uint16_t>(celHeader.subspan(14, 2), m_bigEndian);
      uint16_t numChunks =
          read_scalar<uint16_t>(celHeader.subspan(16, 2), m_bigEndian);
      offset += 22;

      size_t pixel_count = static_cast<size_t>(w) * h;
      if (w == 0 || h == 0 || pixel_count > kMaxCelPixels) {
        throw std::runtime_error("Dimensions de cel invalides");
      }
      if (verticalScale < 1) {
        throw std::runtime_error("Facteur d'échelle vertical invalide");
      }

      size_t sourceHeight = (static_cast<size_t>(h) * verticalScale) / 100;
      if (sourceHeight == 0) {
        throw std::runtime_error("Facteur d'échelle vertical invalide");
      }
      if (w != 0 && sourceHeight > SIZE_MAX / w) {
        throw std::runtime_error(
            "Débordement lors du calcul de la taille de cel");
      }
      size_t expected = static_cast<size_t>(w) * sourceHeight;
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
        std::vector<std::byte> decomp;
        if (compType == 0) {
          decomp = lzs_decompress(comp, decompSz);
        } else if (compType == 2) {
          if (compSz != decompSz) {
            throw std::runtime_error(
                "Données de cel malformées: taille de chunk incohérente");
          }
          decomp.assign(comp.begin(), comp.end());
        } else {
          throw std::runtime_error("Type de compression inconnu: " +
                                   std::to_string(compType));
        }
        m_celBuffer.insert(m_celBuffer.end(), decomp.begin(), decomp.end());
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

      uint16_t newH = h;
      if (verticalScale != 100) {
        std::vector<std::byte> expanded(static_cast<size_t>(w) * h);
        expand_cel(expanded, m_celBuffer, w, h, verticalScale);
        m_celBuffer = std::move(expanded);
      }

      // Taille d'une ligne en octets (largeur en pixels * 4 octets RGBA)
      size_t row_size = static_cast<size_t>(w) * 4;
      // Vérifie qu'on peut multiplier la hauteur par la taille d'une ligne
      // sans dépasser SIZE_MAX
      if (row_size != 0 && static_cast<size_t>(newH) > SIZE_MAX / row_size) {
        throw std::runtime_error(
            "Débordement lors du calcul de la taille du tampon");
      }
      size_t required = static_cast<size_t>(newH) * row_size;
      if (required > kMaxCelPixels * 4) {
        throw std::runtime_error("Tampon RGBA dépasse la limite");
      }
      if (required > m_rgbaBuffer.capacity()) {
        m_rgbaBuffer.reserve(required);
      }
      m_rgbaBuffer.resize(required);
      for (size_t pixel = 0; pixel < m_celBuffer.size(); ++pixel) {
        auto idx = static_cast<uint8_t>(m_celBuffer[pixel]);
        if (static_cast<size_t>(idx) >= m_palette.size() / 3) {
          throw std::runtime_error("Indice de palette hors limites: " +
                                   std::to_string(idx));
        }
        m_rgbaBuffer[pixel * 4 + 0] = m_palette[idx * 3 + 0];
        m_rgbaBuffer[pixel * 4 + 1] = m_palette[idx * 3 + 1];
        m_rgbaBuffer[pixel * 4 + 2] = m_palette[idx * 3 + 2];
        m_rgbaBuffer[pixel * 4 + 3] = std::byte{255};
      }

      std::ostringstream oss;
      oss << std::setw(5) << std::setfill('0') << frameNo << "_" << i << ".png";
      auto outPath = m_dstDir / oss.str();
      std::string outPathStr;
#ifdef _WIN32
      auto longPath = make_long_path(outPath.wstring());
      auto pathUtf8 = std::filesystem::path{longPath}.u8string();
      outPathStr.assign(pathUtf8.begin(), pathUtf8.end());
      if (!stbi_write_png(reinterpret_cast<const char *>(pathUtf8.c_str()), w,
                          newH, 4, m_rgbaBuffer.data(), w * 4)) {
#else
      outPathStr = outPath.string();
      if (!stbi_write_png(outPathStr.c_str(), w, newH, 4, m_rgbaBuffer.data(),
                          w * 4)) {
#endif
        throw std::runtime_error(std::string("Échec de l'écriture de ") +
                                 outPathStr);
      }
        
      nlohmann::json celJson;
      celJson["index"] = i;
      celJson["x"] = x;
      celJson["y"] = y;
      celJson["width"] = w;
      celJson["height"] = newH;
      celJson["vertical_scale"] = verticalScale;
      frameJson["cels"].push_back(celJson);
      offset = cel_offset;
    }
    auto remaining = static_cast<std::ptrdiff_t>(m_frameBuffer.size()) -
                     static_cast<std::ptrdiff_t>(offset);
    if (remaining != 0) {
      throw std::runtime_error(std::to_string(remaining) +
                               " octets non traités dans la frame");
    }
  } else {
    log_warn(m_srcPath,
             "Palette manquante, cels ignorés pour la frame " +
                 std::to_string(frameNo),
             m_options);
  }

  if (m_hasAudio) {
    if (m_packetSizes[frameNo] > m_frameSizes[frameNo]) {
      uint32_t audioBlkLen = m_packetSizes[frameNo] - m_frameSizes[frameNo];
      if (audioBlkLen < 8) {
        m_fp.seekg(static_cast<std::streamoff>(audioBlkLen), std::ios::cur);
      } else {
        int32_t pos = read_scalar<int32_t>(m_fp, m_bigEndian);
        if (pos < 0) {
          throw std::runtime_error("Position audio invalide");
        }
        if (pos == 0) {
          m_fp.seekg(static_cast<std::streamoff>(audioBlkLen - 4),
                     std::ios::cur);
        } else {
          int32_t size = read_scalar<int32_t>(m_fp, m_bigEndian);
          // "size" représente le nombre d'octets suivant ce champ, incluant
          // les 8 octets de "runway" mais excluant l'en-tête "pos" + "size".
          if (size < 8) {
            throw std::runtime_error("Taille audio invalide");
          }
          int64_t maxSize = static_cast<int64_t>(audioBlkLen) - 8;
          if (size <= maxSize) {
            std::vector<std::byte> audio(static_cast<size_t>(size - 8));
            std::array<std::byte, 8> runway{};
            m_fp.read(reinterpret_cast<char *>(runway.data()),
                      checked_streamsize(runway.size()));
            m_fp.read(reinterpret_cast<char *>(audio.data()),
                      checked_streamsize(audio.size()));
            bool isEven = (pos % 2) == 0;
            // L'audio peut exister même sans primer, décompresser toujours.
            if (isEven) {
              // Décompresser le "runway" pour mettre à jour le prédicteur,
              // puis ignorer les échantillons produits.
              [[maybe_unused]] auto runwaySamples =
                  dpcm16_decompress(std::span(runway), m_audioPredictorEven);
              auto samples =
                  dpcm16_decompress(std::span(audio), m_audioPredictorEven);
              if (m_extractAudio) {
                writeWav(samples, 11025, m_evenAudioIndex++, true);
              }
            } else {
              // Même logique pour le canal impair.
              [[maybe_unused]] auto runwaySamples =
                  dpcm16_decompress(std::span(runway), m_audioPredictorOdd);
              auto samples =
                  dpcm16_decompress(std::span(audio), m_audioPredictorOdd);
              if (m_extractAudio) {
                writeWav(samples, 11025, m_oddAudioIndex++, false);
              }
            }
            int64_t toSkip = maxSize - size;
            if (toSkip < 0) {
              throw std::runtime_error("Taille audio incohérente");
            }
            m_fp.seekg(static_cast<std::streamoff>(toSkip), std::ios::cur);
          } else {
            // Invalid header, skip the rest of the audio block safely
            m_fp.seekg(static_cast<std::streamoff>(maxSize), std::ios::cur);
          }
        }
      }
    }
  }
  return true;
}

void RobotExtractor::writeWav(const std::vector<int16_t> &samples,
                              uint32_t sampleRate, int blockIndex,
                              bool isEvenChannel) {
  if (sampleRate == 0)
    sampleRate = 11025;
  if (samples.size() > std::numeric_limits<size_t>::max() / sizeof(int16_t)) {
    throw std::runtime_error("Nombre d'échantillons audio dépasse la limite, "
                             "fichier WAV corrompu potentiel");
  }
  size_t data_size = samples.size() * sizeof(int16_t);
  if (data_size > 0xFFFFFFFFu - 36) {
    throw std::runtime_error(
        "Taille de données audio trop grande pour un fichier WAV: " +
        std::to_string(data_size));
  }
  if (sampleRate > std::numeric_limits<uint32_t>::max() / 2) {
    throw std::runtime_error("Fréquence d'échantillonnage trop élevée: " +
                             std::to_string(sampleRate));
  }
  uint32_t riff_size = 36 + static_cast<uint32_t>(data_size);
  uint32_t byte_rate = sampleRate * 2;

  std::array<char, 44> header{};
  auto write_le16 = [](char *dst, uint16_t v) {
    dst[0] = static_cast<char>(v & 0xFF);
    dst[1] = static_cast<char>((v >> 8) & 0xFF);
  };
  auto write_le32 = [](char *dst, uint32_t v) {
    dst[0] = static_cast<char>(v & 0xFF);
    dst[1] = static_cast<char>((v >> 8) & 0xFF);
    dst[2] = static_cast<char>((v >> 16) & 0xFF);
    dst[3] = static_cast<char>((v >> 24) & 0xFF);
  };

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
  write_le32(header.data() + 16, 16);    // fmt chunk size
  write_le16(header.data() + 20, 1);      // PCM
  write_le16(header.data() + 22, 1);      // Mono
  write_le32(header.data() + 24, sampleRate);
  write_le32(header.data() + 28, byte_rate);
  write_le16(header.data() + 32, 2);      // Block align
  write_le16(header.data() + 34, 16);     // Bits per sample
  header[36] = 'd';
  header[37] = 'a';
  header[38] = 't';
  header[39] = 'a';
  write_le32(header.data() + 40, static_cast<uint32_t>(data_size));
  std::ostringstream wavName;
  wavName << "frame_" << std::setw(5) << std::setfill('0') << blockIndex
          << (isEvenChannel ? "_even" : "_odd") << ".wav";
  auto outPath = m_dstDir / wavName.str();
  std::string outPathStr;
#ifdef _WIN32
  auto longPath = make_long_path(outPath.wstring());
  auto pathUtf8 = std::filesystem::path{longPath}.u8string();
  outPathStr.assign(pathUtf8.begin(), pathUtf8.end());
  std::ofstream wavFile(std::filesystem::path{longPath}, std::ios::binary);
#else
  outPathStr = outPath.string();
  std::ofstream wavFile(outPath, std::ios::binary);
#endif
  if (!wavFile) {
    throw std::runtime_error("Échec de l'ouverture du fichier WAV: " +
                             outPathStr);
  }
  wavFile.write(header.data(), checked_streamsize(header.size()));
  wavFile.write(reinterpret_cast<const char *>(samples.data()),
                checked_streamsize(data_size));
  wavFile.flush();
  if (!wavFile) {
    throw std::runtime_error("Échec de l'écriture du fichier WAV: " +
                             outPathStr);
  }
  wavFile.close();
  if (wavFile.fail()) {
    throw std::runtime_error("Échec de la fermeture du fichier WAV: " +
                             outPathStr);
  }
}

void RobotExtractor::extract() {
  readHeader();
  readPrimer();
  readPalette();
  readSizesAndCues();
  nlohmann::json jsonDoc;
  jsonDoc["version"] = "1.0.0";
  jsonDoc["frames"] = nlohmann::json::array();
  for (int i = 0; i < m_numFrames; ++i) {
    auto pos = m_fp.tellg();
    nlohmann::json frameJson;
    if (exportFrame(i, frameJson)) {
      jsonDoc["frames"].push_back(frameJson);
    }
    m_fp.seekg(pos + static_cast<std::streamoff>(m_packetSizes[i]),
               std::ios::beg);
  }

  auto tmpPath = m_dstDir / "metadata.json.tmp";
  std::string tmpPathStr = tmpPath.string();
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
    std::filesystem::copy_file(tmpPath, finalPath,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (ec) {
      throw std::runtime_error("Échec de la copie de " + tmpPathStr + " vers " +
                               finalPath.string() + ": " + ec.message());
    }
    std::filesystem::remove(tmpPath, ec);
    if (ec) {
      throw std::runtime_error("Échec de la suppression du fichier temporaire " +
                               tmpPathStr + ": " + ec.message());
    }
  }
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
              << " [--audio] [--quiet] [--force-be | --force-le] <input.rbt> "
                 "<output_dir>\n";
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
