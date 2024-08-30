TEMPLATE = subdirs
SUBDIRS = declarative providers playback-manager mce commhistory filter

enable-ngf {
    SUBDIRS += ngf
}
