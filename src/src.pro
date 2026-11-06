TARGET = voicecall-manager
TEMPLATE = app
QT = core dbus positioning sql
CONFIG += link_pkgconfig

INCLUDEPATH += ../lib/src


# $$[QT_INSTALL_LIBS] is queried from the Qt that runs qmake, which is the host
# Qt when cross-building, so distributions need to be able to say where the
# libraries will actually live on the target.
VOICECALL_LIBDIR = $$[QT_INSTALL_LIBS]
!isEmpty(VOICECALL_INSTALL_LIBDIR): VOICECALL_LIBDIR = $${VOICECALL_INSTALL_LIBDIR}
DEFINES += VOICECALL_PLUGIN_DIRECTORY=\"\\\"$${VOICECALL_LIBDIR}/voicecall/plugins\\\"\"

PKGCONFIG += qofono-qt$${QT_MAJOR_VERSION}

# The cell broadcast settings live in dconf, which is reached through mlite.
# mlite is part of the Nemo stack and is not packaged everywhere, so use it
# when it is there and fall back to a QSettings-backed store when it is not,
# rather than dropping the feature. See cellbroadcastsettings.h.
packagesExist(mlite$${QT_MAJOR_VERSION}) {
    PKGCONFIG += mlite$${QT_MAJOR_VERSION}
    DEFINES += HAVE_MLITE
} else {
    warning("mlite not found; cell broadcast settings will be kept in a local settings file")
}

enable-nemo-devicelock {
    PKGCONFIG += nemodevicelock
    DEFINES += WITH_NEMO_DEVICELOCK
}

equals(QT_MAJOR_VERSION, 5):packagesExist(qt5-boostable) {
    DEFINES += HAS_BOOSTER
    PKGCONFIG += qt5-boostable
} else {
    warning("qt5-boostable not available; startup times will be slower")
}

LIBS += -L../lib/src -lvoicecall

HEADERS += \
    cellbroadcastcatalog.h \
    cellbroadcastsettings.h \
    cellbroadcastcontroller.h \
    cellbroadcastdaemon.h \
    cellbroadcastgeofence.h \
    cellbroadcastgeometry.h \
    cellbroadcaststore.h \
    cellbroadcasttopics.h \
    dbus/voicecallmanagerdbusservice.h \
    basicvoicecallconfigurator.h \
    voicecallmanager.h \
    dbus/voicecallmanagerdbusadapter.h \
    dbus/voicecallhandlerdbusadapter.h

SOURCES += \
    cellbroadcastcatalog.cpp \
    cellbroadcastsettings.cpp \
    cellbroadcastcontroller.cpp \
    cellbroadcastdaemon.cpp \
    cellbroadcastgeofence.cpp \
    cellbroadcastgeometry.cpp \
    cellbroadcaststore.cpp \
    cellbroadcasttopics.cpp \
    dbus/voicecallmanagerdbusservice.cpp \
    dbus/voicecallmanagerdbusadapter.cpp \
    dbus/voicecallhandlerdbusadapter.cpp \
    basicvoicecallconfigurator.cpp \
    voicecallmanager.cpp \
    main.cpp \

enable-audiopolicy {
    PKGCONFIG += libresourceqt5
    HEADERS += audiocallpolicyproxy.h
    SOURCES += audiocallpolicyproxy.cpp
    DEFINES += WITH_AUDIOPOLICY
}

target.path = /usr/bin

INSTALLS += target

OTHER_FILES += voicecall-manager.desktop voicecall-manager.service

systemd_service_entry.files = voicecall-manager.service
systemd_service_entry.path = /usr/lib/systemd/user

install-servicefiles {
    INSTALLS += systemd_service_entry
}
