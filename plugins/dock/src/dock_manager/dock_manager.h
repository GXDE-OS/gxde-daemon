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

#ifndef SRC_DOCK_MANAGER_DOCK_MANAGER_H_
#define SRC_DOCK_MANAGER_DOCK_MANAGER_H_

#include <gio/gio.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/app_entries/app_entries.h"
#include "src/backend/window_backend.h"
#include "src/dock_types.h"
#include "src/settings/settings.h"

namespace gxde {
namespace dock {

class DockManager : public WindowObserver {
 public:
  DockManager() = default;
  ~DockManager() override;

  bool Start(GDBusConnection* connection);

  GDBusConnection* connection() const { return connection_; }
  WindowBackend* backend() const { return backend_.get(); }
  Settings* settings() { return &settings_; }
  uint32_t active_window() const { return active_window_; }

  bool DockEntry(AppEntry* entry);
  void UndockEntry(AppEntry* entry);
  void SaveDockedApps();
  void RemoveEntry(AppEntry* entry);
  bool WindowIconPreferred(const std::string& app_id) const;

  void OnWindowAdded(const BackendWindow& window) override;
  void OnWindowChanged(const BackendWindow& window) override;
  void OnWindowRemoved(uint32_t id) override;
  void OnActiveWindowChanged(uint32_t id) override;

  void EmitEntryAdded(const std::string& object_path, int32_t index);
  void EmitEntryRemoved(const std::string& entry_id);
  void EmitServiceStarted();
  void EmitServiceStopped();

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
  static gboolean OnSetProperty(GDBusConnection* connection,
                                const gchar* sender, const gchar* object_path,
                                const gchar* interface_name,
                                const gchar* property_name, GVariant* value,
                                GError** error, gpointer user_data);

  std::string AllocEntryId();
  AppEntry* AttachOrCreateEntry(const BackendWindow& window);
  void LoadDockedApps();
  std::string CreateScratchDesktop(AppEntry* entry);
  void EmitManagerPropertyChanged(const char* name, GVariant* value);

  GDBusConnection* connection_ = nullptr;
  std::unique_ptr<WindowBackend> backend_;
  Settings settings_;
  AppEntries entries_;
  guint registration_id_ = 0;
  uint32_t next_entry_id_ = 0;
  uint32_t active_window_ = 0;
  Rect frontend_window_rect_;
  HideState hide_state_ = HideState::kUnknown;
  std::map<uint32_t, std::string> identify_methods_;
};

}  // namespace dock
}  // namespace gxde

#endif  // SRC_DOCK_MANAGER_DOCK_MANAGER_H_
