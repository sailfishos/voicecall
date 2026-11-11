TEMPLATE = app
TARGET = tst_dtmf
QT += core testlib
QT -= gui

OBJECTS_DIR = $$OUT_PWD/.obj/dtmf
MOC_DIR = $$OUT_PWD/.moc/dtmf

INCLUDEPATH += ../plugins/providers/telepathy/src

HEADERS += ../plugins/providers/telepathy/src/dtmf.h
SOURCES += tst_dtmf.cpp

target.path = /opt/tests/voicecall/telepathy

INSTALLS += target
