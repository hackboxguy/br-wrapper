QT += core gui qml quick
CONFIG += c++11

TARGET = network-manager-app

SOURCES += \
    main.cpp \
    NetTool.cpp \
    StatusController.cpp \
    WifiController.cpp

HEADERS += \
    NetTool.h \
    StatusController.h \
    WifiController.h

RESOURCES += qml.qrc

target.path = /usr/bin
INSTALLS += target
