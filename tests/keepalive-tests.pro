QT = core dbus testlib
CONFIG += console testcase c++11
TARGET = keepalive-tests
TEMPLATE = app

SOURCES += keepalive-tests.cpp \
           ../src/keepalive.cpp
HEADERS += ../src/keepalive.h
