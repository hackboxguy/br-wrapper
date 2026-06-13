QT += quick network
CONFIG += c++11

TARGET = qt-mpv-wrapper
TEMPLATE = app

SOURCES += \
    main.cpp \
    VideoPlayer.cpp \
    VideoController.cpp \
    NetworkInterface.cpp

HEADERS += \
    VideoPlayer.h \
    VideoController.h \
    NetworkInterface.h

RESOURCES += qml.qrc

# Custom target to build input-monitor C program
input_monitor.target = input-monitor
input_monitor.commands = gcc -o input-monitor input-monitor.c -Wall -O2
input_monitor.depends = input-monitor.c
QMAKE_EXTRA_TARGETS += input_monitor
PRE_TARGETDEPS += input-monitor

# Clean up input-monitor
QMAKE_CLEAN += input-monitor

# Installation path
target.path = /usr/bin/
INSTALLS += target
