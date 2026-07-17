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

#ifndef SRC_BACKEND_X11_BACKEND_X11_BACKEND_H_
#define SRC_BACKEND_X11_BACKEND_X11_BACKEND_H_

#include <glib.h>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "src/backend/window_backend.h"

namespace gxde {
namespace dock {

class X11Backend : public WindowBackend {
 public:
  X11Backend() = default;
  ~X11Backend() override;

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
  const char* Name() const override { return "x11"; }

 private:
  bool ReadWindow(xcb_window_t win, BackendWindow* out);
  bool IsTaskbarWindow(xcb_window_t win);
  void SyncClientList();
  void SetActiveFromServer();
  void SendClientMessage(xcb_window_t win, xcb_atom_t type,
                         const uint32_t data[5]);
  static gboolean OnFdReadable(GIOChannel* source, GIOCondition condition,
                               gpointer data);
  void DispatchEvents();

  xcb_connection_t* conn_ = nullptr;
  xcb_ewmh_connection_t ewmh_ = {};
  xcb_window_t root_ = 0;
  int screen_number_ = 0;
  WindowObserver* observer_ = nullptr;
  GIOChannel* io_channel_ = nullptr;
  guint io_watch_ = 0;
  uint32_t active_id_ = 0;
  std::map<uint32_t, BackendWindow> windows_;
};

}  // namespace dock
}  // namespace gxde

#endif  // SRC_BACKEND_X11_BACKEND_X11_BACKEND_H_
