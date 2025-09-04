#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>

#ifndef _WIN32
namespace fs = std::filesystem;

TEST_CASE("Options --force-be et --force-le mutuellement exclusives") {
    fs::path exe = fs::canonical("/proc/self/exe").parent_path() / "robot_extractor";
    std::string cmd = exe.string() + " --force-be --force-le 2>&1";
    std::array<char, 128> buffer{};
    std::string output;
    FILE *pipe = popen(cmd.c_str(), "r");
    REQUIRE(pipe != nullptr);
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    int ret = pclose(pipe);
    REQUIRE(ret != 0);
    REQUIRE(output.find("mutuellement exclusives") != std::string::npos);
}
#endif
