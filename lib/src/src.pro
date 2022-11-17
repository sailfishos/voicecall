TEMPLATE = lib
TARGET = voicecall

QT = core dbus

CONFIG += c++11

HEADERS += \
    common.h \
    voicecallmanagerinterface.h \
    abstractvoicecallhandler.h \
    abstractvoicecallprovider.h \
    abstractvoicecallmanagerplugin.h \
    dbus/voicecallmanagerdbusadapter.h \
    dbus/voicecallhandlerdbusadapter.h

SOURCES += \
    dbus/voicecallmanagerdbusadapter.cpp \
    dbus/voicecallhandlerdbusadapter.cpp \
    abstractvoicecallhandler.cpp \
    common.cpp

target.path = $$[QT_INSTALL_LIBS]

INSTALLS += target

OTHER_FILES +=
