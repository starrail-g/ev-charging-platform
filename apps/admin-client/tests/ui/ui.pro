QT += core gui widgets testlib
TEMPLATE = app
TARGET = tst_ui
CONFIG += c++17 testcase
CONFIG -= app_bundle

INCLUDEPATH += ../../src

SOURCES += \
    tst_ui.cpp \
    ../../src/app/mainwindow.cpp \
    ../../src/pages/loginpage.cpp \
    ../../src/pages/overviewpage.cpp \
    ../../src/pages/pilepage.cpp \
    ../../src/pages/stationpage.cpp \
    ../../src/pages/userpage.cpp \
    ../../src/data/mockadminrepository.cpp \
    ../../src/data/mockdataset.cpp \
    ../../src/theme/theme.cpp \
    ../../src/widgets/aurorabackdrop.cpp \
    ../../src/widgets/metriccard.cpp \
    ../../src/widgets/statestack.cpp \
    ../../src/widgets/stationtopologywidget.cpp \
    ../../src/widgets/statuspulsewidget.cpp \
    ../../src/widgets/statustag.cpp \
    ../../src/models/adminmodels.cpp

HEADERS += \
    ../../src/app/mainwindow.h \
    ../../src/pages/loginpage.h \
    ../../src/pages/overviewpage.h \
    ../../src/pages/pilepage.h \
    ../../src/pages/stationpage.h \
    ../../src/pages/userpage.h \
    ../../src/data/adminrepository.h \
    ../../src/data/mockadminrepository.h \
    ../../src/data/mockdataset.h \
    ../../src/theme/theme.h \
    ../../src/widgets/aurorabackdrop.h \
    ../../src/widgets/metriccard.h \
    ../../src/widgets/statestack.h \
    ../../src/widgets/stationtopologywidget.h \
    ../../src/widgets/statuspulsewidget.h \
    ../../src/widgets/statustag.h \
    ../../src/models/adminmodels.h

RESOURCES += ../../resources/admin-client.qrc
