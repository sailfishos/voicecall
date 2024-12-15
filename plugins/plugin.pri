TEMPLATE = lib
QT = core
CONFIG += plugin link_pkgconfig

# includes are ok all the time, yes, really.
# it's only used for some macros.
INCLUDEPATH += $$PWD/../lib/src

target.path = $$[QT_INSTALL_LIBS]/voicecall/plugins
INSTALLS += target
