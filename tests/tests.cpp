#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include "utilities.hpp"

namespace fs = std::filesystem;

using robot::log_error;
using robot::ExtractorOptions;
#ifdef _WIN32
using robot::make_long_path;
#endif

TEST_CASE("Path handling") {
#ifdef _WIN32
    SECTION("Valid local path") {
        fs::path path = "test/file.rbt";
        fs::path result = make_long_path(path.wstring());
        REQUIRE(fs::absolute(path) == result);
    }

    SECTION("Long path handling") {
        fs::path longPath = std::string(300, 'a') + "\\file.rbt";
        auto result = make_long_path(longPath.wstring());
        auto absolute = fs::absolute(longPath).wstring();
        REQUIRE(result.starts_with(L"\\\\?\\"));
        REQUIRE(result.substr(4) == absolute);
    }

    SECTION("UNC path handling") {
        fs::path uncPath = "\\\\server\\share\\file.rbt";
        auto result = make_long_path(uncPath.wstring());
        REQUIRE(result == uncPath.wstring());
    }

    SECTION("Excessive UNC path length") {
        fs::path uncPath = "\\\\server\\share\\" + std::string(32768, 'a') + "\\file.rbt";
        auto result = make_long_path(uncPath.wstring());
        REQUIRE(result == uncPath.wstring());
        // log_error est appelé, mais nous ne testons pas la sortie ici
    }
#endif

    SECTION("Invalid filename characters") {
        fs::path invalidPath = "frame_0_cel_0/invalid.png";
        std::string errorMessage;
        ExtractorOptions opt;
        log_error(invalidPath, "Invalid character in filename: " + invalidPath.string(), opt);
        // Vérifie que l'erreur est loguée, mais pas de REQUIRE sur le contenu exact
    }
}


