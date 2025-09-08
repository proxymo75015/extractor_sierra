#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;

TEST_CASE("Frame trop courte") {
    std::vector<uint8_t> data(1, 0);
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "frame_too_short.bin";
    fs::path outDir = tmpDir / "frame_too_short_out";
    fs::create_directories(outDir);

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, false);
    extractor.m_frameSizes = {1};
    extractor.m_packetSizes = {1};
    extractor.m_fp.seekg(0, std::ios::beg);

    nlohmann::json frameJson;
    try {
        extractor.exportFrame(0, frameJson);
        FAIL("Aucune exception levée");
    } catch (const std::runtime_error &e) {
        REQUIRE(std::string(e.what()).find("Frame trop courte") !=
                std::string::npos);
    }
}
