#include "robot_extractor.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <filesystem>
#include <span>
#include <limits>

#include "utilities.hpp"
#include "stb_image_write.h"

namespace robot {
RobotExtractor::RobotExtractor(const std::filesystem::path &srcPath, const std::filesystem::path &dstDir, bool extractAudio)
    : m_srcPath(srcPath), m_dstDir(dstDir), m_extractAudio(extractAudio) {
    m_fp.open(srcPath, std::ios::binary);
    if (!m_fp.is_open()) {
        throw std::runtime_error(std::string("Impossible d'ouvrir ") +
                                 srcPath.string());
    }
}

void RobotExtractor::readHeader() {
    StreamExceptionGuard guard(m_fp);
    auto headerStart = m_fp.tellg();

    if (g_force_be) {
        m_bigEndian = true;
    } else if (g_force_le) {
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

    auto version_invalid = [&]() {
        return m_version < 4 || m_version > 6;
    };

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
        return m_xRes < 0 || m_yRes < 0 || m_xRes > 7680 || m_yRes > 4320;
    };

    if (resolution_invalid()) {
        log_warn(m_srcPath, "Résolution suspecte, inversion endianness...");
        m_bigEndian = !m_bigEndian;
        m_fp.seekg(headerStart);
        parseHeaderFields();
        if (resolution_invalid()) {
            throw std::runtime_error("Résolution invalide: " +
                                     std::to_string(m_xRes) + "x" +
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
    m_fp.read(sol.data(), sol.size());
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
    if (m_audioBlkSize >= kMaxAudioBlockSize) {
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
            throw std::runtime_error(
                "Type de compression inconnu: " + std::to_string(compType));
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
                    read_exact(m_fp, m_evenPrimer.data(), static_cast<size_t>(m_evenPrimerSize));
                } catch (const std::runtime_error &) {
                    throw std::runtime_error(std::string("Primer audio pair tronqué pour ") +
                                            m_srcPath.string());
                }
            }
            if (m_oddPrimerSize > 0) {
                try {
                    read_exact(m_fp, m_oddPrimer.data(), static_cast<size_t>(m_oddPrimerSize));
                } catch (const std::runtime_error &) {
                    throw std::runtime_error(std::string("Primer audio impair tronqué pour ") +
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
    if (m_evenPrimerSize > 0) {
        auto evenPcm = dpcm16_decompress(std::span(m_evenPrimer), m_audioPredictorEven);
        if (m_extractAudio) {
            writeWav(evenPcm, 11025, m_evenAudioIndex++, true);
        }
    }
    if (m_oddPrimerSize > 0) {
        auto oddPcm = dpcm16_decompress(std::span(m_oddPrimer), m_audioPredictorOdd);
        if (m_extractAudio) {
            writeWav(oddPcm, 11025, m_oddAudioIndex++, false);
        }
    }
}

void RobotExtractor::readPalette() {
    if (!m_hasPalette) {
        m_fp.seekg(m_paletteSize, std::ios::cur);
        return;
    }
    StreamExceptionGuard guard(m_fp);
    if (m_paletteSize < 768) {
        throw std::runtime_error(std::string("Taille de palette insuffisante dans l'en-tête pour ") +
                                 m_srcPath.string() + ": " +
                                 std::to_string(m_paletteSize) +
                                 " octets (768 requis, multiple de 3, maximum 1200)");
    }
    if (m_paletteSize % 3 != 0) {
        throw std::runtime_error(std::string("Taille de palette non multiple de 3 dans l'en-tête pour ") +
                                 m_srcPath.string() + ": " +
                                 std::to_string(m_paletteSize) +
                                 " octets (maximum 1200)");
    }
    if (m_paletteSize > 1200) {
        throw std::runtime_error(std::string("Taille de palette excessive dans l'en-tête pour ") +
                                 m_srcPath.string() + ": " +
                                 std::to_string(m_paletteSize) +
                                 " octets (maximum 1200)");
    }
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
    uint16_t numCels = read_scalar<uint16_t>(std::span(m_frameBuffer).subspan(0, 2), m_bigEndian);
    if (numCels > m_maxCelsPerFrame) {
        log_warn(m_srcPath, "Nombre de cels excessif dans la frame " + std::to_string(frameNo));
        return false;
    }

    frameJson["frame"] = frameNo;
    frameJson["cels"] = nlohmann::json::array();

    if (m_hasPalette) {
        // Palette size has been validated in readPalette(); assume it is correct here.
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
        uint16_t dataSize = read_scalar<uint16_t>(celHeader.subspan(14, 2), m_bigEndian);
        uint16_t numChunks = read_scalar<uint16_t>(celHeader.subspan(16, 2), m_bigEndian);
        offset += 22;

        size_t pixel_count = static_cast<size_t>(w) * h;
        if (w == 0 || h == 0 || pixel_count > kMaxCelPixels) {
            throw std::runtime_error("Dimensions de cel invalides");
        }
        if (verticalScale < 1 || verticalScale > 100) {
            throw std::runtime_error("Facteur d'échelle vertical invalide");
        }
     
        size_t expected = static_cast<size_t>(w) *
                          ((static_cast<size_t>(h) * verticalScale) / 100);
        m_celBuffer.clear();
        m_celBuffer.reserve(expected);
        size_t cel_offset = offset;
        for (int j = 0; j < numChunks; ++j) {
            if (cel_offset + 10 > m_frameBuffer.size()) {
                throw std::runtime_error("En-tête de chunk invalide");
            }
            auto chunkHeader = std::span(m_frameBuffer).subspan(cel_offset, 10);
            uint32_t compSz = read_scalar<uint32_t>(chunkHeader.subspan(0, 4), m_bigEndian);
            uint32_t decompSz = read_scalar<uint32_t>(chunkHeader.subspan(4, 4), m_bigEndian);
            uint16_t compType = read_scalar<uint16_t>(chunkHeader.subspan(8, 2), m_bigEndian);
            cel_offset += 10;

            size_t remaining_expected = expected - m_celBuffer.size();
            if (decompSz > remaining_expected) {
                log_error(m_srcPath,
                          "Taille de chunk décompressé excède l'espace restant pour le cel " +
                              std::to_string(i) + " dans la frame " +
                              std::to_string(frameNo));
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
                throw std::runtime_error("Type de compression inconnu: " + std::to_string(compType));
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
            throw std::runtime_error(
                "Cel corrompu: taille de données incohérente");
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
        if (required > m_rgbaBuffer.capacity()) {
            size_t growth = required / 2;
            size_t max = std::numeric_limits<size_t>::max();
            size_t new_capacity;
            if (required > max - growth) {
                new_capacity = max;
            } else {
                new_capacity = required + growth;
            }
            m_rgbaBuffer.reserve(new_capacity);
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
        // Conversion du chemin large en UTF-8 via std::filesystem::path::u8string
        auto u8Path = std::filesystem::path{longPath}.u8string();
        std::string pathUtf8(u8Path.begin(), u8Path.end());
        outPathStr = pathUtf8;
        if (!stbi_write_png(pathUtf8.c_str(), w, newH, 4, m_rgbaBuffer.data(), w * 4)) {
#else
        outPathStr = outPath.string();
        if (!stbi_write_png(outPathStr.c_str(), w, newH, 4, m_rgbaBuffer.data(), w * 4)) {
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
        log_warn(m_srcPath, "Palette manquante, cels ignorés pour la frame " +
                                std::to_string(frameNo));
    }
        
    if (m_hasAudio) {
        if (m_packetSizes[frameNo] > m_frameSizes[frameNo]) {
            uint32_t audioBlkLen = m_packetSizes[frameNo] - m_frameSizes[frameNo];
            if (audioBlkLen < 8) {
                m_fp.seekg(static_cast<std::streamoff>(audioBlkLen), std::ios::cur);
            } else {
                int32_t pos = read_scalar<int32_t>(m_fp, m_bigEndian);
                if (pos <= 0) {
                    throw std::runtime_error("Position audio invalide");
                }
                int32_t size = read_scalar<int32_t>(m_fp, m_bigEndian);
                if (size < 8 || size < 0) {
                    throw std::runtime_error("Taille audio invalide");
                }
                uint32_t maxSize = audioBlkLen - 8;
                if (size <= static_cast<int32_t>(maxSize)) {
                    std::vector<std::byte> audio(static_cast<size_t>(size - 8));
                    std::array<std::byte, 8> runway{};
                    m_fp.read(reinterpret_cast<char *>(runway.data()), runway.size());
                    m_fp.read(reinterpret_cast<char *>(audio.data()),
                              static_cast<std::streamsize>(audio.size()));
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
                    m_fp.seekg(static_cast<std::streamoff>(maxSize - static_cast<uint32_t>(size)),
                               std::ios::cur);
                } else {
                    // Invalid header, skip the rest of the audio block safely
                    m_fp.seekg(static_cast<std::streamoff>(maxSize), std::ios::cur);
                }
            }
        }
    }

    return true;   
}

void RobotExtractor::writeWav(const std::vector<int16_t> &samples, uint32_t sampleRate, int blockIndex, bool isEvenChannel) {
    if (sampleRate == 0) sampleRate = 11025;
    std::vector<std::byte> wav;
    size_t data_size = samples.size() * sizeof(int16_t);
        if (data_size > 0xFFFFFFFFu - 36) {
        throw std::runtime_error(
            "Taille de données audio trop grande pour un fichier WAV: " +
            std::to_string(data_size));
    }
    wav.reserve(44 + data_size);
    wav.insert(wav.end(), {std::byte{'R'}, std::byte{'I'}, std::byte{'F'}, std::byte{'F'}});
    uint32_t riff_size = 36 + static_cast<uint32_t>(data_size);
    append_le32(wav, riff_size);
    wav.insert(wav.end(), {std::byte{'W'}, std::byte{'A'}, std::byte{'V'}, std::byte{'E'}});
    wav.insert(wav.end(), {std::byte{'f'}, std::byte{'m'}, std::byte{'t'}, std::byte{' '}});
    uint32_t fmt_size = 16;
    append_le32(wav, fmt_size);
    append_le16(wav, 1); // PCM
    append_le16(wav, 1); // Mono
    append_le32(wav, sampleRate);
    uint32_t byte_rate = sampleRate * 2;
    append_le32(wav, byte_rate);
    append_le16(wav, 2); // Block align
    append_le16(wav, 16); // Bits per sample
    wav.insert(wav.end(), {std::byte{'d'}, std::byte{'a'}, std::byte{'t'}, std::byte{'a'}});
    append_le32(wav, static_cast<uint32_t>(data_size));
    for (const auto &sample : samples) {
        append_le16(wav, static_cast<uint16_t>(sample));
    }
    std::ostringstream wavName;
    wavName << (isEvenChannel ? "even_" : "odd_") << std::setw(5) << std::setfill('0') << blockIndex << ".wav";
    std::ofstream wavFile(m_dstDir / wavName.str(), std::ios::binary);
    if (!wavFile) {
        throw std::runtime_error("Échec de l'ouverture du fichier WAV: " + wavName.str());
    }
    wavFile.write(reinterpret_cast<const char *>(wav.data()), wav.size());
    wavFile.flush();
    if (!wavFile) {
        throw std::runtime_error("Échec de l'écriture du fichier WAV: " + wavName.str());
    }
    wavFile.close();
    if (wavFile.fail()) {
        throw std::runtime_error("Échec de la fermeture du fichier WAV: " + wavName.str());
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
            throw std::runtime_error(std::string("Échec de l'ouverture du fichier JSON temporaire: ") +
                                     tmpPathStr);
        }
        jsonFile << std::setw(2) << jsonDoc;
        jsonFile.flush();
        if (!jsonFile) {
            throw std::runtime_error(std::string("Échec de l'écriture du fichier JSON temporaire: ") +
                                     tmpPathStr);
        }
        jsonFile.close();
        if (jsonFile.fail()) {
            throw std::runtime_error(std::string("Échec de la fermeture du fichier JSON temporaire: ") +
                                     tmpPathStr);
        }
    }
    std::filesystem::rename(tmpPath, m_dstDir / "metadata.json");
}

} // namespace robot

#ifndef ROBOT_EXTRACTOR_NO_MAIN
int main(int argc, char *argv[]) {
    bool extractAudio = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        bool known = false;
        if (arg == "--audio") {
            extractAudio = true;
            known = true;
        } else if (arg == "--quiet") {
            robot::g_quiet = true;
            known = true;
        } else if (arg == "--force-be") {
            robot::g_force_be = true;
            known = true;
        } else if (arg == "--force-le") {
            robot::g_force_le = true;
            known = true;
        }
        if (!known) files.push_back(arg);
    }
        if (robot::g_force_be && robot::g_force_le) {
        std::cerr << "Les options --force-be et --force-le sont mutuellement exclusives\n";
        return 1;
    }
    if (files.size() != 2) {
        std::cerr << "Usage: " << argv[0]
                  << " [--audio] [--quiet] [--force-be | --force-le] <input.rbt> <output_dir>\n";
        return 1;
    }
    try {
        std::filesystem::create_directories(files[1]);
        robot::RobotExtractor extractor(files[0], files[1], extractAudio);
        extractor.extract();
    } catch (const std::exception &e) {
        robot::log_error(files[0], e.what());
        return 1;
    }
    return 0;
}
#endif
