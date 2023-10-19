include(../../plugin.pri)
TARGET = voicecall-ngf-plugin

PKGCONFIG += ngf-qt$${QT_MAJOR_VERSION}

DEFINES += PLUGIN_NAME=\\\"ngf-plugin\\\"

HEADERS += \
    ngfringtoneplugin.h

SOURCES += \
    ngfringtoneplugin.cpp

