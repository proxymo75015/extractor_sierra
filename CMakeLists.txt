cmake_minimum_required(VERSION 3.20)
project(robot_extractor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compilation statique pour Windows (inclure toutes les dépendances)
if(WIN32)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -static")
endif()

# Extracteur principal : analyse RBT et extrait frames/audio
add_executable(robot_extractor
    src/main.cpp
    src/core/rbt_parser.cpp
    src/core/scummvm_robot_helpers.cpp
    src/formats/robot_mkv_exporter.cpp
    src/formats/decompressor_lzs.cpp
    src/formats/lzs.cpp
    src/formats/dpcm.cpp
    src/utils/sci_util.cpp
    src/utils/stb_impl.cpp
)
target_include_directories(robot_extractor PRIVATE 
    ${CMAKE_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/include 
    ${CMAKE_SOURCE_DIR}/src
)

# Exporteur MKV : génère vidéos MKV depuis RBT avec coordonnées
add_executable(export_robot_mkv
    src/export_robot_mkv.cpp
    src/core/rbt_parser.cpp
    src/core/ressci_parser.cpp
    src/core/scummvm_robot_helpers.cpp
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

# JSON library pour configuration
find_package(nlohmann_json 3.11.2 CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

install(FILES README.md LICENSE DESTINATION . CONFIGURATIONS Release)

