cmake_minimum_required(VERSION 3.22.1)
project(mmfplayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SMAF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/yamaha-smaf-player")

add_library(mmfplayer SHARED
    native_bridge.cpp
    "${SMAF_DIR}/src/smaf_file.cpp"
    "${SMAF_DIR}/src/ma_fm_core.cpp"
    "${SMAF_DIR}/src/smaf_voice.cpp"
    "${SMAF_DIR}/src/ma_player.cpp"
)

target_include_directories(mmfplayer PRIVATE "${SMAF_DIR}/src")
target_compile_options(mmfplayer PRIVATE -Wall -Wextra -O3)
