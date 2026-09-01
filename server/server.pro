QT += core network
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = ev-server
INCLUDEPATH += ../libs/protocol/include
SOURCES += src/main.cpp \
           ../libs/protocol/src/message.cpp \
           ../libs/protocol/src/frame_codec.cpp
HEADERS += ../libs/protocol/include/ev_protocol/message.h \
           ../libs/protocol/include/ev_protocol/frame_codec.h
