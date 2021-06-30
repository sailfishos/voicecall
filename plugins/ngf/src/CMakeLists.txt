SET(SRC ngfringtoneplugin.cpp)
SET(HEADERS ngfringtoneplugin.h)

pkg_check_modules(NGF "ngf-qt5" REQUIRED)

add_definitions(-DPLUGIN_NAME="ngf-plugin")

include_directories(${NGF_INCLUDE_DIRS} ${LIBVOICECALL_DIR})

add_library(voicecall-ngf-plugin SHARED ${SRC} ${HEADERS})
add_dependencies(voicecall-ngf-plugin voicecall)

target_link_libraries(voicecall-ngf-plugin PUBLIC
    Qt5::Core
    Qt5::DBus
    ${NGF_LIBRARIES}
    voicecall
)

install(TARGETS voicecall-ngf-plugin
    LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/voicecall/plugins)
