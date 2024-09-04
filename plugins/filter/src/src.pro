include(../../plugin.pri)
TARGET = voicecall-filter-plugin

DEFINES += PLUGIN_NAME=\\\"filter-plugin\\\"

INCLUDEPATH += $$PWD/../lib

LIBS += -L$$PWD/../lib -lvoicecall-filter

HEADERS += filterplugin.h

SOURCES += filterplugin.cpp
