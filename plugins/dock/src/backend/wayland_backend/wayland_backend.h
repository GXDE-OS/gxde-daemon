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

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/backend/window_backend.h"

struct wl_display;
struct wl_registry;
struct kywc_toplevel_manager_v1;
struct kywc_toplevel_v1;
struct kywc_capture_manager_v1;

namespace gxde {
namespace dock {

class WaylandBackend : public WindowBackend {
 public:
  struct Toplevel {
    WaylandBackend* backend = nullptr;
    kywc_toplevel_v1* handle = nullptr;
    uint32_t id = 0;
    std::string uuid;
    std::string title;
    std::string app_id;
    std::string icon;
    uint32_t pid = 0;
    uint32_t capabilities = 0;
    uint32_t state = 0;
    Toplevel* parent = nullptr;
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool initialized = false;
    bool dirty = false;
    bool reported = false;
  };

  struct CaptureRequest {
    std::string path;
    bool done = false;
    bool ok = false;
  };

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
  BackendRect GetWindowGeometry(uint32_t id) override;
  uint32_t GetWindowGroupLeader(uint32_t id) override;
  const char* Name() const override { return "wayland"; }

  void HandleGlobal(wl_registry* registry, uint32_t name, const char* interface,
                    uint32_t version);
  void HandleNewToplevel(kywc_toplevel_v1* handle, const char* uuid);
  void HandleToplevelDone(Toplevel* toplevel);
  void HandleToplevelClosed(Toplevel* toplevel);
  void HandleManagerFinished();

 private:
  BackendWindow ToBackendWindow(const Toplevel& toplevel) const;
  Toplevel* Lookup(uint32_t id) const;
  void Flush();

  bool CaptureViaThumbnail(Toplevel* toplevel,
                           const std::string& out_png_path);

  static gboolean OnFdReadable(GIOChannel* source, GIOCondition condition,
                               gpointer data);

  wl_display* display_ = nullptr;
  wl_registry* registry_ = nullptr;
  kywc_toplevel_manager_v1* toplevel_manager_ = nullptr;
  kywc_capture_manager_v1* capture_manager_ = nullptr;
  WindowObserver* observer_ = nullptr;
  GIOChannel* io_channel_ = nullptr;
  guint io_watch_ = 0;
  uint32_t next_id_ = 1;
  uint32_t active_id_ = 0;
  std::map<uint32_t, std::unique_ptr<Toplevel>> tracked_;
};

}  // namespace dock
}  // namespace gxde

#endif  // SRC_BACKEND_WAYLAND_BACKEND_WAYLAND_BACKEND_H_
