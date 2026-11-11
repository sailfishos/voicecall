include(../../../plugin.pri)
TARGET = voicecall-telepathy-plugin

PKGCONFIG += TelepathyQt5 TelepathyQt5Farstream

# TelepathyQt 0.9.8's shared-ptr.h still uses the 'register' storage class,
# which C++17 removed and Clang rejects. Compilers that default to C++17
# (GCC 11+, Clang 16+) would otherwise fail on the TelepathyQt headers.
CONFIG += c++14

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
