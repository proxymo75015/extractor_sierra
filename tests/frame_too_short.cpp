#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;
using robot::RobotExtractorTester;

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
    RobotExtractorTester::frameSizes(extractor) = {2};
    RobotExtractorTester::packetSizes(extractor) = {2};
    RobotExtractorTester::file(extractor).seekg(0, std::ios::beg);

    nlohmann::json frameJson;
    try {
        RobotExtractorTester::exportFrame(extractor, 0, frameJson);
        FAIL("Aucune exception levée");
    } catch (const std::runtime_error &e) {
        REQUIRE(std::string(e.what()).find(
                    "Taille de frame dépasse les données restantes du fichier") !=
                std::string::npos);
    }
}
