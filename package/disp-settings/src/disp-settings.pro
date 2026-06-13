QT += core gui qml quick quickcontrols2 network

CONFIG += c++11

TARGET = disp-settings
TEMPLATE = app

SOURCES += main.cpp \
    AlsDimmerController.cpp \
    DualDisplayAbsoluteController.cpp \
    FpgaController.cpp \
    TemperatureController.cpp \
    TddiController.cpp \
    McuController.cpp \
    PmicController.cpp

HEADERS += config.h \
    AlsDimmerController.h \
    DualDisplayAbsoluteController.h \
    FpgaController.h \
    TemperatureController.h \
    TddiController.h \
    McuController.h \
    PmicController.h

RESOURCES += qml.qrc

# Default install path
target.path = /usr/bin
INSTALLS += target

# Config file installation
config.files = disp-settings.json
config.path = /usr/share/qt-apps
INSTALLS += config
