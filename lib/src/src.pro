TEMPLATE = lib
TARGET = voicecall

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

INSTALLS += target

OTHER_FILES +=
