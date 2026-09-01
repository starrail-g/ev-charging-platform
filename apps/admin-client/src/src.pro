QT += core gui widgets

TARGET = admin-client
TEMPLATE = app
CONFIG += c++17
CONFIG -= app_bundle

SOURCES += \
    main.cpp \
    app/mainwindow.cpp \
    pages/loginpage.cpp \
    pages/overviewpage.cpp \
    pages/pilepage.cpp \
    pages/stationpage.cpp \
    pages/userpage.cpp

HEADERS += \
    app/mainwindow.h \
    pages/loginpage.h \
    pages/overviewpage.h \
    pages/pilepage.h \
    pages/stationpage.h \
    pages/userpage.h

RESOURCES += \
    ../resources/admin-client.qrc
