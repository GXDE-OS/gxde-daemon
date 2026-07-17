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

#ifndef SRC_APP_ENTRY_APP_ENTRY_H_
#define SRC_APP_ENTRY_APP_ENTRY_H_

#include <gio/gio.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/app_info/app_info.h"
#include "src/backend/window_backend.h"

namespace gxde {
namespace dock {

class DockManager;

class AppEntry {
 public:
  AppEntry(DockManager* manager, std::string id, std::string inner_id,
           std::shared_ptr<AppInfo> app_info);
  ~AppEntry();

  bool Export();
  void Unexport();

  const std::string& id() const { return id_; }
  const std::string& inner_id() const { return inner_id_; }
  const std::string& object_path() const { return object_path_; }
  std::shared_ptr<AppInfo> app_info() const { return app_info_; }
  void set_app_info(std::shared_ptr<AppInfo> info);
  void set_inner_id(std::string inner_id) { inner_id_ = std::move(inner_id); }

  bool is_docked() const { return is_docked_; }
  void set_docked(bool docked);

  bool has_window() const { return !windows_.empty(); }
  bool ContainsWindow(uint32_t window_id) const {
    return windows_.count(window_id) != 0;
  }
  std::vector<uint32_t> window_ids() const;
  uint32_t current_window() const { return current_window_; }

  bool AttachWindow(const BackendWindow& window);
  bool DetachWindow(uint32_t window_id);
  void UpdateWindow(const BackendWindow& window);
  void RefreshActiveState(uint32_t active_window_id);

 private:
  static void OnMethodCall(GDBusConnection* connection, const gchar* sender,
                           const gchar* object_path,
                           const gchar* interface_name,
                           const gchar* method_name, GVariant* parameters,
                           GDBusMethodInvocation* invocation,
                           gpointer user_data);
  static GVariant* OnGetProperty(GDBusConnection* connection,
                                 const gchar* sender, const gchar* object_path,
                                 const gchar* interface_name,
                                 const gchar* property_name, GError** error,
                                 gpointer user_data);

  void Activate(uint32_t timestamp);
  void LaunchApp(const std::vector<std::string>& files);
  void HandleMenuItem(const std::string& item_id);
  void CloseAllWindows();
  void ForceQuit();
  std::vector<uint32_t> AllowedCloseWindows() const;

  std::string BuildMenuJson() const;
  std::string CurrentName() const;
  std::string CurrentIcon() const;

  void UpdateName();
  void UpdateIcon();
  void UpdateWindowInfos();
  void UpdateIsActive();
  void UpdateDesktopFile();
  void UpdateMenu();
  void EmitChanged(const char* name, GVariant* value);

  DockManager* manager_;
  std::string id_;
  std::string inner_id_;
  std::string object_path_;
  std::shared_ptr<AppInfo> app_info_;
  guint registration_id_ = 0;

  std::map<uint32_t, BackendWindow> windows_;
  uint32_t current_window_ = 0;
  bool win_icon_preferred_ = false;

  bool is_active_ = false;
  bool is_docked_ = false;
  std::string name_;
  std::string icon_;
  std::string desktop_file_;
  std::string menu_json_ = "{}";
};

}  // namespace dock
}  // namespace gxde

#endif  // SRC_APP_ENTRY_APP_ENTRY_H_
