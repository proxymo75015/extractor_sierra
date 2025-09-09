#ifdef _WIN32
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <vector>
#include "utilities.hpp"
#include "stb_image_write.h"

namespace fs = std::filesystem;

TEST_CASE("PNG export supports non-ASCII filenames") {
    fs::path filename = L"éxport_test.png";
    std::vector<unsigned char> pixel = {255, 0, 0, 255};
    auto longPath = robot::make_long_path(filename.wstring());
    auto utf8 = fs::path{longPath}.string();
    REQUIRE(stbi_write_png(utf8.c_str(), 1, 1, 4, pixel.data(), 4) != 0);
    REQUIRE(fs::exists(filename));
    fs::remove(filename);
}
#endif
