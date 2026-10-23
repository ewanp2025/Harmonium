QT += core gui widgets multimedia xml
CONFIG += c++17 release

TARGET = Harmonium
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
    xpressive_parser.cpp \
    effect.cpp \
    projectio.cpp \
    midireader.cpp \
    synthengine.cpp \
    mainwindow.cpp

HEADERS += \
    i18n.h \
    xpressive_parser.h \
    drum_presets.h \
    effect.h \
    projectio.h \
    midireader.h \
    synthengine.h \
    mainwindow.h
