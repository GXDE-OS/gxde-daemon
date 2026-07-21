cmake_minimum_required(VERSION 3.16)
project(gxde-gtk-theme-inject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED
  glib-2.0
  gio-2.0
)

add_executable(gxde-gtk-theme-inject
  src/main.cc
)

target_include_directories(gxde-gtk-theme-inject PRIVATE
  ${DEPS_INCLUDE_DIRS}
)

target_link_libraries(gxde-gtk-theme-inject PRIVATE
  ${DEPS_LIBRARIES}
)

install(TARGETS gxde-gtk-theme-inject
  RUNTIME DESTINATION libexec/gxde-daemon/gtk-theme-inject
)
