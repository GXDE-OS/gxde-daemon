/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This file is part of gxde-daemon.
 *
 * gxde-daemon is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gxde-daemon is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gxde-daemon.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <glib.h>

#include <cstdlib>
#include <memory>

#include "src/backend/wayland_backend/wayland_backend.h"
#include "src/backend/window_backend.h"
#include "src/backend/x11_backend/x11_backend.h"

namespace gxde {
namespace dock {

std::unique_ptr<WindowBackend> CreateWindowBackend() {
  const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
  if (wayland_display != nullptr && wayland_display[0] != '\0') {
    g_message("(Dock) Factory: using Wayland (libkywc) backend");
    return std::make_unique<WaylandBackend>();
  }
  g_message("(Dock) Factory: using X11 backend");
  return std::make_unique<X11Backend>();
}

}  // namespace dock
}  // namespace gxde
