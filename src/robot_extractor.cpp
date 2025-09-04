#include "robot_extractor.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <filesystem>
#include <span>

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

    uint16_t sig = read_scalar<uint16_t>(m_fp, m_bigEndian);
    if (sig != kRobotSig && sig != 0x3d) {
        throw std::runtime_error("Signature Robot invalide");
    }
    m_fp.seekg(2, std::ios::cur);
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
    m_primerZeroCompressFlag = read_scalar<int16_t>(m_fp, m_bigEndian);
    m_fp.seekg(2, std::ios::cur);
    m_numFrames = read_scalar<uint16_t>(m_fp, m_bigEndian);
    m_paletteSize = read_scalar<uint16_t>(m_fp, m_bigEndian);
    m_primerReservedSize = read_scalar<uint16_t>(m_fp, m_bigEndian);
    m_xRes = read_scalar<int16_t>(m_fp, m_bigEndian);
    m_yRes = read_scalar<int16_t>(m_fp, m_bigEndian);
    m_hasPalette = read_scalar<uint8_t>(m_fp, m_bigEndian) != 0;
    m_hasAudio = read_scalar<uint8_t>(m_fp, m_bigEndian) != 0;
    m_fp.seekg(2, std::ios::cur);
    m_frameRate = read_scalar<int16_t>(m_fp, m_bigEndian);
    m_isHiRes = read_scalar<int16_t>(m_fp, m_bigEndian) != 0;
    m_maxSkippablePackets = read_scalar<int16_t>(m_fp, m_bigEndian);
    m_maxCelsPerFrame = read_scalar<int16_t>(m_fp, m_bigEndian);
    if (m_version == 4) {
        m_maxCelsPerFrame = 1;
    }
    for (auto &area : m_maxCelArea) {
        area = read_scalar<int32_t>(m_fp, m_bigEndian);
    }
    m_fp.seekg(8, std::ios::cur);

    if (m_xRes > 7680 || m_yRes > 4320) {
        log_warn(m_srcPath, "Résolution suspecte, inversion endianness...");
        m_bigEndian = !m_bigEndian;
        m_fp.seekg(16, std::ios::beg);
        m_xRes = read_scalar<int16_t>(m_fp, m_bigEndian);
        m_yRes = read_scalar<int16_t>(m_fp, m_bigEndian);
    }

    if (m_version < 4 || m_version > 6) {
        throw std::runtime_error("Version Robot non supportée: " + std::to_string(m_version));
    }
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
        if (compType != 0) {
            throw std::runtime_error("Type de compression inconnu: " + std::to_string(compType));
        }
        if (m_evenPrimerSize + m_oddPrimerSize != static_cast<std::streamsize>(m_primerReservedSize)) {
            m_fp.seekg(m_primerPosition + m_primerReservedSize, std::ios::beg);
        } else {
            m_evenPrimer.resize(static_cast<size_t>(m_evenPrimerSize));
            m_oddPrimer.resize(static_cast<size_t>(m_oddPrimerSize));
            if (m_evenPrimerSize > 0) {
                try {
                    m_fp.read(reinterpret_cast<char *>(m_evenPrimer.data()), m_evenPrimerSize);
                } catch (const std::ios_base::failure &) {
                    throw std::runtime_error(std::string("Primer audio pair tronqué pour ") +
                                             m_srcPath.string() + ": lu " +
                                             std::to_string(m_fp.gcount()) + "/" +
                                             std::to_string(m_evenPrimerSize) +
                                             " octets");
                }
                if (m_fp.gcount() < m_evenPrimerSize) {
                    throw std::runtime_error(std::string("Primer audio pair tronqué pour ") +
                                             m_srcPath.string() + ": lu " +
                                             std::to_string(m_fp.gcount()) + "/" +
                                             std::to_string(m_evenPrimerSize) +
                                             " octets");
                }
            }
            if (m_oddPrimerSize > 0) {
                try {
                    m_fp.read(reinterpret_cast<char *>(m_oddPrimer.data()), m_oddPrimerSize);
                } catch (const std::ios_base::failure &) {
                    throw std::runtime_error(std::string("Primer audio impair tronqué pour ") +
                                             m_srcPath.string() + ": lu " +
                                             std::to_string(m_fp.gcount()) + "/" +
                                             std::to_string(m_oddPrimerSize) +
                                             " octets");
                }
                if (m_fp.gcount() < m_oddPrimerSize) {
                    throw std::runtime_error(std::string("Primer audio impair tronqué pour ") +
                                             m_srcPath.string() + ": lu " +
                                             std::to_string(m_fp.gcount()) + "/" +
                                             std::to_string(m_oddPrimerSize) +
                                             " octets");
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
                                 " octets (768 requis)");
    }
    m_palette.resize(m_paletteSize);
    m_fp.read(reinterpret_cast<char *>(m_palette.data()), m_paletteSize);
    if (static_cast<size_t>(m_fp.gcount()) < m_paletteSize) {
        throw std::runtime_error(std::string("Palette tronquée pour ") +
                                 m_srcPath.string() + ": lue " +
                                 std::to_string(m_fp.gcount()) + "/" +
                                 std::to_string(m_paletteSize) +
                                 " octets");
    }
}

void RobotExtractor::readSizesAndCues() {
    StreamExceptionGuard guard(m_fp);
    m_frameSizes.resize(m_numFrames);
    m_packetSizes.resize(m_numFrames);
    for (auto &size : m_frameSizes) {
        size = m_version == 5 ? read_scalar<uint16_t>(m_fp, m_bigEndian) : read_scalar<int32_t>(m_fp, m_bigEndian);
    }
    for (auto &size : m_packetSizes) {
        size = m_version == 5 ? read_scalar<uint16_t>(m_fp, m_bigEndian) : read_scalar<int32_t>(m_fp, m_bigEndian);
    }
    for (auto &time : m_cueTimes) {
        time = read_scalar<int32_t>(m_fp, m_bigEndian);
    }
    for (auto &value : m_cueValues) {
        value = read_scalar<int16_t>(m_fp, m_bigEndian);
    }
    auto pos = m_fp.tellg();
    std::streamoff bytesRemaining = pos % 2048;
    if (bytesRemaining != 0) {
        m_fp.seekg(2048 - bytesRemaining, std::ios::cur);
    }
}

void RobotExtractor::exportFrame(int frameNo, nlohmann::json &frameJson) {
    StreamExceptionGuard guard(m_fp);
    std::vector<std::byte> frameData(m_frameSizes[frameNo]);
    m_fp.read(reinterpret_cast<char *>(frameData.data()), frameData.size());
    uint16_t numCels = read_scalar<uint16_t>(std::span(frameData).subspan(0, 2), m_bigEndian);
    if (numCels > m_maxCelsPerFrame) {
        log_warn(m_srcPath, "Nombre de cels excessif dans la frame " + std::to_string(frameNo));
        return;
    }

    if (m_palette.size() < 768) {
        throw std::runtime_error(std::string("Taille de palette insuffisante pour ") +
                                 m_srcPath.string() + ": " +
                                 std::to_string(m_palette.size()) +
                                 " octets (768 requis)");
    }

    frameJson["frame"] = frameNo;
    frameJson["cels"] = nlohmann::json::array();
    size_t offset = 2;

    std::vector<std::byte> rgba_buffer;
    for (int i = 0; i < numCels; ++i) {
        if (offset + 22 > frameData.size()) {
            throw std::runtime_error("En-tête de cel invalide");
        }
        auto celHeader = std::span(frameData).subspan(offset, 22);
        uint8_t verticalScale = static_cast<uint8_t>(celHeader[1]);
        uint16_t w = read_scalar<uint16_t>(celHeader.subspan(2, 2), m_bigEndian);
        uint16_t h = read_scalar<uint16_t>(celHeader.subspan(4, 2), m_bigEndian);
        int16_t x = read_scalar<int16_t>(celHeader.subspan(10, 2), m_bigEndian);
        int16_t y = read_scalar<int16_t>(celHeader.subspan(12, 2), m_bigEndian);
        [[maybe_unused]] uint16_t dataSize = read_scalar<uint16_t>(celHeader.subspan(14, 2), m_bigEndian);
        uint16_t numChunks = read_scalar<uint16_t>(celHeader.subspan(16, 2), m_bigEndian);
        offset += 22;

        size_t pixel_count = static_cast<size_t>(w) * h;
        if (w == 0 || h == 0 || pixel_count > kMaxCelPixels) {
            throw std::runtime_error("Dimensions de cel invalides");
        }

        std::vector<std::byte> cel_data;
        size_t cel_offset = offset;
        for (int j = 0; j < numChunks; ++j) {
            if (cel_offset + 10 > frameData.size()) {
                throw std::runtime_error("En-tête de chunk invalide");
            }
            auto chunkHeader = std::span(frameData).subspan(cel_offset, 10);
            uint32_t compSz = read_scalar<uint32_t>(chunkHeader.subspan(0, 4), m_bigEndian);
            uint32_t decompSz = read_scalar<uint32_t>(chunkHeader.subspan(4, 4), m_bigEndian);
            uint16_t compType = read_scalar<uint16_t>(chunkHeader.subspan(8, 2), m_bigEndian);
            cel_offset += 10;

            if (cel_offset + compSz > frameData.size()) {
                throw std::runtime_error("Données de chunk insuffisantes");
            }
            auto comp = std::span(frameData).subspan(cel_offset, compSz);
            std::vector<std::byte> decomp;
            if (compType == 0) {
                decomp = lzs_decompress(comp, decompSz);
            } else if (compType == 2) {
                decomp.assign(comp.begin(), comp.end());
            } else {
                throw std::runtime_error("Type de compression inconnu: " + std::to_string(compType));
            }
            cel_data.insert(cel_data.end(), decomp.begin(), decomp.end());
            cel_offset += compSz;
        }

        uint16_t newH = h;
        if (verticalScale != 100) {
            newH = (h * verticalScale) / 100;
            std::vector<std::byte> expanded(cel_data.size() * verticalScale / 100);
            expand_cel(expanded, cel_data, w, h, verticalScale);
            cel_data = std::move(expanded);
        }

        size_t required = static_cast<size_t>(w) * newH * 4;
        if (required > rgba_buffer.capacity()) {
            size_t new_capacity = static_cast<size_t>(required + required / 2);
            rgba_buffer.reserve(new_capacity);
        }
        rgba_buffer.resize(required);
        for (size_t pixel = 0; pixel < cel_data.size(); ++pixel) {
            auto idx = static_cast<uint8_t>(cel_data[pixel]);
            rgba_buffer[pixel * 4 + 0] = m_palette[idx * 3 + 0];
            rgba_buffer[pixel * 4 + 1] = m_palette[idx * 3 + 1];
            rgba_buffer[pixel * 4 + 2] = m_palette[idx * 3 + 2];
            rgba_buffer[pixel * 4 + 3] = std::byte{255};
        }

        std::ostringstream oss;
        oss << std::setw(5) << std::setfill('0') << frameNo << "_" << i << ".png";
        auto outPath = m_dstDir / oss.str();
                std::string outPathStr = outPath.string();
        #ifdef _WIN32
        auto longPath = make_long_path(outPath.wstring());
        if (!stbi_write_png(longPath.c_str(), w, newH, 4, rgba_buffer.data(), w * 4)) {
        #else
        if (!stbi_write_png(outPathStr.c_str(), w, newH, 4, rgba_buffer.data(), w * 4)) {
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

    if (m_hasAudio && m_extractAudio) {
        // Skip the remaining bytes in the packet before the audio block.
        // m_frameSizes[frameNo] bytes of frame data were already read above,
        // so subtract them to avoid overshooting the file position.
        m_fp.seekg(m_packetSizes[frameNo] - m_frameSizes[frameNo] - m_audioBlkSize,
                   std::ios::cur);
        int32_t pos = read_scalar<int32_t>(m_fp, m_bigEndian);
        int32_t size = read_scalar<int32_t>(m_fp, m_bigEndian);
        if (pos != 0 && size <= m_audioBlkSize - 8) {
            std::vector<std::byte> audio(size);
            m_fp.read(reinterpret_cast<char *>(audio.data()), size);
            bool isEven = (pos % 2) == 0;
            if (isEven && m_evenPrimerSize > 0) {
                auto samples = dpcm16_decompress(audio, m_audioPredictorEven);
                writeWav(samples, 22050, m_evenAudioIndex++, true);
            } else if (!isEven && m_oddPrimerSize > 0) {
                auto samples = dpcm16_decompress(audio, m_audioPredictorOdd);
                writeWav(samples, 22050, m_oddAudioIndex++, false);
            }
        }
    }
}

void RobotExtractor::writeWav(const std::vector<int16_t> &samples, uint32_t sampleRate, int blockIndex, bool isEvenChannel) {
    if (sampleRate == 0) sampleRate = 22050;
    std::vector<std::byte> wav;
    size_t data_size = samples.size() * sizeof(int16_t);
    wav.reserve(44 + data_size);
    wav.insert(wav.end(), {std::byte{'R'}, std::byte{'I'}, std::byte{'F'}, std::byte{'F'}});
    uint32_t riff_size = 36 + data_size;
    for (int i = 0; i < 4; ++i) wav.push_back(std::byte(riff_size >> (i * 8)));
    wav.insert(wav.end(), {std::byte{'W'}, std::byte{'A'}, std::byte{'V'}, std::byte{'E'}});
    wav.insert(wav.end(), {std::byte{'f'}, std::byte{'m'}, std::byte{'t'}, std::byte{' '}});
    uint32_t fmt_size = 16;
    for (int i = 0; i < 4; ++i) wav.push_back(std::byte(fmt_size >> (i * 8)));
    wav.push_back(std::byte{1}); wav.push_back(std::byte{0}); // PCM
    wav.push_back(std::byte{1}); wav.push_back(std::byte{0}); // Mono
    for (int i = 0; i < 4; ++i) wav.push_back(std::byte(sampleRate >> (i * 8)));
    uint32_t byte_rate = sampleRate * 2;
    for (int i = 0; i < 4; ++i) wav.push_back(std::byte(byte_rate >> (i * 8)));
    wav.push_back(std::byte{2}); wav.push_back(std::byte{0}); // Block align
    wav.push_back(std::byte{16}); wav.push_back(std::byte{0}); // Bits per sample
    wav.insert(wav.end(), {std::byte{'d'}, std::byte{'a'}, std::byte{'t'}, std::byte{'a'}});
    for (int i = 0; i < 4; ++i) wav.push_back(std::byte(data_size >> (i * 8)));
    for (const auto &sample : samples) {
        for (int i = 0; i < 2; ++i) wav.push_back(std::byte(sample >> (i * 8)));
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
        exportFrame(i, frameJson);
        jsonDoc["frames"].push_back(frameJson);
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
                                     tmpPathStr);;
        }
    }
    std::filesystem::rename(tmpPath, m_dstDir / "metadata.json");
}

} // namespace robot

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
    if (files.size() != 2) {
        std::cerr << "Usage: " << argv[0] << " [--audio] [--quiet] [--force-be | --force-le] <input.rbt> <output_dir>\n";
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
