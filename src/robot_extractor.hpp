#pragma once

#include "utilities.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace robot {

class RobotExtractor {
public:
    RobotExtractor(const std::filesystem::path &srcPath, const std::filesystem::path &dstDir, bool extractAudio);
    void extract();

private:
    static constexpr uint16_t kRobotSig = 0x16;
    static constexpr size_t kMaxCelPixels = 1024 * 1024;
    static constexpr size_t kMaxCuePoints = 256;

    void readHeader();
    void readPrimer();
    void readPalette();
    void readSizesAndCues();
    void exportFrame(int frameNo, nlohmann::json &frameJson);
    void writeWav(const std::vector<int16_t> &samples, uint32_t sampleRate, int frameNo, bool isEvenChannel);

    std::filesystem::path m_srcPath;
    std::filesystem::path m_dstDir;
    std::ifstream m_fp;
    bool m_bigEndian = false;bool m_bigEndian = false;
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
    int32_t m_evenPrimerSize;
    int32_t m_oddPrimerSize;
    int32_t m_totalPrimerSize;
    int32_t m_primerPosition;
    std::vector<std::byte> m_evenPrimer;
    std::vector<std::byte> m_oddPrimer;
    int16_t m_audioPredictorEven = 0;
    int16_t m_audioPredictorOdd = 0;
};

} // namespace robot
