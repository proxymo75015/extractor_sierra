     #define CATCH_CONFIG_MAIN
     #include <catch2/catch.hpp>
     #include "utilities.hpp"

     TEST_CASE("make_long_path handles Windows long paths", "[utilities]") {
         std::filesystem::path path = "C:/very/long/path/that/exceeds/260/characters/..."
                                    "12345678901234567890123456789012345678901234567890"
                                    "12345678901234567890123456789012345678901234567890"
                                    "12345678901234567890123456789012345678901234567890"
                                    "12345678901234567890123456789012345678901234567890"
                                    "12345678901234567890123456789012345678901234567890";
         auto long_path = make_long_path(path);
         #ifdef _WIN32
         REQUIRE(long_path.wstring().starts_with(L"\\\\?\\"));
         #else
         REQUIRE(long_path == std::filesystem::absolute(path));
         #endif
     }

     TEST_CASE("log_error writes to file", "[utilities]") {
         std::filesystem::path src = "test.rbt";
         std::string msg = "Test error message";
         log_error(src, msg);
         std::ifstream logfile("robot_extractor.log");
         REQUIRE(logfile.good());
         std::string line;
         bool found = false;
         while (std::getline(logfile, line)) {
             if (line.find("[ERREUR] test.rbt : Test error message") != std::string::npos) {
                 found = true;
                 break;
             }
         }
         REQUIRE(found);
     }