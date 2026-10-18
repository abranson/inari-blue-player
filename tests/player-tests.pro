QT = core dbus testlib
CONFIG += console testcase c++11
TEMPLATE = app
TARGET = player-tests
INCLUDEPATH += ../src
SOURCES += tst_player.cpp ../src/player.cpp ../src/hardware.cpp ../src/pattern.cpp
HEADERS += ../src/player.h ../src/hardware.h ../src/businterfaces.h
