TEMPLATE = lib
QT = core
CONFIG += plugin link_pkgconfig c++11

QMAKE_CXXFLAGS += -std=c++0x

# includes are ok all the time, yes, really.
# it's only used for some macros.
INCLUDEPATH += $$PWD/../lib/src

LIBS += -L$$PWD/../lib/src -lvoicecall

target.path = $$[QT_INSTALL_LIBS]/voicecall/plugins
INSTALLS += target
