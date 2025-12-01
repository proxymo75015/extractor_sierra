cmake_minimum_required(VERSION 3.20)
project(robot_extractor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Construire les tests unitaires" ON)

# Add main executable
add_executable(robot_extractor
    src/main.cpp
    src/core/rbt_parser.cpp
    src/formats/decompressor_lzs.cpp
    src/formats/lzs.cpp
    src/formats/dpcm.cpp
    src/utils/sci_util.cpp
)
target_include_directories(robot_extractor PRIVATE 
    ${CMAKE_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/include 
    ${CMAKE_SOURCE_DIR}/src
)

# Add MKV exporter executable
add_executable(export_robot_mkv
    src/export_robot_mkv.cpp
    src/core/rbt_parser.cpp
    src/formats/robot_mkv_exporter.cpp
    src/formats/decompressor_lzs.cpp
    src/formats/lzs.cpp
    src/formats/dpcm.cpp
    src/utils/sci_util.cpp
    src/utils/stb_impl.cpp
)
target_include_directories(export_robot_mkv PRIVATE 
    ${CMAKE_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/include 
    ${CMAKE_SOURCE_DIR}/src
)

include(FetchContent)

# Find or fetch nlohmann_json
find_package(nlohmann_json 3.11.2 CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

if(NOT BUILD_TESTS)
  # Disabled for now - missing source file
  # add_executable(robot_extractor src/robot_extractor.cpp)
  # target_include_directories(robot_extractor PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
  # target_link_libraries(robot_extractor PRIVATE nlohmann_json::nlohmann_json)
endif()

# Utility to dump RBT compressed blocks for inspection (disabled - missing source)
# add_executable(rbt_dumper src/rbt_dumper.cpp src/robot_extractor.cpp)
# target_include_directories(rbt_dumper PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

if(BUILD_TESTS)
  # Disabled for now - missing source file
  # enable_testing()
  # FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.4.0)
  # FetchContent_MakeAvailable(Catch2)

  # file(GLOB TEST_SOURCES tests/*.cpp)
  # add_executable(tests ${TEST_SOURCES} src/robot_extractor.cpp)
  # target_compile_definitions(tests PRIVATE ROBOT_EXTRACTOR_NO_MAIN ROBOT_EXTRACTOR_TESTING)
  # target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
  # target_link_libraries(tests PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
  # add_test(NAME ExtractionTests COMMAND tests)
endif()

install(FILES readme.md DESTINATION . CONFIGURATIONS Release)

