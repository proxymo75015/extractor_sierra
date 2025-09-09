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

TEST_CASE("Audio block size exceeding limit throws") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "audio_block_too_big.rbt";
    fs::path outDir = tmpDir / "audio_block_too_big_out";
    fs::create_directories(outDir);

    std::vector<uint8_t> data;
    push16(data, 0x16);  // signature
    data.insert(data.end(), {'S', 'O', 'L', '\0'});
    push16(data, 5);  // version
    // Value equal to the maximum triggers the check (limit is exclusive)
    push16(data, robot::RobotExtractor::kMaxAudioBlockSize);

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, true);
    try {
        extractor.extract();
        FAIL("Aucune exception levée");
    } catch (const std::runtime_error &e) {
        REQUIRE(std::string(e.what()).find("Taille de bloc audio excessive") !=
                std::string::npos);
    }
}
