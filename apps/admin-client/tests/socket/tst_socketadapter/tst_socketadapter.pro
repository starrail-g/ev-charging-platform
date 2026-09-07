QT += core gui network testlib
TEMPLATE = app
TARGET = tst_socketadapter
CONFIG += c++17 testcase
CONFIG -= app_bundle

INCLUDEPATH += $$PWD/../../../src

# D1: 协议栈复用 libs/protocol（$$PWD 基准自包含）
include($$PWD/../../../../../libs/protocol/protocol.pri)

SOURCES += \
    tst_socketadapter.cpp \
    $$PWD/../../../src/data/socketadminrepository.cpp \
    $$PWD/../../../src/data/socketparse.cpp \
    $$PWD/../../../src/models/adminmodels.cpp

HEADERS += \
    $$PWD/../../../src/data/socketadminrepository.h \
    $$PWD/../../../src/data/socketparse.h \
    $$PWD/../../../src/models/adminmodels.h \
    $$PWD/../../../src/data/adminrepository.h
