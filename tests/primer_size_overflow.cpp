#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;

constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);

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

static std::vector<uint8_t> build_header(uint16_t primerReservedSize) {
    std::vector<uint8_t> h;
    push16(h, 0x16);            // signature
    h.insert(h.end(), {'S','O','L','\0'});
    push16(h, 5);               // version
    push16(h, 8);               // audio block size
    push16(h, 0);               // primerZeroCompressFlag
    push16(h, 0);               // skip
    push16(h, 1);               // numFrames
    push16(h, 0);               // paletteSize
    push16(h, primerReservedSize);
    push16(h, 1);               // xRes
    push16(h, 1);               // yRes
    h.push_back(0);             // hasPalette
    h.push_back(1);             // hasAudio
    push16(h, 0);               // skip
    push16(h, 60);              // frameRate
    push16(h, 0);               // isHiRes
    push16(h, 0);               // maxSkippablePackets
    push16(h, 1);               // maxCelsPerFrame
    for (int i = 0; i < 4; ++i) push32(h, 0); // champs supplémentaires
    for (int i = 0; i < 2; ++i) push32(h, 0); // zone réservée
    return h;
}

static std::vector<uint8_t> build_primer_header(uint32_t total,
                                                uint32_t evenSize,
                                                uint32_t oddSize) {
    std::vector<uint8_t> p;
    push32(p, total);
    push16(p, 0); // compType
    push32(p, evenSize);
    push32(p, oddSize);
    return p;
}

TEST_CASE("Somme des primers dépasse les bornes") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "overflow_primer.rbt";
    fs::path outDir = tmpDir / "overflow_primer_out";
    fs::create_directories(outDir);

    const uint32_t evenSize = 4;
    const uint32_t oddSize = 5;
    const uint32_t total = kPrimerHeaderSize + evenSize + oddSize;
    auto data = build_header(static_cast<uint16_t>(kPrimerHeaderSize + 8));
    auto primer = build_primer_header(total, evenSize, oddSize);
    data.insert(data.end(), primer.begin(), primer.end());

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, false);
    REQUIRE_THROWS_AS(extractor.extract(), std::runtime_error);
}
