TEMPLATE = lib
TARGET = voicecall-filter

DEFINES += FILTER_SHARED

CONFIG += shared \
          hide_symbols \
          link_pkgconfig \
          create_pc \
          create_prl \
          no_install_prl \

PKGCONFIG += mlite5 commhistory-qt5

PUBLIC_HEADERS += \
    filter.h    
          
HEADERS += \
    $$PUBLIC_HEADERS \
    filterlist.h

SOURCES += \
    filter.cpp \
    filterlist.cpp

INCLUDEPATH += $$PWD/../../../lib/src

LIBS += -L$$PWD/../../../lib/src -lvoicecall

target.path = $$[QT_INSTALL_LIBS]

develheaders.path = /usr/include/voicecall/Filter
develheaders.files = $$PUBLIC_HEADERS

QMAKE_PKGCONFIG_NAME = $$TARGET
QMAKE_PKGCONFIG_DESCRIPTION = Voice call filtering library
QMAKE_PKGCONFIG_LIBDIR = $$target.path
QMAKE_PKGCONFIG_INCDIR = /usr/include/voicecall
QMAKE_PKGCONFIG_DESTDIR = pkgconfig
QMAKE_PKGCONFIG_VERSION = $$PROJECT_VERSION

INSTALLS += target develheaders

