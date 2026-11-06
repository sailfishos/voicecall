TEMPLATE = app
TARGET = tst_cellbroadcastcontroller
QT += core dbus testlib
QT -= gui

OBJECTS_DIR = $$OUT_PWD/.obj/controller
MOC_DIR = $$OUT_PWD/.moc/controller

INCLUDEPATH += fakes ../src

# fakes/MDConfItem stands in for mlite, so the controller is built the way it
# is with mlite available. See ../src/cellbroadcastsettings.h.
DEFINES += HAVE_MLITE

HEADERS += \
    fakes/fake_mdconfitem.h \
    fakes/qofonocellbroadcast.h \
    ../src/cellbroadcastcatalog.h \
    ../src/cellbroadcastcontroller.h \
    ../src/cellbroadcastsettings.h \
    ../src/cellbroadcasttopics.h

SOURCES += \
    tst_cellbroadcastcontroller.cpp \
    ../src/cellbroadcastcatalog.cpp \
    ../src/cellbroadcastcontroller.cpp \
    ../src/cellbroadcasttopics.cpp

DISTFILES += \
    data/test-catalog.json \
    fakes/MDConfItem

target.path = /opt/tests/voicecall/cellbroadcast

INSTALLS += target
