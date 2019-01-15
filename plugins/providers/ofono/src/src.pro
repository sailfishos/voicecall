include(../../../plugin.pri)
TARGET = voicecall-ofono-plugin
QT += dbus

PKGCONFIG += qofono-qt5

HEADERS += \
    ofonovoicecallhandler.h  \
    ofonovoicecallprovider.h \
    ofonovoicecallproviderfactory.h

SOURCES += \
    ofonovoicecallhandler.cpp \
    ofonovoicecallprovider.cpp \
    ofonovoicecallproviderfactory.cpp

DEFINES += PLUGIN_NAME=\\\"voicecall-ofono-plugin\\\"

