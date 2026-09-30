TEMPLATE = subdirs
SUBDIRS += src lib plugins tests

plugins.depends = lib
src.depends = lib
tests.depends = lib

OTHER_FILES = LICENSE makedist rpm/voicecall-qt5.spec \
    data/ngfd/cellbroadcast_attention.ini \
    data/xpolicy/50-cellbroadcast-critical.conf


oneshot.files = oneshot/phone-move-recordings-dir
oneshot.path  = $$[QT_INSTALL_PREFIX]/lib/oneshot.d

ngfd_events.files = data/ngfd/cellbroadcast_attention.ini
ngfd_events.path = /usr/share/ngfd/events.d

xpolicy.files = data/xpolicy/50-cellbroadcast-critical.conf
xpolicy.path = /etc/pulse/xpolicy.conf.d

INSTALLS += oneshot ngfd_events xpolicy
