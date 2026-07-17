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

#ifndef SRC_MANAGER_MANAGER_H_
#define SRC_MANAGER_MANAGER_H_

#include <gio/gio.h>

#include <map>
#include <string>

#include "src/manifest/manifest.h"

namespace gxde {
namespace dmgr {

inline constexpr char kBusName[] = "top.gxde.daemon.manager";
inline constexpr char kObjectPath[] = "/top/gxde/daemon/manager";
inline constexpr char kInterface[] = "top.gxde.daemon.manager";
inline constexpr char kManifestDir[] = "/usr/share/gxde-daemon/plugins";

class Manager {
 public:
  Manager() = default;
  ~Manager();

  bool Start(GDBusConnection* connection);
  void Stop();  // terminate all supervised plugins (called on logout/shutdown)

 private:
  struct Supervised {
    GPid pid = 0;
    guint child_watch = 0;
    guint restart_source = 0;
    int restarts = 0;
  };

  struct WatchContext {
    Manager* self;
    std::string name;
  };

  void Rescan();
  void StartSupervised(const Manifest& manifest);
  static void OnChildExit(GPid pid, gint status, gpointer user_data);
  static gboolean OnRestartTimeout(gpointer user_data);
  bool IsNameOwned(const std::string& bus_name) const;
  GVariant* BuildPluginInfo(const Manifest& manifest) const;

  static void OnMethodCall(GDBusConnection* connection, const gchar* sender,
    const gchar* object_path, const gchar* interface_name,
    const gchar* method_name, GVariant* parameters,
    GDBusMethodInvocation* invocation, gpointer user_data);

  GDBusConnection* connection_ = nullptr;
  guint registration_id_ = 0;
  std::map<std::string, Manifest> plugins_;
  std::map<std::string, Supervised> supervised_;
};

}  // namespace dmgr
}  // namespace gxde

#endif  // SRC_MANAGER_MANAGER_H_
