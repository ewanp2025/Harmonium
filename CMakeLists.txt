cmake_minimum_required(VERSION 3.16)
project(HarmoniumEsp32Sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core Gui Widgets Multimedia REQUIRED)

add_executable(HarmoniumEsp32Sim
    main.cpp
    midireader.cpp
    midireader.h
    xpressive_parser.h
    effect.h
    drum_presets.h
    synthengine.h
    storage.h
    display_driver_qt.h
    i18n.h
    bt_audio.h
)

target_link_libraries(HarmoniumEsp32Sim PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia
)

if(MSVC)
    target_compile_options(HarmoniumEsp32Sim PRIVATE /utf-8)
endif()

if(UNIX AND NOT APPLE)
    target_compile_options(HarmoniumEsp32Sim PRIVATE -O3 -ffast-math)
    target_link_libraries(HarmoniumEsp32Sim PRIVATE asound pthread)
elseif(WIN32)
    target_link_libraries(HarmoniumEsp32Sim PRIVATE winmm)
endif()
