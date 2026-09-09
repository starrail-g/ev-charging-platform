QT += core network sql
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = ev-server
INCLUDEPATH += ../libs/protocol/include \
               ../libs/database/include
SOURCES += src/main.cpp \
           ../libs/protocol/src/message.cpp \
           ../libs/protocol/src/frame_codec.cpp \
           ../libs/database/src/database.cpp \
           ../libs/database/src/pile_generator.cpp \
           src/map/tencent_client.cpp \
           src/map/map_service.cpp \
           src/map/simulation_gateway.cpp
HEADERS += ../libs/protocol/include/ev_protocol/message.h \
           ../libs/protocol/include/ev_protocol/frame_codec.h \
           ../libs/database/include/ev_database/database.h \
           ../libs/database/include/ev_database/pile_generator.h \
           src/map/tencent_client.h \
           src/map/map_service.h \
           src/map/simulation_gateway.h
