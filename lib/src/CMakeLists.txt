
set(QT_MIN_VERSION "5.6.0")
find_package(Qt5 COMPONENTS Core DBus REQUIRED)


set(VOICECALL_LIB_SOURCES
    dbus/voicecallmanagerdbusadapter.cpp
    dbus/voicecallhandlerdbusadapter.cpp
    abstractvoicecallhandler.cpp
    common.cpp
)

set(VOICECALL_LIB_HEADERS
    common.h
    voicecallmanagerinterface.h
    abstractnotificationprovider.h
    abstractvoicecallhandler.h
    abstractvoicecallprovider.h
    abstractvoicecallmanagerplugin.h
    dbus/voicecallmanagerdbusadapter.h
    dbus/voicecallhandlerdbusadapter.h
)


add_library(voicecall SHARED ${VOICECALL_LIB_SOURCES} ${VOICECALL_LIB_HEADERS})
target_link_libraries(voicecall
    Qt5::Core
    Qt5::DBus
)

set_target_properties(voicecall PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

install(TARGETS voicecall
    LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})
