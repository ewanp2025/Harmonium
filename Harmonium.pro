QT += core gui widgets multimedia
CONFIG += c++17 release

TARGET = HarmoniumEsp32Sim
TEMPLATE = app

msvc {
    QMAKE_CXXFLAGS += /utf-8
}

unix:!macx {
    QMAKE_CXXFLAGS_RELEASE += -O3 -ffast-math
    LIBS += -lasound -lpthread
}

win32 {
    LIBS += -lwinmm
}

SOURCES += \
    main.cpp \
    midireader.cpp

HEADERS += \
    midireader.h \
    xpressive_parser.h \
    effect.h \
    drum_presets.h \
    synthengine.h \
    storage.h \
    display_driver_qt.h \
    i18n.h \
    bt_audio.h
