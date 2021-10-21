SET(SRC
    voicecallaudiorecorder.cpp
    voicecallhandler.cpp
    voicecallmanager.cpp
    voicecallmodel.cpp
    voicecallprovidermodel.cpp
    voicecallplugin.cpp
    ../../../lib/src/common.cpp
)

SET(HEADERS
    voicecallaudiorecorder.h
    voicecallhandler.h
    voicecallmanager.h
    voicecallmodel.h
    voicecallprovidermodel.h
    voicecallplugin.h
)


add_definitions(-DPLUGIN_NAME="voicecall-ofono-plugin")

# FIXME debug
#add_definitions(-DWANT_TRACE)

# if enable-ngf
    pkg_check_modules(NGF "ngf-qt5" REQUIRED)
    add_definitions(-DWITH_NGF)
# endif enable-ngf


include_directories(${NGF_INCLUDE_DIRS} ${LIBVOICECALL_DIR})

add_library(voicecalldeclarative SHARED ${SRC} ${HEADERS})
add_dependencies(voicecalldeclarative voicecall)

target_link_libraries(voicecalldeclarative PUBLIC
    Qt5::Core
    Qt5::DBus
    Qt5::Multimedia
    Qt5::Quick
    ${NGF_LIBRARIES}
    voicecall
)


#CONFIG += no_libvoicecall no_plugininstall
#TARGET = voicecall
#uri = org.nemomobile.voicecall
#OTHER_FILES += qmldir

function(FindQtInstallQml)
    find_program(QMAKE NAMES qmake-qt5 qmake)
    if(NOT QMAKE)
        message(FATAL_ERROR "qmake not found")
    endif()
    execute_process(
        COMMAND ${QMAKE} -query QT_INSTALL_QML
        OUTPUT_VARIABLE PROC_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(QT_INSTALL_QML ${PROC_RESULT} PARENT_SCOPE)
endfunction()

FindQtInstallQml()

install(FILES qmldir DESTINATION ${QT_INSTALL_QML}/org/nemomobile/voicecall/)
install(TARGETS voicecalldeclarative DESTINATION ${QT_INSTALL_QML}/org/nemomobile/voicecall/)
