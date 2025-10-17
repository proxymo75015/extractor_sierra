#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstddef>
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

TEST_CASE("Primer audio pair tronqué") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "trunc_even.rbt";
    fs::path outDir = tmpDir / "trunc_even_out";
    fs::create_directories(outDir);

    auto data = build_header(static_cast<uint16_t>(
        kPrimerHeaderSize + robot::kRobotRunwayBytes));
    auto primer = build_primer_header(kPrimerHeaderSize +
                                          robot::kRobotRunwayBytes,
                                      4, 4);
    data.insert(data.end(), primer.begin(), primer.end());
    data.push_back(0xAA); // données tronquées (2 < 4)
    data.push_back(0xBB);

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, false);
    try {
        extractor.extract();
        FAIL("Aucune exception levée");
    } catch (const std::runtime_error &e) {
        INFO(e.what());
        REQUIRE(std::string(e.what()).find("Primer hors limites") !=
                std::string::npos);
    }
}

TEST_CASE("Primer audio impair tronqué") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "trunc_odd.rbt";
    fs::path outDir = tmpDir / "trunc_odd_out";
    fs::create_directories(outDir);

    auto data =
        build_header(static_cast<uint16_t>(kPrimerHeaderSize + 6));
    auto primer = build_primer_header(kPrimerHeaderSize + 6, 2, 4);
    data.insert(data.end(), primer.begin(), primer.end());
    // even primer complet
    data.push_back(0xAA);
    data.push_back(0xBB);
    // odd primer tronqué (1 < 4)
    data.push_back(0xCC);

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, false);
    try {
        extractor.extract();
        FAIL("Aucune exception levée");
    } catch (const std::runtime_error &e) {
        INFO(e.what());
        REQUIRE(std::string(e.what()).find("Primer hors limites") !=
                std::string::npos);
    }
}

TEST_CASE("Primer plus court que la piste d'élan est utilisé") {
    fs::path tmpDir = fs::temp_directory_path();
    fs::path input = tmpDir / "primer_short_runway.rbt";
    fs::path outDir = tmpDir / "primer_short_runway_out";
    fs::remove_all(outDir);
    fs::create_directories(outDir);

    constexpr uint32_t evenSize = robot::kRobotRunwayBytes - 2;
    constexpr uint32_t oddSize = robot::kRobotRunwayBytes;
    const uint32_t totalPrimerSize = kPrimerHeaderSize + evenSize + oddSize;
    auto header = build_header(static_cast<uint16_t>(totalPrimerSize));
    auto primer = build_primer_header(totalPrimerSize, evenSize, oddSize);

    std::vector<uint8_t> data;
    data.reserve(header.size() + primer.size() + evenSize + oddSize);
    data.insert(data.end(), header.begin(), header.end());
    data.insert(data.end(), primer.begin(), primer.end());

    std::array<uint8_t, evenSize> evenBytes{};
    for (size_t i = 0; i < evenBytes.size(); ++i)
        evenBytes[i] = static_cast<uint8_t>(0x10 + i);
    data.insert(data.end(), evenBytes.begin(), evenBytes.end());

    std::array<uint8_t, oddSize> oddBytes{};
    for (size_t i = 0; i < oddBytes.size(); ++i)
        oddBytes[i] = static_cast<uint8_t>(0x80 + i);
    data.insert(data.end(), oddBytes.begin(), oddBytes.end());

    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();

    robot::RobotExtractor extractor(input, outDir, true);
    REQUIRE_NOTHROW(robot::RobotExtractorTester::readHeader(extractor));
    REQUIRE_NOTHROW(robot::RobotExtractorTester::readPrimer(extractor));

    const auto &evenPrimer = robot::RobotExtractorTester::evenPrimer(extractor);
    REQUIRE(evenPrimer.size() == evenBytes.size());
    for (size_t i = 0; i < evenBytes.size(); ++i) {
        REQUIRE(std::to_integer<uint8_t>(evenPrimer[i]) == evenBytes[i]);
    }

    const auto primerEnd = robot::RobotExtractorTester::postPrimerPos(extractor);
    REQUIRE(primerEnd == robot::RobotExtractorTester::postHeaderPos(extractor) +
                             static_cast<std::streamoff>(totalPrimerSize));

    REQUIRE(robot::RobotExtractorTester::evenPrimerSize(extractor) ==
            static_cast<std::streamsize>(evenSize));
    REQUIRE(robot::RobotExtractorTester::oddPrimerSize(extractor) ==
            static_cast<std::streamsize>(oddSize));

    REQUIRE_NOTHROW(robot::RobotExtractorTester::finalizeAudio(extractor));

    const auto evenStream =
        robot::RobotExtractorTester::buildChannelStream(extractor, true);
    const auto oddStream =
        robot::RobotExtractorTester::buildChannelStream(extractor, false);
    REQUIRE(evenStream.size() == evenSize);
    REQUIRE(oddStream.size() == oddSize);
}
