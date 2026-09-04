QT += widgets network
CONFIG += c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = ev-user-client

INCLUDEPATH += $$PWD/src
SOURCES += \
    $$PWD/src/main.cpp \
    $$PWD/src/client_service.cpp \
    $$PWD/src/socket_user_service.cpp
HEADERS += \
    $$PWD/src/client_service.h \
    $$PWD/src/socket_user_service.h

# Keep build outputs in the qmake build directory selected by the caller.
DESTDIR = $$OUT_PWD

include(../../libs/protocol/protocol.pri)
