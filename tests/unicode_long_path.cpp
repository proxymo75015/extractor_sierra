#ifdef _WIN32
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "utilities.hpp"
#include "stb_image_write.h"

namespace fs = std::filesystem;

TEST_CASE("Output supports Unicode paths longer than 260 characters") {
    std::wstring dirName(200, L'a');
    fs::path dir = dirName;
    fs::create_directory(dir);
    fs::path pngPath = dir / (std::wstring(70, L'b') + L"é.png");
    fs::path wavPath = dir / (std::wstring(70, L'b') + L"é.wav");

    std::vector<unsigned char> pixel = {255, 0, 0, 255};
    auto pngLong = robot::make_long_path(pngPath.wstring());
    auto pngUtf8 = fs::path{pngLong}.u8string();
    REQUIRE(pngUtf8.size() > 260);
    REQUIRE(stbi_write_png(reinterpret_cast<const char *>(pngUtf8.c_str()), 1, 1, 4,
                           pixel.data(), 4) != 0);
    REQUIRE(fs::exists(pngPath));
    fs::remove(pngPath);

    auto wavLong = robot::make_long_path(wavPath.wstring());
    auto wavUtf8 = fs::path{wavLong}.u8string();
    REQUIRE(wavUtf8.size() > 260);
    std::ofstream wavFile(std::filesystem::path{wavLong}, std::ios::binary);
    REQUIRE(wavFile);
    wavFile.write("data", 4);
    wavFile.close();
    REQUIRE(fs::exists(wavPath));
    fs::remove(wavPath);

    fs::remove(dir);
}
#endif
