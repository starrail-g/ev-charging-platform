QT += core network sql
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tencent-client-test
INCLUDEPATH += ../../libs/database/include \
               ../../libs/protocol/include \
               ../src
SOURCES += tencent_client_test.cpp \
           ../src/map/tencent_client.cpp
HEADERS += ../../libs/database/include/ev_database/database.h \
           ../../libs/protocol/include/ev_protocol/message.h \
           ../src/map/tencent_client.h
