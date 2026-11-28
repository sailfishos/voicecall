TEMPLATE = lib
QT = core
CONFIG += plugin link_pkgconfig

# includes are ok all the time, yes, really.
# it's only used for some macros.
INCLUDEPATH += $$PWD/../lib/src

LIBS += -L$$PWD/../lib/src -lvoicecall


# $$[QT_INSTALL_LIBS] is queried from the Qt that runs qmake, which is the host
# Qt when cross-building, so distributions need to be able to say where the
# libraries will actually live on the target.
VOICECALL_LIBDIR = $$[QT_INSTALL_LIBS]
!isEmpty(VOICECALL_INSTALL_LIBDIR): VOICECALL_LIBDIR = $${VOICECALL_INSTALL_LIBDIR}
target.path = $${VOICECALL_LIBDIR}/voicecall/plugins
INSTALLS += target
