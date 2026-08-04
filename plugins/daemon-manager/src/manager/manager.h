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

#ifndef PLUGINS_DAEMON_MANAGER_SRC_MANAGER_MANAGER_H_
#define PLUGINS_DAEMON_MANAGER_SRC_MANAGER_MANAGER_H_

#include <gio/gio.h>

#include <map>
#include <optional>
#include <set>
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

  struct SessionInfo {
    std::string id;
    std::string path;
    std::string type;
    std::string display;
    guint32 leader = 0;
  };

  struct StaleCleanup {
    gint64 deadline = 0;
    bool sigkill_sent = false;
  };

  void Rescan();
  void StartConfiguredPlugins();
  void StartSupervised(const Manifest& manifest);
  void StartOneshot(const Manifest& manifest);
  void StopNonResident();
  void StopSupervised(const std::string& name, Supervised* supervised);
  bool EnsureNonResidentNamesFree(const Manifest& manifest);
  std::optional<guint32> GetNameOwnerPid(const std::string& bus_name) const;
  bool ProcessMatchesManifest(guint32 pid, const Manifest& manifest) const;

  bool InitSessionTracking();
  void CleanupSessionTracking();
  std::optional<SessionInfo> FindActiveGraphicalSession() const;
  bool ApplySessionEnvironment(const SessionInfo& session);
  void ReconcileSession();
  static void OnLoginSessionChanged(GDBusConnection* connection,
    const gchar* sender_name, const gchar* object_path,
    const gchar* interface_name, const gchar* signal_name,
    GVariant* parameters, gpointer user_data);
  static gboolean OnSessionReconcile(gpointer user_data);
  static gboolean OnPluginStopTimeout(gpointer user_data);
  static gboolean OnStaleCleanupRetry(gpointer user_data);

  static void OnChildExit(GPid pid, gint status, gpointer user_data);
  static gboolean OnRestartTimeout(gpointer user_data);
  static void DestroyWatchContext(gpointer user_data);
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
  std::set<std::string> oneshot_launched_;
  std::set<std::string> stopping_plugins_;
  std::map<std::string, StaleCleanup> stale_cleanups_;
  guint plugin_stop_timeout_source_ = 0;
  guint stale_cleanup_retry_source_ = 0;

  GDBusConnection* system_connection_ = nullptr;
  guint session_properties_subscription_ = 0;
  guint session_new_subscription_ = 0;
  guint session_removed_subscription_ = 0;
  guint session_reconcile_source_ = 0;
  bool session_tracking_ = false;
  std::string active_session_id_;
};

}  // namespace dmgr
}  // namespace gxde

#endif  // PLUGINS_DAEMON_MANAGER_SRC_MANAGER_MANAGER_H_
