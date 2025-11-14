// Simplified Robot extractor API (minimal implementation)
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace robot {

// Constants used by tests and extractor
inline constexpr size_t kRobotZeroCompressSize = 2048;
inline constexpr size_t kRobotRunwayBytes = 8;
inline constexpr size_t kRobotRunwaySamples = kRobotRunwayBytes / sizeof(int16_t);
inline constexpr size_t kRobotAudioHeaderSize = 8;

// Simple options struct for extractor behaviour
struct ExtractorOptions {
    bool forceBigEndian = false;
    bool forceLittleEndian = false;
    bool quiet = false;
};

// Decompress an LZS-compressed buffer into expected_size bytes.
// This is a public helper used by tests.
std::vector<std::byte> lzs_decompress(std::span<const std::byte> src, size_t expected_size);

// Decompress SOL DPCM16-compressed data. Returns vector of int16 samples
// `predictor` is passed by reference and updated to the last sample value.
std::vector<int16_t> dpcm16_decompress(std::span<const std::byte> src, int16_t &predictor);
void dpcm16_decompress_last(std::span<const std::byte> src, int16_t &predictor);

// Expand vertically-compressed cel into target buffer.
void expand_cel(std::span<uint8_t> target, std::span<const uint8_t> source, int celWidth, int celHeight, int verticalScalePercent);

// Simple RobotExtractor: high-level class to extract PNG/WAV/metadata
class RobotExtractor {
public:
    RobotExtractor(const std::filesystem::path &inputFile,
                   const std::filesystem::path &outDir,
                   bool extractAudio = false,
                   const ExtractorOptions &opts = {});

    // Perform extraction (creates output files). Returns true on success.
    bool extractAll();

    // Accessors for tests (basic)
    const std::filesystem::path &inputPath() const { return _input; }
    const std::filesystem::path &outputPath() const { return _outDir; }

private:
    std::filesystem::path _input;
    std::filesystem::path _outDir;
    bool _extractAudio;
    ExtractorOptions _opts;
};

} // namespace robot
