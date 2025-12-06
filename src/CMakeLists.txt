cmake_minimum_required(VERSION 3.10)

project(robot_decoder LANGUAGES CXX)

add_executable(robot_decoder
    main.cpp
    core/rbt_parser.cpp
    core/ressci_parser.cpp
    formats/dpcm.cpp
    formats/lzs.cpp
    formats/decompressor_lzs.cpp
    utils/sci_util.cpp
)

target_include_directories(robot_decoder PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/formats
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
)
set_target_properties(robot_decoder PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# Extracteur de coordonnées Robot depuis RESSCI
add_executable(extract_coordinates
    extract_coordinates.cpp
    core/ressci_parser.cpp
    formats/lzs.cpp
    formats/decompressor_lzs.cpp
)

target_include_directories(extract_coordinates PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/formats
)
set_target_properties(extract_coordinates PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
