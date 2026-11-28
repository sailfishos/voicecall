TEMPLATE = app
TARGET = tst_ofonoprovider
QT += core testlib
QT -= gui

OBJECTS_DIR = $$OUT_PWD/.obj/ofono
MOC_DIR = $$OUT_PWD/.moc/ofono

PROVIDER = ../plugins/providers/ofono/src
DEFINES += PLUGIN_NAME=\\\"voicecall-ofono-plugin\\\"

# fakes/ofono stands in for libqofono
INCLUDEPATH += fakes/ofono fakes ../src ../lib/src $$PROVIDER

HEADERS += \
    fakes/ofono/fake_qofono.h \
    ../lib/src/abstractvoicecallhandler.h \
    ../lib/src/abstractvoicecallprovider.h \
    ../lib/src/abstractvoicecallmanagerplugin.h \
    ../lib/src/voicecallmanagerinterface.h \
    ../src/voicecallmanager.h \
    $$PROVIDER/ofonovoicecallhandler.h \
    $$PROVIDER/ofonovoicecallprovider.h \
    $$PROVIDER/ofonovoicecallproviderfactory.h

SOURCES += \
    tst_ofonoprovider.cpp \
    ../lib/src/abstractvoicecallhandler.cpp \
    ../lib/src/common.cpp \
    ../src/voicecallmanager.cpp \
    $$PROVIDER/ofonovoicecallhandler.cpp \
    $$PROVIDER/ofonovoicecallprovider.cpp \
    $$PROVIDER/ofonovoicecallproviderfactory.cpp

target.path = /opt/tests/voicecall/ofono

INSTALLS += target
