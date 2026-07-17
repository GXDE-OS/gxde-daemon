# Copyright (C) 2026 CharOfString <root@charofstring.cc>
#
# This file is part of gxde-daemon.
#
# gxde-daemon is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# gxde-daemon is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with gxde-daemon.  If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.16)
project(gxde-daemon-manager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED glib-2.0 gio-2.0 gobject-2.0 libcjson)

add_executable(gxde-daemon-manager
  src/main.cc
  src/manifest/manifest.h
  src/manifest/manifest.cc
  src/manager/manager.h
  src/manager/manager.cc
)

target_include_directories(gxde-daemon-manager PRIVATE ${DEPS_INCLUDE_DIRS} ./)
target_compile_options(gxde-daemon-manager PRIVATE ${DEPS_CFLAGS_OTHER})
target_link_libraries(gxde-daemon-manager PRIVATE ${DEPS_LIBRARIES})
target_link_directories(gxde-daemon-manager PRIVATE ${DEPS_LIBRARY_DIRS})

install(TARGETS gxde-daemon-manager
  RUNTIME DESTINATION libexec/gxde-daemon/daemon-manager
)
