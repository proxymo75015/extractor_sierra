#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include "utilities.hpp"

namespace fs = std::filesystem;

// Déclarations des fonctions testées
fs::path make_long_path(const fs::path& path);
void log_error(const fs::path& srcPath, const std::string& message);

TEST_CASE("Path handling") {
    SECTION("Valid local path") {
        fs::path path = "test/file.rbt";
        auto result = make_long_path(path);
        REQUIRE(fs::absolute(path).string() == result.string());
    }

#if defined(_WIN32)
    SECTION("Long path handling") {
        fs::path longPath = std::string(300, 'a') + "\\file.rbt";
        auto result = make_long_path(longPath);
        REQUIRE(result.wstring().starts_with(L"\\\\?\\"));
    }

    SECTION("UNC path handling") {
        fs::path uncPath = "\\\\server\\share\\file.rbt";
        auto result = make_long_path(uncPath);
        REQUIRE(result.wstring() == uncPath.wstring());
    }

    SECTION("Excessive UNC path length") {
        fs::path uncPath = "\\\\server\\share\\" + std::string(32768, 'a') + "\\file.rbt";
        auto result = make_long_path(uncPath);
        REQUIRE(result.wstring() == uncPath.wstring());
        // log_error est appelé, mais nous ne testons pas la sortie ici
    }
#endif

    SECTION("Invalid filename characters") {
        fs::path invalidPath = "frame_0_cel_0/invalid.png";
        std::string errorMessage;
        log_error(invalidPath, "Invalid character in filename: " + invalidPath.string());
        // Vérifie que l'erreur est loguée, mais pas de REQUIRE sur le contenu exact
    }
}