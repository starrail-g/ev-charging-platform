QT += core gui widgets testlib

TEMPLATE = app
TARGET = tst_launchsmoke
CONFIG += c++17 testcase
CONFIG -= app_bundle

INCLUDEPATH += ../src

SOURCES += \
    tst_launchsmoke.cpp \
    ../src/app/mainwindow.cpp \
    ../src/pages/loginpage.cpp \
    ../src/pages/overviewpage.cpp \
    ../src/pages/pilepage.cpp \
    ../src/pages/stationpage.cpp \
    ../src/pages/userpage.cpp \
    ../src/data/mockadminrepository.cpp \
    ../src/data/mockdataset.cpp \
    ../src/models/adminmodels.cpp \
    ../src/theme/theme.cpp \
    ../src/widgets/aurorabackdrop.cpp \
    ../src/widgets/metriccard.cpp \
    ../src/widgets/statestack.cpp \
    ../src/widgets/stationtopologywidget.cpp \
    ../src/widgets/statustag.cpp

HEADERS += \
    ../src/app/mainwindow.h \
    ../src/pages/loginpage.h \
    ../src/pages/overviewpage.h \
    ../src/pages/pilepage.h \
    ../src/pages/stationpage.h \
    ../src/pages/userpage.h \
    ../src/data/adminrepository.h \
    ../src/data/mockadminrepository.h \
    ../src/data/mockdataset.h \
    ../src/models/adminmodels.h \
    ../src/theme/theme.h \
    ../src/widgets/aurorabackdrop.h \
    ../src/widgets/metriccard.h \
    ../src/widgets/statestack.h \
    ../src/widgets/stationtopologywidget.h \
    ../src/widgets/statustag.h
