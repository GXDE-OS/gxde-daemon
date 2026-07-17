# Copyright (C) 2026 GXDE OS Maintainers
#
# This file is part of gxde-daemon.
#
# gxde-daemon is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

cmake_minimum_required(VERSION 3.16)
project(gxde-dock LANGUAGES CXX)

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
  wayland-client
  kywc
  xcb
  xcb-ewmh
  xcb-icccm)

add_executable(gxde-dock-daemon
  src/main.cc
  src/settings/settings.cc
  src/app_info/app_info.cc
  src/window_identify/window_identify.cc
  src/app_entry/app_entry.cc
  src/app_entries/app_entries.cc
  src/dock_manager/dock_manager.cc
  src/backend/backend_factory.cc
  src/backend/wayland_backend/wayland_backend.cc
  src/backend/x11_backend/x11_backend.cc)

target_include_directories(gxde-dock-daemon PRIVATE ${DEPS_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(gxde-dock-daemon PRIVATE ${DEPS_CFLAGS_OTHER})
target_link_libraries(gxde-dock-daemon PRIVATE ${DEPS_LIBRARIES})
target_link_directories(gxde-dock-daemon PRIVATE ${DEPS_LIBRARY_DIRS})

install(TARGETS gxde-dock-daemon
  RUNTIME DESTINATION libexec/gxde-daemon/dock)
