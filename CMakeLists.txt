cmake_minimum_required(VERSION 3.20)
project(robot_extractor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Construire les tests unitaires" ON)

# Add robot_decoder executable
add_executable(robot_decoder 
    src/robot_decoder/main.cpp
    src/robot_decoder/rbt_parser.cpp
    src/robot_decoder/robot_audio_stream.cpp
    src/robot_decoder/sci_util.cpp
    src/robot_decoder/dpcm.cpp
    src/robot_decoder/decompressor_lzs.cpp
    src/robot_decoder/lzs.cpp
    src/formats/robot_exr_exporter.cpp
    src/utils/exr_writer.cpp
)
target_include_directories(robot_decoder PRIVATE 
    ${CMAKE_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/include 
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(robot_decoder PRIVATE OpenEXR::OpenEXR)

include(FetchContent)

# Find OpenEXR (required for EXR export functionality)
find_package(OpenEXR REQUIRED CONFIG)

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
