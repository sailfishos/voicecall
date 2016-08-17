TEMPLATE = lib
QT = core
CONFIG += plugin link_pkgconfig c++11

QMAKE_CXXFLAGS += -std=c++0x

# includes are ok all the time, yes, really.
# it's only used for some macros.
INCLUDEPATH += $$PWD/../lib/src

!no_libvoicecall {
    LIBS += -L$$PWD/../lib/src -lvoicecall
}

# used as e.g. the declarative plugin is a QML plugin, not a voicecall plugin
!no_plugininstall {
    target.path = /usr/lib/voicecall/plugins
    INSTALLS += target
}
