SET(SRC mceplugin.cpp)
SET(HEADERS mceplugin.h)

add_definitions(-DPLUGIN_NAME="mce-plugin")

include_directories(${LIBVOICECALL_DIR})

add_library(voicecall-mce-plugin SHARED ${SRC} ${HEADERS})
add_dependencies(voicecall-mce-plugin voicecall)

target_link_libraries(voicecall-mce-plugin PUBLIC
    Qt5::Core
    Qt5::DBus
    voicecall
)

install(TARGETS voicecall-mce-plugin
    LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/voicecall/plugins)
