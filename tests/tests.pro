TEMPLATE = subdirs

store.file = store.pro
controller.file = controller.pro

SUBDIRS += store controller

tests_xml.path = /opt/tests/voicecall/cellbroadcast
tests_xml.files = tests.xml

INSTALLS += tests_xml

OTHER_FILES += tests.xml
