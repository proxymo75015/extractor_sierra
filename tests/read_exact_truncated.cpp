#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "utilities.hpp"

namespace fs = std::filesystem;

TEST_CASE("read_exact resets position on incomplete read") {
    fs::path tmp = fs::temp_directory_path() / "truncated_read.bin";
    {
        std::ofstream out(tmp, std::ios::binary);
        char data[2] = {0x11, 0x22};
        out.write(data, 2);
    }

    std::ifstream in(tmp, std::ios::binary);
    char buf[4];
    auto start = in.tellg();
    REQUIRE(start == 0);
    try {
        robot::read_exact(in, buf, 4);
        FAIL("Aucune exception levée");
    } catch (const std::runtime_error &e) {
        REQUIRE(std::string(e.what()).find("Lecture incomplète") != std::string::npos);
    }
    REQUIRE(in.tellg() == start);
    char buf2[2];
    REQUIRE_NOTHROW(robot::read_exact(in, buf2, 2));
    in.close();
    fs::remove(tmp);
}
