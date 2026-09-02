QT += widgets testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = ev-user-client-tests

INCLUDEPATH += $$PWD/../src
SOURCES += \
    $$PWD/client_service_test.cpp \
    $$PWD/../src/client_service.cpp
HEADERS += \
    $$PWD/../src/client_service.h

DESTDIR = $$OUT_PWD
