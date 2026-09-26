QT += core gui qml quick
CONFIG += c++11

TARGET = system-manager-app

SOURCES += \
    main.cpp \
    UpdateController.cpp

HEADERS += \
    UpdateController.h

RESOURCES += qml.qrc

target.path = /usr/bin
INSTALLS += target
