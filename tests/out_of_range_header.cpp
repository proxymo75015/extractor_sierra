#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdint>

#include <nlohmann/json.hpp>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;
using robot::RobotExtractorTester;

static void push16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>(x >> 8));
}

static void push32(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

TEST_CASE("Extraction succeeds with ScummVM-tolerated header anomalies") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "scummvm_outliers.rbt";
    fs::path outDir = tmpDir / "scummvm_outliers_out";
    std::error_code ec;
    fs::remove_all(outDir, ec);
    fs::create_directories(outDir);

    std::vector<uint8_t> data;
    push16(data, 0x16);  // signature
    data.insert(data.end(), {'S', 'O', 'L', '\0'});
    push16(data, 5);     // version
    push16(data, 0);     // audio block size
    push16(data, 2);     // primerZeroCompressFlag (non standard)
    push16(data, 0);     // reserved
    push16(data, 0);     // numFrames (valeur hors borne)
    push16(data, 0);     // paletteSize
    push16(data, 0);     // primerReservedSize
    push16(data, 1);     // xRes
    push16(data, 1);     // yRes
    data.push_back(0);   // hasPalette
    data.push_back(0);   // hasAudio
    push16(data, 0);     // reserved
    push16(data, 240);   // frameRate hors limite précédente
    push16(data, 0);     // isHiRes
    push16(data, 0);     // maxSkippablePackets
    push16(data, 0);     // maxCelsPerFrame (sera corrigé à 1)
    for (int i = 0; i < 4; ++i)
        push32(data, 0);  // champs supplémentaires
    for (int i = 0; i < 2; ++i)
        push32(data, 0);  // zone réservée

    for (int i = 0; i < 256; ++i)
        push32(data, 0);  // cue times
    for (int i = 0; i < 256; ++i)
        push16(data, 0);  // cue values

    size_t pad = (2048 - (data.size() % 2048)) % 2048;
    data.insert(data.end(), pad, 0);

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();

    robot::RobotExtractor headerExtractor(input, outDir, false);
    REQUIRE_NOTHROW(RobotExtractorTester::readHeader(headerExtractor));
    REQUIRE(RobotExtractorTester::numFrames(headerExtractor) == 0);
    REQUIRE(RobotExtractorTester::maxCelsPerFrame(headerExtractor) == 1);

    robot::RobotExtractor extractor(input, outDir, false);
    REQUIRE_NOTHROW(extractor.extract());

    auto metadataPath = outDir / "metadata.json";
    REQUIRE(fs::exists(metadataPath));
    std::ifstream meta(metadataPath);
    REQUIRE(meta.is_open());
    nlohmann::json jsonDoc;
    meta >> jsonDoc;
    REQUIRE(jsonDoc.contains("frames"));
    REQUIRE(jsonDoc["frames"].is_array());
    REQUIRE(jsonDoc["frames"].empty());
}
