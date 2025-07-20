TARGET = qt_demo_launcher

# Qt configuration for buildroot cross-compilation
STAGING_DIR ?= $(shell dirname $(shell which $(CXX)))/../aarch64-buildroot-linux-gnu/sysroot
QT5_INCDIR = $(STAGING_DIR)/usr/include/qt5
QT5_LIBDIR = $(STAGING_DIR)/usr/lib

QT_CFLAGS = -I$(QT5_INCDIR) \
            -I$(QT5_INCDIR)/QtCore \
            -I$(QT5_INCDIR)/QtGui \
            -I$(QT5_INCDIR)/QtWidgets \
            -fPIC

QT_LIBS = -L$(QT5_LIBDIR) -lQt5Widgets -lQt5Gui -lQt5Core -lpthread

# Compiler settings
CXX ?= g++
override CXXFLAGS += -std=c++11 $(QT_CFLAGS)
override LDFLAGS += $(QT_LIBS)

# MOC settings
HOST_DIR ?= $(shell dirname $(shell which $(CXX)))/../..
MOC = $(HOST_DIR)/bin/moc

.PHONY: clean install

# Force Make to build the target by default
.DEFAULT_GOAL := $(TARGET)

# Build the target
$(TARGET): main.o
	$(CXX) main.o -o $(TARGET) $(LDFLAGS)

# Compile main.cpp (depends on MOC file being generated)
main.o: main.cpp main.moc
	$(CXX) $(CXXFLAGS) -c main.cpp -o main.o

# Generate MOC file
main.moc: main.cpp
	$(MOC) -I$(QT5_INCDIR) -I$(QT5_INCDIR)/QtCore -I$(QT5_INCDIR)/QtGui -I$(QT5_INCDIR)/QtWidgets main.cpp -o main.moc

# Clean targets
clean:
	rm -f main.o main.moc $(TARGET)

# Install
install: $(TARGET)
	install -m 755 $(TARGET) /usr/bin/
