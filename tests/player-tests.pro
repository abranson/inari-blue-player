QT = core dbus testlib
CONFIG += console testcase c++11
TEMPLATE = app
TARGET = player-tests
INCLUDEPATH += ../src
SOURCES += tst_player.cpp ../src/player.cpp ../src/hardware.cpp ../src/pattern.cpp
HEADERS += ../src/player.h ../src/hardware.h ../src/businterfaces.h

SOURCES += ../src/aw2023.cpp ../src/keepalive.cpp
HEADERS += ../src/aw2023.h ../src/keepalive.h
