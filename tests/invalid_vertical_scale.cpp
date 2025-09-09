#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;

static void push16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>(x >> 8));
}

TEST_CASE("Vertical scale out of range throws") {
    std::vector<uint8_t> data;
    data.reserve(2 + 22);
    push16(data, 1); // numCels

    std::vector<uint8_t> celHeader(22, 0);
    celHeader[1] = 150; // verticalScale > 100
    celHeader[2] = 1;   // width
    celHeader[4] = 1;   // height
    data.insert(data.end(), celHeader.begin(), celHeader.end());

    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "invalid_vertical_scale.bin";
    fs::path outDir = tmpDir / "invalid_vertical_scale_out";
    fs::create_directories(outDir);

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, false);
    extractor.m_hasPalette = true;
    extractor.m_bigEndian = false;
    extractor.m_maxCelsPerFrame = 1;
    extractor.m_frameSizes = {static_cast<uint32_t>(data.size())};
    extractor.m_packetSizes = {static_cast<uint32_t>(data.size())};
    extractor.m_palette.assign(768, std::byte{0});
    extractor.m_fp.seekg(0, std::ios::beg);

    nlohmann::json frameJson;
    try {
        extractor.exportFrame(0, frameJson);
        FAIL("No exception thrown");
    } catch (const std::runtime_error &e) {
        REQUIRE(std::string(e.what()).find("Facteur d'échelle vertical invalide") !=
                std::string::npos);
    }
}
