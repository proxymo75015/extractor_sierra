#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

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

static void appendCel(std::vector<uint8_t> &data, uint8_t pixelValue) {
    const uint16_t w = 1;
    const uint16_t h = 1;
    const uint16_t dataSize = 10 + 1; // chunk header + payload

    const size_t headerStart = data.size();
    data.resize(headerStart + 22);
    auto *celHeader = data.data() + static_cast<std::ptrdiff_t>(headerStart);
    celHeader[1] = 100; // vertical scale
    celHeader[2] = static_cast<uint8_t>(w & 0xFF);
    celHeader[3] = static_cast<uint8_t>(w >> 8);
    celHeader[4] = static_cast<uint8_t>(h & 0xFF);
    celHeader[5] = static_cast<uint8_t>(h >> 8);
    celHeader[14] = static_cast<uint8_t>(dataSize & 0xFF);
    celHeader[15] = static_cast<uint8_t>(dataSize >> 8);
    celHeader[16] = 1; // numChunks
    celHeader[17] = 0;

    push32(data, 1); // compSz
    push32(data, 1); // decompSz
    push16(data, 2); // compType = uncompressed
    data.push_back(pixelValue);
}

TEST_CASE("Une frame avec plus de cels que l'annonce est exportée") {
    std::vector<uint8_t> frameData;
    push16(frameData, 2); // numCels réel
    appendCel(frameData, 0x11);
    appendCel(frameData, 0x22);

    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "excess_cels.bin";
    fs::path outDir = tmpDir / "excess_cels_out";
    fs::create_directories(outDir);

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(frameData.data()),
              static_cast<std::streamsize>(frameData.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, false);
    RobotExtractorTester::hasPalette(extractor) = false; // évite l'écriture de PNG
    RobotExtractorTester::bigEndian(extractor) = false;
    RobotExtractorTester::maxCelsPerFrame(extractor) = 1; // valeur de l'en-tête
    RobotExtractorTester::frameSizes(extractor) =
        {static_cast<uint32_t>(frameData.size())};
    RobotExtractorTester::packetSizes(extractor) =
        {static_cast<uint32_t>(frameData.size())};
    RobotExtractorTester::file(extractor).seekg(0, std::ios::beg);

    nlohmann::json frameJson;
    bool exported = false;
    REQUIRE_NOTHROW(exported = RobotExtractorTester::exportFrame(extractor, 0, frameJson));
    REQUIRE(exported);
    REQUIRE(frameJson["cels"].size() == 2);
    REQUIRE(RobotExtractorTester::maxCelsPerFrame(extractor) == 2);
}

TEST_CASE("Les chunks non compressés plus grands que la taille annoncée sont tolérés") {
    std::vector<uint8_t> frameData;
    push16(frameData, 1); // numCels

    const uint16_t w = 1;
    const uint16_t h = 1;
    const uint32_t compSz = 2;
    const uint32_t decompSz = 1;
    const uint16_t dataSize = static_cast<uint16_t>(10 + compSz);

    const size_t headerStart = frameData.size();
    frameData.resize(headerStart + 22);
    auto *celHeader = frameData.data() + static_cast<std::ptrdiff_t>(headerStart);
    celHeader[1] = 100; // vertical scale
    celHeader[2] = static_cast<uint8_t>(w & 0xFF);
    celHeader[3] = static_cast<uint8_t>(w >> 8);
    celHeader[4] = static_cast<uint8_t>(h & 0xFF);
    celHeader[5] = static_cast<uint8_t>(h >> 8);
    celHeader[14] = static_cast<uint8_t>(dataSize & 0xFF);
    celHeader[15] = static_cast<uint8_t>(dataSize >> 8);
    celHeader[16] = 1; // numChunks
    celHeader[17] = 0;

    push32(frameData, compSz);
    push32(frameData, decompSz);
    push16(frameData, 2); // compType = uncompressed
    frameData.push_back(0x33);
    frameData.push_back(0x44); // données supplémentaires

    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "uncompressed_padding.bin";
    fs::path outDir = tmpDir / "uncompressed_padding_out";
    fs::create_directories(outDir);

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(frameData.data()),
              static_cast<std::streamsize>(frameData.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, false);
    RobotExtractorTester::hasPalette(extractor) = false;
    RobotExtractorTester::bigEndian(extractor) = false;
    RobotExtractorTester::maxCelsPerFrame(extractor) = 1;
    RobotExtractorTester::frameSizes(extractor) =
        {static_cast<uint32_t>(frameData.size())};
    RobotExtractorTester::packetSizes(extractor) =
        {static_cast<uint32_t>(frameData.size())};
    RobotExtractorTester::file(extractor).seekg(0, std::ios::beg);

    nlohmann::json frameJson;
    bool exported = false;
    REQUIRE_NOTHROW(exported = RobotExtractorTester::exportFrame(extractor, 0, frameJson));
    REQUIRE(exported);
    REQUIRE(frameJson["cels"].size() == 1);
}
