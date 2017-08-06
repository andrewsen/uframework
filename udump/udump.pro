TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt
QMAKE_CXXFLAGS += -std=c++11

SOURCES += main.cpp \
    ../uasm/module.cpp \
    ../uasm/assembler.cpp

HEADERS += \
    loadablemodule.h \
    ../uasm/module.h

