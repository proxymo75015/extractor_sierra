#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"
#include "palette_helpers.hpp"

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

TEST_CASE("Invalid cel size throws") {
    const uint16_t w = 4;
    const uint16_t h = 4;
    const size_t pixels = static_cast<size_t>(w) * h;

    std::vector<uint8_t> data;
    data.reserve(2 + 22 + 10 + pixels);
    push16(data, 1); // numCels

    std::vector<uint8_t> celHeader(22, 0);
    celHeader[1] = 100; // vertical scale
    celHeader[2] = static_cast<uint8_t>(w & 0xFF);
    celHeader[3] = static_cast<uint8_t>(w >> 8);
    celHeader[4] = static_cast<uint8_t>(h & 0xFF);
    celHeader[5] = static_cast<uint8_t>(h >> 8);
    celHeader[14] = 1; // incorrect data size
    celHeader[16] = 1; // numChunks
    data.insert(data.end(), celHeader.begin(), celHeader.end());

    std::vector<uint8_t> chunkHeader(10, 0);
    uint32_t sz = static_cast<uint32_t>(pixels);
    chunkHeader[0] = static_cast<uint8_t>(sz & 0xFF);
    chunkHeader[1] = static_cast<uint8_t>((sz >> 8) & 0xFF);
    chunkHeader[2] = static_cast<uint8_t>((sz >> 16) & 0xFF);
    chunkHeader[3] = static_cast<uint8_t>((sz >> 24) & 0xFF);
    chunkHeader[4] = chunkHeader[0];
    chunkHeader[5] = chunkHeader[1];
    chunkHeader[6] = chunkHeader[2];
    chunkHeader[7] = chunkHeader[3];
    chunkHeader[8] = 2; // uncompressed
    chunkHeader[9] = 0;
    data.insert(data.end(), chunkHeader.begin(), chunkHeader.end());

    data.insert(data.end(), pixels, 0); // pixel data

    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "invalid_cel_size.bin";
    fs::path outDir = tmpDir / "invalid_cel_size_out";
    fs::create_directories(outDir);

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, false);
    RobotExtractorTester::hasPalette(extractor) = true;
    RobotExtractorTester::bigEndian(extractor) = false;
    RobotExtractorTester::maxCelsPerFrame(extractor) = 1;
    RobotExtractorTester::frameSizes(extractor) = {static_cast<uint32_t>(data.size())};
    RobotExtractorTester::packetSizes(extractor) = {static_cast<uint32_t>(data.size())};
  RobotExtractorTester::palette(extractor) =
      test_palette::build_flat_palette(0, 0, 0);
    RobotExtractorTester::file(extractor).seekg(0, std::ios::beg);
    
    nlohmann::json frameJson;
    try {
        RobotExtractorTester::exportFrame(extractor, 0, frameJson);
        FAIL("No exception thrown");
    } catch (const std::runtime_error &e) {
        REQUIRE(std::string(e.what()).find(
                    "Données de cel malformées: taille déclarée incohérente") !=
                std::string::npos);
    }
}
