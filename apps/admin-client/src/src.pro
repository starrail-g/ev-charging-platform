QT += core gui widgets

TARGET = admin-client
TEMPLATE = app
CONFIG += c++17
CONFIG -= app_bundle

win32:UI_TOKEN_PYTHON = python
unix:UI_TOKEN_PYTHON = python3
UI_TOKEN_SCRIPT = $$system_path($$clean_path($$PWD/../../../scripts/generate_ui_tokens.py))
QMAKE_PRE_LINK += $$UI_TOKEN_PYTHON "$$UI_TOKEN_SCRIPT" --check

SOURCES += \
    main.cpp \
    app/mainwindow.cpp \
    pages/loginpage.cpp \
    pages/overviewpage.cpp \
    pages/pilepage.cpp \
    pages/stationpage.cpp \
    pages/userpage.cpp \
    data/mockadminrepository.cpp \
    data/mockdataset.cpp \
    models/adminmodels.cpp \
    widgets/statestack.cpp

HEADERS += \
    app/mainwindow.h \
    pages/loginpage.h \
    pages/overviewpage.h \
    pages/pilepage.h \
    pages/stationpage.h \
    pages/userpage.h \
    data/adminrepository.h \
    data/mockadminrepository.h \
    data/mockdataset.h \
    models/adminmodels.h \
    widgets/statestack.h

RESOURCES += \
    ../resources/admin-client.qrc
