#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;

TEST_CASE("LZS decompression basic") {
    // simple compressed payload that our minimal decoder will expand
    const std::vector<std::byte> comp = {std::byte{0x20}, std::byte{0x90}, std::byte{0xB0}, std::byte{0x58}};
    auto out = robot::lzs_decompress(std::span<const std::byte>(comp.data(), comp.size()), 7);
    REQUIRE(out.size() == 7);
}

TEST_CASE("DPCM16 decompression basic") {
    int16_t predictor = 0;
    const std::vector<std::byte> data = {std::byte{0x10}, std::byte{0x32}, std::byte{0x54}, std::byte{0x76}};
    auto pcm = robot::dpcm16_decompress(std::span<const std::byte>(data.data(), data.size()), predictor);
    REQUIRE(pcm.size() == data.size());
}

TEST_CASE("RobotExtractor creates outputs") {
    fs::path tmp = fs::temp_directory_path();
    fs::path in = tmp / "test_simple.rbt";
    fs::path out = tmp / "test_simple_out";
    // write a minimal dummy input file
    std::ofstream f(in, std::ios::binary);
    f << "DUMMY";
    f.close();

    fs::remove_all(out);
    robot::ExtractorOptions opts;
    robot::RobotExtractor extractor(in, out, true, opts);
    REQUIRE(extractor.extractAll());
    REQUIRE(fs::exists(out / "metadata.json"));
    REQUIRE(fs::exists(out / "frame_00000_0.png"));
    REQUIRE(fs::exists(out / "frame_00000.wav"));

    fs::remove(in);
    fs::remove_all(out);
}
