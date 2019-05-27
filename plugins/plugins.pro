TEMPLATE = subdirs
SUBDIRS = declarative providers playback-manager mce

enable-ngf {
    SUBDIRS += ngf
}
