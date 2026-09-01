QT += core
CONFIG += console c++17
TEMPLATE = app
TARGET = protocol-tests
include(../protocol.pri)
SOURCES += test_protocol.cpp
