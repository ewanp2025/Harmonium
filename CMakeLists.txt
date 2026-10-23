cmake_minimum_required(VERSION 3.16)
project(Harmonium LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core Gui Widgets Multimedia Xml REQUIRED)

add_executable(Harmonium
    main.cpp
    i18n.h
    drum_presets.h
    xpressive_parser.cpp xpressive_parser.h
    effect.cpp effect.h
    projectio.cpp projectio.h
    midireader.cpp midireader.h
    synthengine.cpp synthengine.h
    mainwindow.cpp mainwindow.h
)

target_link_libraries(Harmonium PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia Qt6::Xml
)

if(MSVC)
    target_compile_options(Harmonium PRIVATE /utf-8)
endif()

if(UNIX AND NOT APPLE)
    target_compile_options(Harmonium PRIVATE -O3 -ffast-math)
    target_link_libraries(Harmonium PRIVATE asound pthread)
elseif(WIN32)
    target_link_libraries(Harmonium PRIVATE winmm)
endif()
