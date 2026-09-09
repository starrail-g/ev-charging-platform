QT += core gui widgets network charts

TARGET = admin-client
TEMPLATE = app
CONFIG += c++17
CONFIG -= app_bundle

# D1 协议栈复用: libs/protocol(Message/encodeFrame/FrameDecoder), 不复制
# (相对基准 = 仓库根, 与下方 UI_TOKEN_SCRIPT 的 $$PWD/../../../ 一致)
include(../../../libs/protocol/protocol.pri)

win32:UI_TOKEN_PYTHON = python
unix:UI_TOKEN_PYTHON = python3
UI_TOKEN_SCRIPT = $$system_path($$clean_path($$PWD/../../../scripts/generate_ui_tokens.py))
QMAKE_PRE_LINK += $$UI_TOKEN_PYTHON "$$UI_TOKEN_SCRIPT" --check

SOURCES += \
    main.cpp \
    app/mainwindow.cpp \
    pages/loginpage.cpp \
    pages/overviewpage.cpp \
    pages/revenuepage.cpp \
    pages/pilepage.cpp \
    pages/stationpage.cpp \
    pages/userpage.cpp \
    data/mockadminrepository.cpp \
    data/mockdataset.cpp \
    data/socketadminrepository.cpp \
    data/socketparse.cpp \
    models/adminmodels.cpp \
    theme/theme.cpp \
    widgets/aurorabackdrop.cpp \
    widgets/metriccard.cpp \
    widgets/statestack.cpp \
    widgets/stationtopologywidget.cpp \
    widgets/statusglyphwidget.cpp \
    widgets/statuspulsewidget.cpp \
    widgets/statustag.cpp \
    widgets/revenuechartwidget.cpp \
    widgets/revenuemetriccard.cpp

HEADERS += \
    app/mainwindow.h \
    pages/loginpage.h \
    pages/overviewpage.h \
    pages/revenuepage.h \
    pages/pilepage.h \
    pages/stationpage.h \
    pages/userpage.h \
    data/adminrepository.h \
    data/mockadminrepository.h \
    data/mockdataset.h \
    data/socketadminrepository.h \
    data/socketparse.h \
    models/adminmodels.h \
    theme/theme.h \
    theme/generated/theme_tokens.h \
    widgets/aurorabackdrop.h \
    widgets/metriccard.h \
    widgets/statestack.h \
    widgets/stationtopologywidget.h \
    widgets/statusglyphwidget.h \
    widgets/statuspulsewidget.h \
    widgets/statustag.h \
    widgets/revenuechartwidget.h \
    widgets/revenuemetriccard.h

RESOURCES += \
    ../resources/admin-client.qrc
