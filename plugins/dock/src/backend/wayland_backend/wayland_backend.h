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

#ifndef SRC_BACKEND_WAYLAND_BACKEND_WAYLAND_BACKEND_H_
#define SRC_BACKEND_WAYLAND_BACKEND_WAYLAND_BACKEND_H_

#include <glib.h>
#include <libkywc.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "src/backend/window_backend.h"

namespace gxde {
namespace dock {

class WaylandBackend : public WindowBackend {
 public:
  WaylandBackend() = default;
  ~WaylandBackend() override;

  bool Init(WindowObserver* observer) override;
  std::vector<BackendWindow> ListWindows() override;
  uint32_t ActiveWindow() override;

  bool Activate(uint32_t id) override;
  bool Close(uint32_t id) override;
  bool Minimize(uint32_t id) override;
  bool Maximize(uint32_t id) override;
  bool MakeAbove(uint32_t id) override;
  bool MoveWindow(uint32_t id) override;
  bool KillClient(uint32_t id) override;
  bool CaptureWindow(uint32_t id, const std::string& out_png_path) override;
  const char* Name() const override { return "wayland"; }

  void HandleNewToplevel(kywc_toplevel* toplevel);
  void HandleToplevelState(kywc_toplevel* toplevel, uint32_t mask);
  void HandleToplevelDestroy(kywc_toplevel* toplevel);

 private:
  struct Tracked {
    uint32_t id = 0;
    kywc_toplevel* toplevel = nullptr;
    bool reported = false;
  };

  struct CaptureRequest {
    std::string path;
    bool done = false;
    bool ok = false;
    bool destroyed = false;
  };

  BackendWindow ToBackendWindow(const Tracked& tracked) const;
  kywc_toplevel* Lookup(uint32_t id) const;
  void Flush();

  bool CaptureViaThumbnail(kywc_toplevel* toplevel,
                           const std::string& out_png_path);
  static bool OnThumbnailBuffer(kywc_thumbnail* thumbnail,
                                const struct kywc_thumbnail_buffer* buffer,
                                void* data);
  static void OnThumbnailDestroy(kywc_thumbnail* thumbnail, void* data);

  static gboolean OnFdReadable(GIOChannel* source, GIOCondition condition,
                               gpointer data);

  kywc_context* context_ = nullptr;
  WindowObserver* observer_ = nullptr;
  GIOChannel* io_channel_ = nullptr;
  guint io_watch_ = 0;
  uint32_t next_id_ = 1;
  uint32_t active_id_ = 0;
  std::map<uint32_t, Tracked> tracked_;
};

}  // namespace dock
}  // namespace gxde

#endif  // SRC_BACKEND_WAYLAND_BACKEND_WAYLAND_BACKEND_H_
