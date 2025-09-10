#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <limits>

#include "utilities.hpp"

namespace fs = std::filesystem;

TEST_CASE("read_exact throws when size exceeds streamsize") {
    fs::path tmp = fs::temp_directory_path() / "too_big_read.bin";
    {
        std::ofstream out(tmp, std::ios::binary);
        // create empty file
    }
    std::ifstream in(tmp, std::ios::binary);
    char dummy = 0;
    size_t big = static_cast<size_t>(std::numeric_limits<std::streamsize>::max()) + 1;
    REQUIRE_THROWS(robot::read_exact(in, &dummy, big));
    in.close();
    fs::remove(tmp);
}
