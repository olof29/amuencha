#-------------------------------------------------
#
# Project created by QtCreator 2016-06-10T16:53:23
#
#-------------------------------------------------

QT       += core gui multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = amuencha
TEMPLATE = app


SOURCES += main.cpp\
        mainwindow.cpp \
    spiraldisplay.cpp \
    frequency_analyzer.cpp \
    clickableslider.cpp

HEADERS  += mainwindow.h \
    spiraldisplay.h \
    frequency_analyzer.h \
    sse_mathfun.h \
    clickableslider.h

FORMS    += mainwindow.ui

CONFIG += c++11

QMAKE_CFLAGS_RELEASE = -O3
QMAKE_CXXFLAGS += -ffast-math

unix: CONFIG += link_pkgconfig
unix: PKGCONFIG += libavdevice libavformat libavcodec libavutil libswresample rtaudio

amuencha.path = /usr/bin
amuencha.files = amuencha

iconsvg.path = /usr/share/icons/hicolor/scalable/apps
iconsvg.files = amuencha.svg

iconpng.path = /usr/share/icons/hicolor/128x128/apps
iconpng.files = amuencha.png

desktop.path = /usr/share/applications
desktop.files = Amuencha.desktop

man.path = /usr/share/man/man1
man.files = amuencha.1.gz

INSTALLS += amuencha iconsvg iconpng desktop man


# TODO 1: make mxe use Qt5 by default
# TODO 2: translate these flags for the mxe cross-compilation
#make CXXFLAGS='-pipe -fno-keep-inline-dllexport -ffast-math -O2 -frtti -fexceptions -mthreads -Wall -Wextra -DUNICODE -DQT_DLL -DQT_NO_DEBUG -DQT_MULTIMEDIA_LIB -DQT_GUI_LIB -DQT_CORE_LIB -DQT_THREAD_SUPPORT -I/usr/local/mxe/include -I/usr/local/mxe/include/QtAV -std=c++11' LIBS='-L/usr/lib/mxe/usr/x86_64-w64-mingw32.shared/qt/lib -lmingw32 -lqtmain -lQtMultimedia4 -lQtGui4 -lQtCore4 -L/usr/local/mxe/lib -lavdevice -lavformat -lavcodec -lavutil -lswresample -lrtaudio -lole32 -lwinmm -lksuser -luuid'

