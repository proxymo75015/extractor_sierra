cmake_minimum_required(VERSION 3.10)

project(robot_decoder LANGUAGES CXX)

add_executable(robot_decoder
    main.cpp
    rbt_parser.cpp
    lzs.cpp
    dpcm.cpp
    decompressor_lzs.cpp
    sci_util.cpp
    robot_audio_stream.cpp
)

target_include_directories(robot_decoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(robot_decoder PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
