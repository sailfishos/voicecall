TARGET = voicecall-manager
TEMPLATE = app
QT = core dbus
CONFIG += link_pkgconfig

INCLUDEPATH += ../lib/src

DEFINES += VOICECALL_PLUGIN_DIRECTORY=\"\\\"$$[QT_INSTALL_LIBS]/voicecall/plugins\\\"\"

enable-nemo-devicelock {
    PKGCONFIG += libresourceqt$${QT_MAJOR_VERSION} nemodevicelock
    DEFINES += WITH_NEMO_DEVICELOCK
}

packagesExist(qt5-boostable) {
    DEFINES += HAS_BOOSTER
    PKGCONFIG += qt5-boostable
} else {
    warning("qt5-boostable not available; startup times will be slower")
}

LIBS += -L../lib/src -lvoicecall

HEADERS += \
    dbus/voicecallmanagerdbusservice.h \
    basicvoicecallconfigurator.h \
    voicecallmanager.h \
    dbus/voicecallmanagerdbusadapter.h \
    dbus/voicecallhandlerdbusadapter.h

SOURCES += \
    dbus/voicecallmanagerdbusservice.cpp \
    dbus/voicecallmanagerdbusadapter.cpp \
    dbus/voicecallhandlerdbusadapter.cpp \
    basicvoicecallconfigurator.cpp \
    voicecallmanager.cpp \
    main.cpp \

enable-audiopolicy {
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
