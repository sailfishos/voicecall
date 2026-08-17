TEMPLATE = lib
TARGET = voicecall
VERSION = 2.0.0

QT = core

HEADERS += \
    common.h \
    voicecallmanagerinterface.h \
    abstractvoicecallhandler.h \
    abstractvoicecallprovider.h \
    abstractvoicecallmanagerplugin.h \

SOURCES += \
    abstractvoicecallhandler.cpp \
    common.cpp

target.path = $$[QT_INSTALL_LIBS]

develheaders.path = /usr/include/voicecall
develheaders.files = $$HEADERS

INSTALLS += target develheaders

OTHER_FILES +=
