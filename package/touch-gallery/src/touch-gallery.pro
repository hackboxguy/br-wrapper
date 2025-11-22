QT += quick
CONFIG += c++11

TARGET = touch-gallery
TEMPLATE = app

SOURCES += main.cpp \
           NetworkInterface.cpp \
           GalleryController.cpp

HEADERS += NetworkInterface.h \
           GalleryController.h

RESOURCES += qml.qrc
