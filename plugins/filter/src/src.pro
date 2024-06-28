include(../../plugin.pri)
TARGET = voicecall-filter-plugin

PKGCONFIG += mlite5

DEFINES += PLUGIN_NAME=\\\"filter-plugin\\\"

HEADERS += \
    filterplugin.h \
    filter.h \
    filterlist.h

SOURCES += \
    filterplugin.cpp \
    filter.cpp \
    filterlist.cpp
