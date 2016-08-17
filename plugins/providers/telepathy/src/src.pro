include(../../../plugin.pri)
TARGET = voicecall-telepathy-plugin

PKGCONFIG += TelepathyQt5 TelepathyQt5Farstream

#DEFINES += WANT_TRACE

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
