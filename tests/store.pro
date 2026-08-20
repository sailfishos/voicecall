TEMPLATE = app
TARGET = tst_cellbroadcast
QT += core testlib sql
QT -= gui

OBJECTS_DIR = $$OUT_PWD/.obj/store
MOC_DIR = $$OUT_PWD/.moc/store

INCLUDEPATH += ../src

HEADERS += \
    ../src/cellbroadcastcatalog.h \
    ../src/cellbroadcastgeometry.h \
    ../src/cellbroadcaststore.h

SOURCES += \
    tst_cellbroadcaststore.cpp \
    ../src/cellbroadcastcatalog.cpp \
    ../src/cellbroadcastgeometry.cpp \
    ../src/cellbroadcaststore.cpp

qtHaveModule(positioning) {
    QT += positioning
    DEFINES += HAVE_CELLBROADCAST_GEOFENCE_TESTS
    HEADERS += ../src/cellbroadcastgeofence.h
    SOURCES += ../src/cellbroadcastgeofence.cpp
}

DISTFILES += \
    data/test-catalog.json

target.path = /opt/tests/voicecall/cellbroadcast

catalog_test_data.files = data/test-catalog.json
catalog_test_data.path = /opt/tests/voicecall/cellbroadcast/data

INSTALLS += target catalog_test_data
