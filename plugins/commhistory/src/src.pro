include(../../plugin.pri)
TARGET = voicecall-commhistory-plugin

PKGCONFIG += commhistory-qt5

DEFINES += PLUGIN_NAME=\\\"commhistory-plugin\\\"

HEADERS += \
    commhistoryplugin.h

SOURCES += \
    commhistoryplugin.cpp
