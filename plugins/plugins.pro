TEMPLATE = subdirs
SUBDIRS = declarative providers playback-manager mce commhistory

enable-ngf {
    SUBDIRS += ngf
}
