QT += widgets network testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = ev-user-client-tests

INCLUDEPATH += $$PWD/../src
SOURCES += \
    $$PWD/client_service_test.cpp \
    $$PWD/../src/client_service.cpp \
    $$PWD/../src/socket_user_service.cpp
HEADERS += \
    $$PWD/../src/client_service.h \
    $$PWD/../src/socket_user_service.h

DESTDIR = $$OUT_PWD

include(../../../libs/protocol/protocol.pri)
