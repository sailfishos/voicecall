include(../../plugin.pri)
TARGET = voicecall-mce-plugin

QT += dbus

DEFINES += PLUGIN_NAME=\\\"mce-plugin\\\"

HEADERS += \
    mceplugin.h

SOURCES += \
    mceplugin.cpp


