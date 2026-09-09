QT += core network testlib concurrent
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = ev-server-map-service-tests

INCLUDEPATH += $$PWD/../src
SOURCES += \
    $$PWD/server_map_service_test.cpp \
    $$PWD/../src/server_map_service.cpp \
    $$PWD/../src/map_service.cpp
HEADERS += \
    $$PWD/../src/server_map_service.h \
    $$PWD/../src/map_service.h \
    $$PWD/../src/map_types.h \
    $$PWD/../src/client_service.h
DESTDIR = $$OUT_PWD
include(../../../libs/protocol/protocol.pri)
