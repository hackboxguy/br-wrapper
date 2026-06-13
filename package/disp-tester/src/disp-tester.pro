QT += core qml quick

CONFIG += c++11

TARGET = disp-tester

SOURCES += \
    main.cpp \
    PatternController.cpp \
    NetworkInterface.cpp \
    FpgaController.cpp

HEADERS += \
    PatternController.h \
    NetworkInterface.h \
    PatternParameters.h \
    FpgaController.h

RESOURCES += qml.qrc

# Ensure proper deployment
target.path = /usr/bin/
INSTALLS += target
