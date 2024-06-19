TEMPLATE = subdirs
SUBDIRS = declarative providers playback-manager mce filter

enable-ngf {
    SUBDIRS += ngf
}
