TEMPLATE = subdirs

store.file = store.pro
controller.file = controller.pro
manager.file = manager.pro
ofono.file = ofono.pro
dtmf.file = dtmf.pro
recordingfile.file = recordingfile.pro
dbus.file = dbus.pro

SUBDIRS += store controller manager ofono dtmf recordingfile dbus

tests_xml.path = /opt/tests/voicecall/cellbroadcast
tests_xml.files = tests.xml

INSTALLS += tests_xml

OTHER_FILES += tests.xml \
    hardening/README \
    hardening/run \
    hardening/static-analysis \
    hardening/static-analysis.baseline
