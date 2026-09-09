QT += core sql
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = simulation-gateway-test
INCLUDEPATH += ../../libs/database/include \
               ../../libs/protocol/include \
               ../src
SOURCES += simulation_gateway_test.cpp \
           ../../libs/database/src/database.cpp \
           ../../libs/database/src/pile_generator.cpp \
           ../src/map/simulation_gateway.cpp
HEADERS += ../../libs/database/include/ev_database/database.h \
           ../../libs/database/include/ev_database/pile_generator.h \
           ../src/map/simulation_gateway.h
