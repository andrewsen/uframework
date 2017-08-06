TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt
QMAKE_CXXFLAGS += -std=c++11 -O0 -g

SOURCES += main.cpp \
    assembler.cpp \
    module.cpp \
    importedmodule.cpp

HEADERS += \
    assembler.h \
    module.h \
    token.h \
    importedmodule.h \
    ../common/opcodes.h

