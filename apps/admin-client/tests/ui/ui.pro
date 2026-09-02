QT += core gui widgets testlib
TEMPLATE = app
TARGET = tst_ui
CONFIG += c++17 testcase
CONFIG -= app_bundle

INCLUDEPATH += ../../src

SOURCES += \
    tst_ui.cpp \
    ../../src/theme/theme.cpp \
    ../../src/widgets/statuspulsewidget.cpp \
    ../../src/widgets/statustag.cpp \
    ../../src/models/adminmodels.cpp

HEADERS += \
    ../../src/theme/theme.h \
    ../../src/widgets/statuspulsewidget.h \
    ../../src/widgets/statustag.h \
    ../../src/models/adminmodels.h

RESOURCES += ../../resources/admin-client.qrc
