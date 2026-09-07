QT += core testlib
TEMPLATE = app
TARGET = tst_socketparse
CONFIG += c++17 testcase
CONFIG -= app_bundle

INCLUDEPATH += $$PWD/../../../src

SOURCES += \
    tst_socketparse.cpp \
    $$PWD/../../../src/data/socketparse.cpp \
    $$PWD/../../../src/models/adminmodels.cpp

HEADERS += \
    $$PWD/../../../src/data/socketparse.h \
    $$PWD/../../../src/models/adminmodels.h \
    $$PWD/../../../src/data/adminrepository.h
