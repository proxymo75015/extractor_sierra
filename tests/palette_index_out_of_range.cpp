#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;

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

TEST_CASE("Palette index out of range throws") {
    const uint16_t w = 1024;
    const uint16_t h = 1024;
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
    celHeader[14] = 1; // data size LSB
    celHeader[16] = 1; // numChunks LSB
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
    chunkHeader[8] = 2; // compType uncompressed
    chunkHeader[9] = 0;
    data.insert(data.end(), chunkHeader.begin(), chunkHeader.end());

    data.insert(data.end(), pixels, 255); // pixel data all 255

    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "palette_oob_frame.bin";
    fs::path outDir = tmpDir / "palette_oob_out";
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
    extractor.m_palette.assign(768, std::byte{0}); // 256 colors
    extractor.m_fp.seekg(0, std::ios::beg);

    std::exception_ptr eptr;
    std::thread th([&] {
        nlohmann::json frameJson;
        try {
            extractor.exportFrame(0, frameJson);
        } catch (...) {
            eptr = std::current_exception();
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    extractor.m_palette.resize(3); // shrink palette to trigger OOB during loop

    th.join();

    REQUIRE(eptr);
    try {
        std::rethrow_exception(eptr);
        FAIL("No exception thrown");
    } catch (const std::runtime_error &e) {
        INFO(std::string(e.what()));
        REQUIRE(std::string(e.what()).find("Indice de palette hors limites") !=
                std::string::npos);
    }
}
