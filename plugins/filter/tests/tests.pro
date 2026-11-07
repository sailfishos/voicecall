include(../../plugin.pri)

TEMPLATE = app
TARGET = tst_filter
QT += testlib

PKGCONFIG += mlite$${QT_MAJOR_VERSION} commhistory-qt$${QT_MAJOR_VERSION}

SRCDIR = ../lib
INCLUDEPATH += $$SRCDIR
DEPENDPATH = $$INCLUDEPATH

HEADERS += $$SRCDIR/filter.h \
    $$SRCDIR/filterlist.h

SOURCES += tst_filter.cpp \
    $$SRCDIR/filter.cpp \
    $$SRCDIR/filterlist.cpp

target.path = /opt/tests/voicecall/filter

tests_xml.path = /opt/tests/voicecall/filter
tests_xml.files = tests.xml
INSTALLS += tests_xml

OTHER_FILES += tests.xml
