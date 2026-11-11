TEMPLATE = app
TARGET = tst_recordingfile
QT += core testlib
QT -= gui

OBJECTS_DIR = $$OUT_PWD/.obj/recordingfile
MOC_DIR = $$OUT_PWD/.moc/recordingfile

INCLUDEPATH += ../plugins/declarative/src

HEADERS += ../plugins/declarative/src/recordingfile.h
SOURCES += tst_recordingfile.cpp

target.path = /opt/tests/voicecall/declarative

INSTALLS += target
