TEMPLATE = app
TARGET = tst_dbusservice
QT += core dbus testlib
QT -= gui

OBJECTS_DIR = $$OUT_PWD/.obj/dbus
MOC_DIR = $$OUT_PWD/.moc/dbus

# Needs a session bus: run it under dbus-run-session (it skips without one).
INCLUDEPATH += fakes ../src ../src/dbus ../lib/src

HEADERS += \
    fakes/fake_voicecall.h \
    ../lib/src/abstractvoicecallhandler.h \
    ../lib/src/abstractvoicecallprovider.h \
    ../lib/src/abstractvoicecallmanagerplugin.h \
    ../lib/src/voicecallmanagerinterface.h \
    ../src/voicecallmanager.h \
    ../src/dbus/voicecallmanagerdbusservice.h \
    ../src/dbus/voicecallmanagerdbusadapter.h \
    ../src/dbus/voicecallhandlerdbusadapter.h

SOURCES += \
    tst_dbusservice.cpp \
    ../lib/src/abstractvoicecallhandler.cpp \
    ../lib/src/common.cpp \
    ../src/voicecallmanager.cpp \
    ../src/dbus/voicecallmanagerdbusservice.cpp \
    ../src/dbus/voicecallmanagerdbusadapter.cpp \
    ../src/dbus/voicecallhandlerdbusadapter.cpp

target.path = /opt/tests/voicecall/dbus

INSTALLS += target
