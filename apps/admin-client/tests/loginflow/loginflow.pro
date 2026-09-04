QT += core gui widgets testlib

TEMPLATE = app
TARGET = tst_loginflow
CONFIG += c++17 testcase
CONFIG -= app_bundle

INCLUDEPATH += ../../src

SOURCES += \
    tst_loginflow.cpp \
    ../../src/app/mainwindow.cpp \
    ../../src/pages/loginpage.cpp \
    ../../src/pages/overviewpage.cpp \
    ../../src/pages/pilepage.cpp \
    ../../src/pages/stationpage.cpp \
    ../../src/pages/userpage.cpp \
    ../../src/data/mockadminrepository.cpp \
    ../../src/data/mockdataset.cpp \
    ../../src/models/adminmodels.cpp \
    ../../src/widgets/statestack.cpp

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
    ../../src/models/adminmodels.h \
    ../../src/widgets/statestack.h
