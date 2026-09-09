QT += widgets network testlib webenginewidgets
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = ev-map-service-tests

INCLUDEPATH += $$PWD/../src
SOURCES += \
    $$PWD/map_service_test.cpp \
    $$PWD/../src/map_service.cpp \
    $$PWD/../src/tencent_map_service.cpp \
    $$PWD/../src/server_map_service.cpp \
    $$PWD/../src/map_web_view.cpp
HEADERS += \
    $$PWD/../src/map_types.h \
    $$PWD/../src/map_service.h \
    $$PWD/../src/tencent_map_service.h \
    $$PWD/../src/server_map_service.h \
    $$PWD/../src/map_web_view.h \
    $$PWD/../src/client_service.h
RESOURCES += $$PWD/../resources/map_resources.qrc

DESTDIR = $$OUT_PWD
include(../../../libs/protocol/protocol.pri)
# Legacy client-side Tencent adapter regression target. The production user
# client uses server-map-service-tests and never loads this adapter.
