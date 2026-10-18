QT = core dbus testlib
CONFIG += console testcase c++11
TEMPLATE = app
TARGET = hardware-tests
INCLUDEPATH += ../src
SOURCES = tst_hardware.cpp ../src/hardware.cpp ../src/aw2023.cpp ../src/pattern.cpp
HEADERS = ../src/hardware.h ../src/businterfaces.h
