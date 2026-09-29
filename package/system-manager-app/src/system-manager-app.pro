QT += core gui qml quick
CONFIG += c++11

TARGET = system-manager-app

SOURCES += \
    main.cpp \
    UpdateController.cpp \
    SystemImageController.cpp \
    FpgaController.cpp

HEADERS += \
    UpdateController.h \
    SystemImageController.h \
    SystemImageText.h \
    FpgaController.h

RESOURCES += qml.qrc

target.path = /usr/bin
INSTALLS += target
