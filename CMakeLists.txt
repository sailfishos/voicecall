cmake_minimum_required(VERSION 3.6.0)

project(voicecall
	VERSION 0.7.11
	DESCRIPTION "voicecall"
	LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(FeatureSummary)
include(GNUInstallDirs)

set(QT_MIN_VERSION "5.6.0")
find_package(Qt5 COMPONENTS Core Quick DBus REQUIRED)
find_package(Qt5Multimedia REQUIRED)
find_package(PkgConfig REQUIRED)


set(LIBVOICECALL_DIR "${PROJECT_SOURCE_DIR}/lib/src")

add_subdirectory(lib/src)
add_subdirectory(src)
add_subdirectory(plugins)

add_subdirectory(oneshot)
add_subdirectory(rpm)
