SET(SRC
    ofonovoicecallhandler.cpp
    ofonovoicecallprovider.cpp
    ofonovoicecallproviderfactory.cpp
)

SET(HEADERS
    ofonovoicecallhandler.h
    ofonovoicecallprovider.h
    ofonovoicecallproviderfactory.h
)

pkg_check_modules(QOFONO "qofono-qt5 " REQUIRED)

add_definitions(-DPLUGIN_NAME="voicecall-ofono-plugin")

# FIXME debug
#add_definitions(-DWANT_TRACE)

include_directories(${QOFONO_INCLUDE_DIRS} ${LIBVOICECALL_DIR})

add_library(voicecall-ofono-plugin SHARED ${SRC} ${HEADERS})
add_dependencies(voicecall-ofono-plugin voicecall)

target_link_libraries(voicecall-ofono-plugin PUBLIC
    Qt5::Core
    Qt5::DBus
    ${QOFONO_LIBRARIES}
    voicecall
)

install(TARGETS voicecall-ofono-plugin
    LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/voicecall/plugins)
