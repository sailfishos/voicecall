TEMPLATE = subdirs
SUBDIRS = declarative providers playback-manager mce

# commhistory-qt is not available everywhere - it is part of the Nemo stack -
# and the plugins that use it are optional, so only build them when it is
# actually installed rather than failing the whole build.
packagesExist(commhistory-qt$${QT_MAJOR_VERSION}) {
    SUBDIRS += commhistory filter
}

enable-ngf {
    SUBDIRS += ngf
}
