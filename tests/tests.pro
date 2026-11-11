TEMPLATE = subdirs

store.file = store.pro
controller.file = controller.pro
manager.file = manager.pro
ofono.file = ofono.pro
dtmf.file = dtmf.pro

SUBDIRS += store controller manager ofono dtmf

tests_xml.path = /opt/tests/voicecall/cellbroadcast
tests_xml.files = tests.xml

INSTALLS += tests_xml

OTHER_FILES += tests.xml
