TARGET = voicecall-manager
TEMPLATE = app
QT = core dbus
CONFIG += link_pkgconfig

INCLUDEPATH += ../lib/src

DEFINES += VOICECALL_PLUGIN_DIRECTORY=\"\\\"$$[QT_INSTALL_LIBS]/voicecall/plugins\\\"\"

PKGCONFIG += mlite$${QT_MAJOR_VERSION} qofono-qt$${QT_MAJOR_VERSION}

enable-nemo-devicelock {
    PKGCONFIG += nemodevicelock
    DEFINES += WITH_NEMO_DEVICELOCK
}

packagesExist(qt$${QT_MAJOR_VERSION}-boostable) {
    DEFINES += HAS_BOOSTER
    PKGCONFIG += qt$${QT_MAJOR_VERSION}-boostable
} else {
    warning("qt$${QT_MAJOR_VERSION}-boostable not available; startup times will be slower")
}

LIBS += -L../lib/src -lvoicecall

HEADERS += \
    cellbroadcastcatalog.h \
    cellbroadcastcontroller.h \
    cellbroadcastdaemon.h \
    cellbroadcasttopics.h \
    dbus/voicecallmanagerdbusservice.h \
    basicvoicecallconfigurator.h \
    voicecallmanager.h \
    dbus/voicecallmanagerdbusadapter.h \
    dbus/voicecallhandlerdbusadapter.h

SOURCES += \
    cellbroadcastcatalog.cpp \
    cellbroadcastcontroller.cpp \
    cellbroadcastdaemon.cpp \
    cellbroadcasttopics.cpp \
    dbus/voicecallmanagerdbusservice.cpp \
    dbus/voicecallmanagerdbusadapter.cpp \
    dbus/voicecallhandlerdbusadapter.cpp \
    basicvoicecallconfigurator.cpp \
    voicecallmanager.cpp \
    main.cpp \

enable-audiopolicy {
    PKGCONFIG += libresourceqt$${QT_MAJOR_VERSION}
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
