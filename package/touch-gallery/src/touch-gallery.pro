QT += quick
CONFIG += c++11

TARGET = touch-gallery
TEMPLATE = app

SOURCES += main.cpp \
           NetworkInterface.cpp \
           GalleryController.cpp \
           FpgaController.cpp

HEADERS += NetworkInterface.h \
           GalleryController.h \
           FpgaController.h

RESOURCES += qml.qrc
