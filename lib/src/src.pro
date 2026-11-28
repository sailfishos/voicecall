TEMPLATE = lib
TARGET = voicecall

QT = core

HEADERS += \
    common.h \
    voicecallmanagerinterface.h \
    abstractvoicecallhandler.h \
    abstractvoicecallprovider.h \
    abstractvoicecallmanagerplugin.h \

SOURCES += \
    abstractvoicecallhandler.cpp \
    common.cpp


# $$[QT_INSTALL_LIBS] is queried from the Qt that runs qmake, which is the host
# Qt when cross-building, so distributions need to be able to say where the
# libraries will actually live on the target.
VOICECALL_LIBDIR = $$[QT_INSTALL_LIBS]
!isEmpty(VOICECALL_INSTALL_LIBDIR): VOICECALL_LIBDIR = $${VOICECALL_INSTALL_LIBDIR}
target.path = $${VOICECALL_LIBDIR}

develheaders.path = /usr/include/voicecall
develheaders.files = $$HEADERS

INSTALLS += target develheaders

OTHER_FILES +=
