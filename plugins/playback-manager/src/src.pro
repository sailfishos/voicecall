include(../../plugin.pri)
TARGET = voicecall-playback-manager-plugin
QT += dbus

DEFINES += PLUGIN_NAME=\\\"voicecall-playback-manager-plugin\\\"

enable-debug {
    DEFINES += WANT_TRACE
}

HEADERS += \
    playbackmanagerplugin.h

SOURCES += \
    playbackmanagerplugin.cpp
