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

static void push32(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

static std::vector<uint8_t> build_header() {
    std::vector<uint8_t> h;
    push16(h, 0x16);           // signature
    h.insert(h.end(), {'S','O','L','\0'});
    push16(h, 5);              // version
    push16(h, 24);             // audio block size
    push16(h, 0);              // primerZeroCompressFlag
    push16(h, 0);              // skip
    push16(h, 1);              // numFrames
    push16(h, 0);              // paletteSize
    push16(h, 2);              // primerReservedSize
    push16(h, 1);              // xRes
    push16(h, 1);              // yRes
    h.push_back(0);            // hasPalette
    h.push_back(1);            // hasAudio
    push16(h, 0);              // skip
    push16(h, 60);             // frameRate
    push16(h, 0);              // isHiRes
    push16(h, 0);              // maxSkippablePackets
    push16(h, 1);              // maxCelsPerFrame
    for (int i = 0; i < 4; ++i) push32(h, 0); // maxCelArea
    for (int i = 0; i < 2; ++i) push32(h, 0); // padding
    return h;
}

static std::vector<uint8_t> build_primer_header() {
    std::vector<uint8_t> p;
    push32(p, 2);  // total primer size (unused)
    push16(p, 0);  // compType
    push32(p, 2);  // even size
    push32(p, 0);  // odd size
    return p;
}

TEST_CASE("Negative audio position throws") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "neg_audio_pos.rbt";
    fs::path outDir = tmpDir / "neg_audio_pos_out";
    fs::create_directories(outDir);

    auto data = build_header();
    auto primer = build_primer_header();
    data.insert(data.end(), primer.begin(), primer.end());
    data.push_back(0x88); // even primer data
    data.push_back(0x88);

    push16(data, 2);   // frame size
    push16(data, 26);  // packet size

    for (int i = 0; i < 256; ++i) push32(data, 0); // cue times
    for (int i = 0; i < 256; ++i) push16(data, 0); // cue values

    data.resize(((data.size() + 2047) / 2048) * 2048, 0); // pad to 2048-byte boundary

    // Frame data (numCels = 0)
    data.push_back(0); // numCels low byte
    data.push_back(0); // numCels high byte

    push32(data, static_cast<uint32_t>(-2)); // negative pos
    push32(data, 10);  // size (unused, but valid)
    for (int i = 0; i < 8; ++i) data.push_back(0); // runway
    for (int i = 0; i < 2; ++i) data.push_back(0); // audio data
    for (int i = 0; i < 6; ++i) data.push_back(0); // padding

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, true);
    try {
        extractor.extract();
        FAIL("Aucune exception levée");
    } catch (const std::runtime_error &e) {
        REQUIRE(std::string(e.what()).find("Position audio invalide") != std::string::npos);
    }
}
