TARGET = qt-demo-launcher
QT += core gui widgets network
CONFIG += c++11

SOURCES += main.cpp NetworkInterface.cpp
HEADERS += NetworkInterface.h

target.path = /usr/bin
INSTALLS += target
