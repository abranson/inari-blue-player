QT = core testlib
CONFIG += console testcase c++11
TEMPLATE = app
TARGET = pattern-tests
INCLUDEPATH += ../src
SOURCES += tst_pattern.cpp ../src/pattern.cpp
