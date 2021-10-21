SET(SRC playbackmanagerplugin.cpp)
SET(HEADERS playbackmanagerplugin.h)

add_definitions(-DPLUGIN_NAME="voicecall-playback-manager-plugin")

# FIXME if debug
# add_definitions(-DWANT_TRACE)

include_directories(${LIBVOICECALL_DIR})

add_library(voicecall-playback-manager-plugin SHARED ${SRC} ${HEADERS})
add_dependencies(voicecall-playback-manager-plugin voicecall)

target_link_libraries(voicecall-playback-manager-plugin PUBLIC
    Qt5::Core
    Qt5::DBus
    voicecall
)

install(TARGETS voicecall-playback-manager-plugin
    LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/voicecall/plugins)
