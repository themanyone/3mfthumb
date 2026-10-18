# KDE thumbnail plugin for 3mfthumb. Build only when Qt 6 and KF6 KIO are
# installed (KDE boxes); the command-line tool is built by the Makefile and
# does not need either.
#
#   cmake -S . -B build-kde -DCMAKE_INSTALL_PREFIX=/usr
#   cmake --build build-kde
#   sudo cmake --install build-kde
#
# Installs lib3mfthumbnail.so into the KDE thumbnail plugin directory
# (thumbcreator/) and 3mfthumbnail.json next to it, where KIO picks both up.

cmake_minimum_required(VERSION 3.16)
project(3mfthumbnail LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
# KF6's packages are per library (KF6KIO); the KF6 umbrella config is not
# shipped on Fedora.
find_package(KF6KIO REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIB3MF REQUIRED IMPORTED_TARGET lib3mf)

add_library(3mfthumbnail MODULE thumbcreator.cpp)
set_target_properties(3mfthumbnail PROPERTIES OUTPUT_NAME 3mfthumbnail)
target_include_directories(3mfthumbnail PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(3mfthumbnail PRIVATE
    Qt6::Core
    Qt6::Gui
    KF6::KIOGui
    PkgConfig::LIB3MF
)

include(GNUInstallDirs)
if(DEFINED KDE_INSTALL_PLUGINDIR)
    set(THUMBCREATOR_DIR "${KDE_INSTALL_PLUGINDIR}/thumbcreator")
else()
    # Qt's own plugin location, also used by Debian and Ubuntu, which do not
    # ship ECM on every KIO install.
    set(THUMBCREATOR_DIR "${CMAKE_INSTALL_LIBDIR}/qt6/plugins/thumbcreator")
endif()

install(TARGETS 3mfthumbnail LIBRARY DESTINATION ${THUMBCREATOR_DIR})
install(FILES 3mfthumbnail.json DESTINATION ${THUMBCREATOR_DIR})
