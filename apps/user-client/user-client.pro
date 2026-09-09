QT += widgets network concurrent webenginewidgets
CONFIG += c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = ev-user-client

INCLUDEPATH += $$PWD/src
SOURCES += \
    $$PWD/src/main.cpp \
    $$PWD/src/client_service.cpp \
    $$PWD/src/socket_user_service.cpp \
    $$PWD/src/map_service.cpp \
    $$PWD/src/server_map_service.cpp \
    $$PWD/src/map_web_view.cpp
HEADERS += \
    $$PWD/src/client_service.h \
    $$PWD/src/socket_user_service.h \
    $$PWD/src/map_types.h \
    $$PWD/src/map_service.h \
    $$PWD/src/server_map_service.h \
    $$PWD/src/map_web_view.h
RESOURCES += $$PWD/resources/map_resources.qrc

# Keep build outputs in the qmake build directory selected by the caller.
DESTDIR = $$OUT_PWD

include(../../libs/protocol/protocol.pri)
