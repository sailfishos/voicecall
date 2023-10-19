include(../../../plugin.pri)
TARGET = voicecall-telepathy-plugin

PKGCONFIG += TelepathyQt$${QT_MAJOR_VERSION} TelepathyQt$${QT_MAJOR_VERSION}Farstream gstreamer-1.0 gstreamer-base-1.0 gobject-2.0 glib-2.0 farstream-0.2 telepathy-farstream

#DEFINES += WANT_TRACE

QT += dbus

HEADERS += \
    telepathyproviderplugin.h \
    telepathyprovider.h \
    farstreamchannel.h \
    callchannelhandler.h \
    streamchannelhandler.h \
    basechannelhandler.h

SOURCES += \
    telepathyproviderplugin.cpp \
    telepathyprovider.cpp \
    farstreamchannel.cpp \
    callchannelhandler.cpp \
    streamchannelhandler.cpp \
    basechannelhandler.cpp

DEFINES += PLUGIN_NAME=\\\"voicecall-telepathy-plugin\\\"
