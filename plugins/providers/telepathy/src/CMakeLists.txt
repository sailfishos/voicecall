SET(SRC
    telepathyproviderplugin.cpp
    telepathyprovider.cpp
    farstreamchannel.cpp
    callchannelhandler.cpp
    streamchannelhandler.cpp
    basechannelhandler.cpp
)

SET(HEADERS
    telepathyproviderplugin.h
    telepathyprovider.h
    farstreamchannel.h
    callchannelhandler.h
    streamchannelhandler.h
    basechannelhandler.h
)

pkg_check_modules(TELEPATHY "TelepathyQt5 " REQUIRED)
pkg_check_modules(TELEPATHYFARSTREAM "TelepathyQt5Farstream" REQUIRED)

add_definitions(-DPLUGIN_NAME="voicecall-telepathy-plugin")

# FIXME debug
#add_definitions(-DWANT_TRACE)

include_directories(${TELEPATHY_INCLUDE_DIRS} ${TELEPATHYFARSTREAM_INCLUDE_DIRS} ${LIBVOICECALL_DIR})

add_library(voicecall-telepathy-plugin SHARED ${SRC} ${HEADERS})
add_dependencies(voicecall-telepathy-plugin voicecall)

target_link_libraries(voicecall-telepathy-plugin PUBLIC
    Qt5::Core
    Qt5::DBus
    ${TELEPATHY_LIBRARIES}
    ${TELEPATHYFARSTREAM_LIBRARIES}
    voicecall
)

install(TARGETS voicecall-telepathy-plugin
    LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/voicecall/plugins)
