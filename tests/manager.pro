TEMPLATE = app
TARGET = tst_voicecallmanager
QT += core testlib
QT -= gui

OBJECTS_DIR = $$OUT_PWD/.obj/manager
MOC_DIR = $$OUT_PWD/.moc/manager

INCLUDEPATH += fakes ../src ../lib/src

HEADERS += \
    fakes/fake_voicecall.h \
    ../lib/src/abstractvoicecallhandler.h \
    ../lib/src/abstractvoicecallprovider.h \
    ../lib/src/voicecallmanagerinterface.h \
    ../src/voicecallmanager.h

SOURCES += \
    tst_voicecallmanager.cpp \
    ../lib/src/abstractvoicecallhandler.cpp \
    ../lib/src/common.cpp \
    ../src/voicecallmanager.cpp

target.path = /opt/tests/voicecall/manager

INSTALLS += target
