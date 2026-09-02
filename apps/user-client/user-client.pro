QT += widgets
CONFIG += c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = ev-user-client

INCLUDEPATH += $$PWD/src
SOURCES += \
    $$PWD/src/main.cpp \
    $$PWD/src/client_service.cpp
HEADERS += \
    $$PWD/src/client_service.h

# Keep build outputs in the qmake build directory selected by the caller.
DESTDIR = $$OUT_PWD
