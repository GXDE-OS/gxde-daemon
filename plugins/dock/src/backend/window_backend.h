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

#ifndef SRC_BACKEND_WINDOW_BACKEND_H_
#define SRC_BACKEND_WINDOW_BACKEND_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gxde {
namespace dock {

struct BackendWindow {
  uint32_t id = 0;
  std::string app_id;
  std::string wm_instance;
  std::string wm_class;
  std::string title;
  std::string icon;
  uint32_t pid = 0;
  bool minimized = false;
  bool maximized = false;
  bool active = false;
  bool skip_taskbar = false;
  bool has_parent = false;
  bool allowed_close = true;
};

class WindowObserver {
 public:
  virtual ~WindowObserver() = default;
  virtual void OnWindowAdded(const BackendWindow& window) = 0;
  virtual void OnWindowChanged(const BackendWindow& window) = 0;
  virtual void OnWindowRemoved(uint32_t id) = 0;
  virtual void OnActiveWindowChanged(uint32_t id) = 0;
};

class WindowBackend {
 public:
  virtual ~WindowBackend() = default;

  virtual bool Init(WindowObserver* observer) = 0;

  virtual std::vector<BackendWindow> ListWindows() = 0;
  virtual uint32_t ActiveWindow() = 0;

  virtual bool Activate(uint32_t id) = 0;
  virtual bool Close(uint32_t id) = 0;
  virtual bool Minimize(uint32_t id) = 0;
  virtual bool Maximize(uint32_t id) = 0;
  virtual bool MakeAbove(uint32_t id) = 0;
  virtual bool MoveWindow(uint32_t id) = 0;
  virtual bool KillClient(uint32_t id) = 0;

  virtual bool CaptureWindow(uint32_t id, const std::string& out_png_path) = 0;

  virtual const char* Name() const = 0;
};

std::unique_ptr<WindowBackend> CreateWindowBackend();

}  // namespace dock
}  // namespace gxde

#endif  // SRC_BACKEND_WINDOW_BACKEND_H_
