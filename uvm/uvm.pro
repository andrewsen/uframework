TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt
QMAKE_CXXFLAGS += -std=c++11

SOURCES += main.cpp \
    configreader.cpp

HEADERS += \
    ../libuvm/runtime.h \
    configreader.h

unix:!macx: LIBS += -L$$PWD/../bin/ -luvm

#INCLUDEPATH += $$PWD/../bin
#DEPENDPATH += $$PWD/../bin
