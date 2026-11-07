include(../../plugin.pri)
TARGET = voicecall-commhistory-plugin

PKGCONFIG += commhistory-qt$${QT_MAJOR_VERSION}

DEFINES += PLUGIN_NAME=\\\"commhistory-plugin\\\"

HEADERS += \
    commhistoryplugin.h

SOURCES += \
    commhistoryplugin.cpp
