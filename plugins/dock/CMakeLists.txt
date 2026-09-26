# Copyright (C) 2026 GXDE OS Maintainers
#
# This file is part of gxde-daemon.
#
# gxde-daemon is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

cmake_minimum_required(VERSION 3.16)
project(gxde-dock LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED
  glib-2.0
  gio-2.0
  gio-unix-2.0
  gobject-2.0
  gdk-pixbuf-2.0
  libcjson
  wayland-client
  xcb
  xcb-ewmh
  xcb-icccm)

# Generate client bindings for the gxde-wlcom (kywc) protocols we speak.
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
if(NOT WAYLAND_SCANNER)
  find_program(WAYLAND_SCANNER wayland-scanner)
endif()
if(NOT WAYLAND_SCANNER)
  message(FATAL_ERROR "wayland-scanner not found")
endif()

set(PROTOCOL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/protocols)
set(PROTOCOL_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${PROTOCOL_OUT_DIR})
set(PROTOCOL_SOURCES)
foreach(protocol kywc-toplevel-v1 kywc-capture-v1)
  set(xml ${PROTOCOL_DIR}/${protocol}.xml)
  set(header ${PROTOCOL_OUT_DIR}/${protocol}-client-protocol.h)
  set(code ${PROTOCOL_OUT_DIR}/${protocol}-protocol.c)
  add_custom_command(
    OUTPUT ${header}
    COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${header}
    DEPENDS ${xml}
    VERBATIM)
  add_custom_command(
    OUTPUT ${code}
    COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${code}
    DEPENDS ${xml}
    VERBATIM)
  list(APPEND PROTOCOL_SOURCES ${header} ${code})
endforeach()

add_executable(gxde-dock-daemon
  ${PROTOCOL_SOURCES}
  src/main.cc
  src/settings/settings.cc
  src/app_info/app_info.cc
  src/window_identify/window_identify.cc
  src/app_entry/app_entry.cc
  src/app_entries/app_entries.cc
  src/dock_manager/dock_manager.cc
  src/backend/backend_factory.cc
  src/backend/wayland_backend/wayland_backend.cc
  src/backend/x11_backend/x11_backend.cc
  src/i18n/i18n.cc)

target_include_directories(gxde-dock-daemon PRIVATE ${DEPS_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR} ${PROTOCOL_OUT_DIR})
target_compile_options(gxde-dock-daemon PRIVATE ${DEPS_CFLAGS_OTHER})
target_link_libraries(gxde-dock-daemon PRIVATE ${DEPS_LIBRARIES})
target_link_directories(gxde-dock-daemon PRIVATE ${DEPS_LIBRARY_DIRS})

# Install path for translation files.
target_compile_definitions(gxde-dock-daemon PRIVATE
  TRANSLATIONS_DIR="${CMAKE_INSTALL_PREFIX}/share/gxde-daemon/dock/translations")

install(TARGETS gxde-dock-daemon
  RUNTIME DESTINATION libexec/gxde-daemon/dock)

install(DIRECTORY translations/
  DESTINATION share/gxde-daemon/dock/translations
  FILES_MATCHING PATTERN "*.json")
