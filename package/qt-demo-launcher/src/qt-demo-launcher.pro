TARGET = qt-demo-launcher
QT += core gui widgets network
CONFIG += c++11

SOURCES += main.cpp NetworkInterface.cpp
HEADERS += NetworkInterface.h

# Note: Child apps inherit QT_QPA_PLATFORM from the launcher's environment
# No need for compile-time platform configuration

target.path = /usr/bin
INSTALLS += target
