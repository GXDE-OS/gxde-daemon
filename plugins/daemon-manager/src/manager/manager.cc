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

#include <sys/prctl.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "src/manager/manager.h"

namespace gxde {
namespace dmgr {

namespace {

const char kIntrospection[] =
"<node>"
"  <interface name='top.gxde.daemon.manager'>"
"    <method name='ListPlugins'>"
"      <arg name='plugins' type='a{sv}' direction='out'/>"
"    </method>"
"    <method name='GetPlugin'>"
"      <arg name='name' type='s' direction='in'/>"
"      <arg name='info' type='a{sv}' direction='out'/>"
"    </method>"
"    <method name='Rescan'/>"
"  </interface>"
"</node>";

constexpr gint64 kPluginStopTimeoutUsec = 2 * G_USEC_PER_SEC;
constexpr guint kSessionReconcileDelayMs = 100;

const char* const kSessionEnvironmentKeys[] = {
  "DISPLAY",
  "WAYLAND_DISPLAY",
  "XAUTHORITY",
  "XDG_CURRENT_DESKTOP",
  "XDG_RUNTIME_DIR",
  "XDG_SESSION_DESKTOP",
  "XDG_SESSION_ID",
  "XDG_SESSION_TYPE",
};

GDBusInterfaceInfo* InterfaceInfo() {
  static GDBusNodeInfo* node = [] {
    GError* error = nullptr;
    GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(kIntrospection, &error);
    if (info == nullptr) {
      g_error("(Daemon MGR) FAIL: Bad introspection: %s", error->message);
    }
    return info;
  }();
  return node->interfaces[0];
}

/**
 * The sub prlugins will run in forked child processes. For those auto-closing
 * plugins, we ask the kernel to send SIGTERM if the manager, which is the
 * parent process, dies. Those "resident" plugins passes a NULL user_data, and
 * they are untouched even if the manager dies.
 */
void SetDeathSignal(gpointer user_data) {
  if (user_data != nullptr) {
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    pid_t expected_parent = static_cast<pid_t>(GPOINTER_TO_INT(user_data));
    if (getppid() != expected_parent) {
      _exit(1);
    }
  }
}

std::map<std::string, std::string> ReadProcessEnvironment(guint32 pid) {
  std::map<std::string, std::string> result;
  gchar* path = g_strdup_printf("/proc/%u/environ", pid);
  gchar* contents = nullptr;
  gsize length = 0;
  if (!g_file_get_contents(path, &contents, &length, nullptr)) {
    g_free(path);
    return result;
  }
  g_free(path);

  gsize offset = 0;
  while (offset < length) {
    const gchar* item = contents + offset;
    gsize remaining = length - offset;
    gsize item_length = strnlen(item, remaining);
    if (item_length == remaining) {
      break;
    }
    const gchar* equals = static_cast<const gchar*>(
      memchr(item, '=', item_length));
    if (equals != nullptr) {
      result.emplace(std::string(item, equals - item),
        std::string(equals + 1, item + item_length));
    }
    offset += item_length + 1;
  }
  g_free(contents);
  return result;
}

std::map<std::string, std::string> ReadSessionEnvironment(
    const std::string& session_id, guint32 leader,
    const std::string& session_type) {
  std::map<std::string, std::string> environment =
    ReadProcessEnvironment(leader);
  const char* required = session_type == "wayland"
    ? "WAYLAND_DISPLAY" : "DISPLAY";
  if (environment.count(required) != 0) {
    return environment;
  }

  // logind's Leader is often a PAM/session wrapper and may not carry display
  // variables. Find a process that actually belongs to the session instead.
  GDir* proc = g_dir_open("/proc", 0, nullptr);
  if (proc == nullptr) {
    return environment;
  }
  const gchar* name = nullptr;
  while ((name = g_dir_read_name(proc)) != nullptr) {
    bool numeric = name[0] != '\0';
    for (const char* p = name; numeric && *p != '\0'; ++p) {
      numeric = std::isdigit(static_cast<unsigned char>(*p)) != 0;
    }
    if (!numeric) {
      continue;
    }
    guint64 parsed = g_ascii_strtoull(name, nullptr, 10);
    if (parsed == 0 || parsed > G_MAXUINT32) {
      continue;
    }
    std::map<std::string, std::string> candidate =
      ReadProcessEnvironment(static_cast<guint32>(parsed));
    auto id = candidate.find("XDG_SESSION_ID");
    if (id != candidate.end() && id->second == session_id &&
        candidate.count(required) != 0) {
      g_dir_close(proc);
      return candidate;
    }
  }
  g_dir_close(proc);
  return environment;
}

}  // namespace

Manager::~Manager() {
  Stop();
  CleanupSessionTracking();
}

bool Manager::Start(GDBusConnection* connection) {
  connection_ = connection;

  static const GDBusInterfaceVTable vtable = {
    &Manager::OnMethodCall,
    nullptr,
    nullptr,
    {
      nullptr,
      nullptr,
      nullptr
    }
  };

  GError* error = nullptr;
  registration_id_ = g_dbus_connection_register_object(connection_,
    kObjectPath, InterfaceInfo(), &vtable, this, nullptr, &error);
  if (registration_id_ == 0) {
    g_warning("(Daemon MGR) Registration: Failed for %s!!", error->message);
    g_clear_error(&error);
    return false;
  }

  session_tracking_ = InitSessionTracking();
  if (session_tracking_) {
    std::optional<SessionInfo> session = FindActiveGraphicalSession();
    if (session.has_value()) {
      if (ApplySessionEnvironment(*session)) {
        active_session_id_ = session->id;
        g_message("(Daemon MGR) Session: active graphical session %s (%s).",
          session->id.c_str(), session->type.c_str());
      } else {
        session_reconcile_source_ = g_timeout_add(
          500, &Manager::OnSessionReconcile, this);
      }
    } else {
      g_message("(Daemon MGR) Session: waiting for an active graphical "
        "session.");
    }
  }

  Rescan();
  return true;
}

void Manager::Rescan() {
  plugins_.clear();

  /**
   * For debuging process you may set a GXDE_DAEMON_PLUGIN_DIR environment to
   * tese your plugin without actually installing. This environment variable
   * will override the directory provided by the plugin manifest.
   * --------------------------------------------------------------------------
   * But just keep in mind, those are just for testing purpose ONLY.
   */
  const char* dir_override = g_getenv("GXDE_DAEMON_PLUGIN_DIR");
  const std::string dir = (dir_override != nullptr && dir_override[0] != '\0')
    ? dir_override : kManifestDir;

  for (Manifest& manifest : LoadManifests(dir)) {
    std::string name = manifest.name;
    plugins_[name] = manifest;
  }

  g_message("(Daemon MGR) Plugin Discovery: Total of %zu plugin(s).",
    plugins_.size());

  StartConfiguredPlugins();
}

void Manager::StartConfiguredPlugins() {
  const bool graphical_session_active =
    !session_tracking_ || !active_session_id_.empty();

  // Till now, those "supervised" plugins are NOT running yet, launch them.
  for (const auto& [name, manifest] : plugins_) {
    if (manifest.NeedsSupervision() &&
        supervised_.find(name) == supervised_.end() &&
        (manifest.resident || graphical_session_active) &&
        !manifest.exec.empty()) {
      StartSupervised(manifest);
    }
  }

  // Oneshot plugins: Just fire-and-forget.
  for (const auto& [name, manifest] : plugins_) {
    if (manifest.oneshot &&
        graphical_session_active &&
        oneshot_launched_.find(name) == oneshot_launched_.end() &&
        !manifest.exec.empty()) {
      StartOneshot(manifest);
      oneshot_launched_.insert(name);
    }
  }
}

void Manager::StartSupervised(const Manifest& manifest) {
  // Only resident plugins may outlive a manager and be adopted. A non-resident
  // bus owner belongs to a stale graphical session and must not leak into the
  // new session.
  for (const std::string& bus : manifest.bus_names) {
    if (IsNameOwned(bus)) {
      if (manifest.resident) {
        g_message("(Daemon MGR) Plugin: %s is already running, adopting it.",
          manifest.name.c_str());
        return;
      }
      if (!EnsureNonResidentNamesFree(manifest)) {
        g_debug("(Daemon MGR) Plugin: Waiting for stale non-resident plugin "
          "%s to release its bus.", manifest.name.c_str());
        return;
      }
      break;
    }
  }

  gint argc = 0;
  gchar** argv = nullptr;
  GError* error = nullptr;
  if (!g_shell_parse_argv(manifest.exec.c_str(), &argc, &argv, &error)) {
    g_warning("(Daemon MGR) Plugin: Bad exec for %s: %s, halted!",
      manifest.name.c_str(), error->message);
    g_clear_error(&error);
    return;
  }

  // Those so-called "auto-closing" plugins gains PR_SET_PDEATHSIG (requiring
  // non-NULL pointers); Resident ones are just left untouched w/ NULLPTR.
  gpointer death_signal = manifest.resident
    ? nullptr : GINT_TO_POINTER(static_cast<gint>(getpid()));
  GPid pid = 0;
  gboolean ok = g_spawn_async(nullptr, argv, nullptr,
    static_cast<GSpawnFlags>(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH),
    &SetDeathSignal, death_signal, &pid, &error);

  g_strfreev(argv);
  if (!ok) {
    g_warning("(Daemon MGR) Plugin: Failed to spawn %s. %s",
      manifest.name.c_str(), error->message);
    g_clear_error(&error);
    return;
  }

  Supervised& sup = supervised_[manifest.name];
  sup.pid = pid;
  auto* ctx = new WatchContext{this, manifest.name};
  sup.child_watch = g_child_watch_add_full(
    G_PRIORITY_DEFAULT, pid, &Manager::OnChildExit, ctx,
    &Manager::DestroyWatchContext);
  g_message("(Daemon MGR) Plugin: Started %s (pid %d)", manifest.name.c_str(),
    pid);
}

void Manager::StartOneshot(const Manifest& manifest) {
  gint argc = 0;
  gchar** argv = nullptr;
  GError* error = nullptr;

  if (!g_shell_parse_argv(manifest.exec.c_str(), &argc, &argv, &error)) {
    g_warning("(Daemon MGR) Plugin: Bad exec for oneshot %s. %s",
      manifest.name.c_str(), error->message);
    g_clear_error(&error);
    return;
  }

  gboolean ok = g_spawn_async(nullptr, argv, nullptr,
    static_cast<GSpawnFlags>(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH),
    nullptr, nullptr, nullptr, &error);

  g_strfreev(argv);

  if (!ok) {
    g_warning("(Daemon MGR) Plugin: Failed to spawn oneshot %s. %s",
      manifest.name.c_str(), error->message);
    g_clear_error(&error);
    return;
  }

  g_message("(Daemon MGR) Plugin: Oneshot %s launched."
    "Note that we won't manage it.", manifest.name.c_str());
}

std::optional<guint32> Manager::GetNameOwnerPid(
    const std::string& bus_name) const {
  GError* error = nullptr;
  GVariant* reply = g_dbus_connection_call_sync(
      connection_, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "GetConnectionUnixProcessID",
      g_variant_new("(s)", bus_name.c_str()), G_VARIANT_TYPE("(u)"),
      G_DBUS_CALL_FLAGS_NONE, 500, nullptr, &error);
  if (reply == nullptr) {
    g_clear_error(&error);
    return std::nullopt;
  }

  guint32 pid = 0;
  g_variant_get(reply, "(u)", &pid);
  g_variant_unref(reply);
  return pid != 0 ? std::optional<guint32>(pid) : std::nullopt;
}

bool Manager::ProcessMatchesManifest(guint32 pid,
                                     const Manifest& manifest) const {
  gint argc = 0;
  gchar** argv = nullptr;
  if (!g_shell_parse_argv(manifest.exec.c_str(), &argc, &argv, nullptr) ||
      argc == 0) {
    g_strfreev(argv);
    return false;
  }
  std::string expected = argv[0];
  g_strfreev(argv);

  gchar* expected_real = realpath(expected.c_str(), nullptr);
  gchar* exe_path = g_strdup_printf("/proc/%u/exe", pid);
  gchar* actual = g_file_read_link(exe_path, nullptr);
  g_free(exe_path);
  bool matches = expected_real != nullptr && actual != nullptr &&
    expected_real == std::string(actual);
  free(expected_real);
  g_free(actual);
  if (matches) {
    return true;
  }

  // Script plugins have the interpreter in /proc/PID/exe, while argv[0]
  // still names the manifest executable.
  gchar* cmdline_path = g_strdup_printf("/proc/%u/cmdline", pid);
  gchar* cmdline = nullptr;
  gsize cmdline_length = 0;
  if (g_file_get_contents(cmdline_path, &cmdline, &cmdline_length, nullptr) &&
      cmdline_length > 0) {
    matches = expected == std::string(cmdline,
      strnlen(cmdline, cmdline_length));
  }
  g_free(cmdline);
  g_free(cmdline_path);
  return matches;
}

bool Manager::EnsureNonResidentNamesFree(const Manifest& manifest) {
  std::set<guint32> stale_pids;
  for (const std::string& bus : manifest.bus_names) {
    if (!IsNameOwned(bus)) {
      continue;
    }
    std::optional<guint32> pid = GetNameOwnerPid(bus);
    if (!pid.has_value() || *pid == static_cast<guint32>(getpid()) ||
        !ProcessMatchesManifest(*pid, manifest)) {
      g_warning("(Daemon MGR) Plugin: Bus %s has an unverified owner; it will "
        "not be terminated.", bus.c_str());
      stale_cleanups_.erase(manifest.name);
      return false;
    }
    stale_pids.insert(*pid);
  }

  if (stale_pids.empty()) {
    stale_cleanups_.erase(manifest.name);
    return true;
  }

  const gint64 now = g_get_monotonic_time();
  auto [cleanup_it, inserted] = stale_cleanups_.try_emplace(manifest.name);
  StaleCleanup& cleanup = cleanup_it->second;
  if (inserted) {
    cleanup.deadline = now + kPluginStopTimeoutUsec;
    for (guint32 pid : stale_pids) {
      g_warning("(Daemon MGR) Plugin: Terminating stale non-resident %s "
        "(pid %u).", manifest.name.c_str(), pid);
      kill(static_cast<pid_t>(pid), SIGTERM);
    }
  } else if (!cleanup.sigkill_sent && now >= cleanup.deadline) {
    // Revalidate before SIGKILL so PID reuse cannot target another process.
    for (guint32 pid : stale_pids) {
      if (ProcessMatchesManifest(pid, manifest)) {
        g_warning("(Daemon MGR) Plugin: Stale %s did not stop; killing pid %u.",
          manifest.name.c_str(), pid);
        kill(static_cast<pid_t>(pid), SIGKILL);
      }
    }
    cleanup.sigkill_sent = true;
    cleanup.deadline = now + G_USEC_PER_SEC;
  } else if (cleanup.sigkill_sent && now >= cleanup.deadline) {
    g_warning("(Daemon MGR) Plugin: Stale %s still owns its bus after "
      "SIGKILL; giving up automatic cleanup.", manifest.name.c_str());
    return false;
  }

  if (stale_cleanup_retry_source_ == 0) {
    stale_cleanup_retry_source_ = g_timeout_add(
      100, &Manager::OnStaleCleanupRetry, this);
  }
  return false;
}

gboolean Manager::OnStaleCleanupRetry(gpointer user_data) {
  auto* self = static_cast<Manager*>(user_data);
  self->stale_cleanup_retry_source_ = 0;
  self->StartConfiguredPlugins();
  return G_SOURCE_REMOVE;
}

bool Manager::InitSessionTracking() {
  GError* error = nullptr;
  system_connection_ = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
  if (system_connection_ == nullptr) {
    g_message("(Daemon MGR) Session: logind unavailable (%s); using the "
      "manager process lifetime.", error != nullptr ? error->message :
      "unknown error");
    g_clear_error(&error);
    return false;
  }

  GVariant* owner_reply = g_dbus_connection_call_sync(
    system_connection_, "org.freedesktop.DBus", "/org/freedesktop/DBus",
    "org.freedesktop.DBus", "NameHasOwner",
    g_variant_new("(s)", "org.freedesktop.login1"), G_VARIANT_TYPE("(b)"),
    G_DBUS_CALL_FLAGS_NONE, 500, nullptr, &error);
  gboolean login1_owned = FALSE;
  if (owner_reply != nullptr) {
    g_variant_get(owner_reply, "(b)", &login1_owned);
    g_variant_unref(owner_reply);
  }
  if (!login1_owned) {
    g_message("(Daemon MGR) Session: logind service unavailable; using the "
      "manager process lifetime.");
    g_clear_error(&error);
    g_clear_object(&system_connection_);
    return false;
  }

  session_properties_subscription_ = g_dbus_connection_signal_subscribe(
    system_connection_, "org.freedesktop.login1",
    "org.freedesktop.DBus.Properties", "PropertiesChanged", nullptr,
    "org.freedesktop.login1.Session", G_DBUS_SIGNAL_FLAGS_NONE,
    &Manager::OnLoginSessionChanged, this, nullptr);
  session_new_subscription_ = g_dbus_connection_signal_subscribe(
    system_connection_, "org.freedesktop.login1",
    "org.freedesktop.login1.Manager", "SessionNew",
    "/org/freedesktop/login1", nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
    &Manager::OnLoginSessionChanged, this, nullptr);
  session_removed_subscription_ = g_dbus_connection_signal_subscribe(
    system_connection_, "org.freedesktop.login1",
    "org.freedesktop.login1.Manager", "SessionRemoved",
    "/org/freedesktop/login1", nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
    &Manager::OnLoginSessionChanged, this, nullptr);
  return true;
}

void Manager::CleanupSessionTracking() {
  if (session_reconcile_source_ != 0) {
    g_source_remove(session_reconcile_source_);
    session_reconcile_source_ = 0;
  }
  if (plugin_stop_timeout_source_ != 0) {
    g_source_remove(plugin_stop_timeout_source_);
    plugin_stop_timeout_source_ = 0;
  }
  if (stale_cleanup_retry_source_ != 0) {
    g_source_remove(stale_cleanup_retry_source_);
    stale_cleanup_retry_source_ = 0;
  }
  if (system_connection_ != nullptr) {
    for (guint subscription : {session_properties_subscription_,
                               session_new_subscription_,
                               session_removed_subscription_}) {
      if (subscription != 0) {
        g_dbus_connection_signal_unsubscribe(system_connection_, subscription);
      }
    }
  }
  session_properties_subscription_ = 0;
  session_new_subscription_ = 0;
  session_removed_subscription_ = 0;
  g_clear_object(&system_connection_);
}

std::optional<Manager::SessionInfo>
Manager::FindActiveGraphicalSession() const {
  if (system_connection_ == nullptr) {
    return std::nullopt;
  }

  GError* error = nullptr;
  GVariant* reply = g_dbus_connection_call_sync(
    system_connection_, "org.freedesktop.login1",
    "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
    "ListSessions", nullptr, G_VARIANT_TYPE("(a(susso))"),
    G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &error);
  if (reply == nullptr) {
    g_warning("(Daemon MGR) Session: Cannot list logind sessions: %s",
      error != nullptr ? error->message : "unknown error");
    g_clear_error(&error);
    return std::nullopt;
  }

  std::optional<SessionInfo> selected;
  GVariantIter* sessions = nullptr;
  g_variant_get(reply, "(a(susso))", &sessions);
  const gchar* id = nullptr;
  guint32 uid = 0;
  const gchar* user = nullptr;
  const gchar* seat = nullptr;
  const gchar* path = nullptr;
  while (g_variant_iter_next(sessions, "(&su&s&s&o)", &id, &uid, &user,
                             &seat, &path)) {
    if (uid != static_cast<guint32>(getuid())) {
      continue;
    }

    GVariant* properties_reply = g_dbus_connection_call_sync(
      system_connection_, "org.freedesktop.login1", path,
      "org.freedesktop.DBus.Properties", "GetAll",
      g_variant_new("(s)", "org.freedesktop.login1.Session"),
      G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr,
      nullptr);
    if (properties_reply == nullptr) {
      continue;
    }

    bool active = false;
    bool remote = false;
    std::string type;
    std::string session_class;
    std::string display;
    guint32 leader = 0;
    GVariantIter* properties = nullptr;
    g_variant_get(properties_reply, "(a{sv})", &properties);
    gchar* key = nullptr;
    GVariant* value = nullptr;
    while (g_variant_iter_next(properties, "{sv}", &key, &value)) {
      if (strcmp(key, "Active") == 0) {
        active = g_variant_get_boolean(value);
      } else if (strcmp(key, "Remote") == 0) {
        remote = g_variant_get_boolean(value);
      } else if (strcmp(key, "Type") == 0) {
        type = g_variant_get_string(value, nullptr);
      } else if (strcmp(key, "Class") == 0) {
        session_class = g_variant_get_string(value, nullptr);
      } else if (strcmp(key, "Display") == 0) {
        display = g_variant_get_string(value, nullptr);
      } else if (strcmp(key, "Leader") == 0) {
        leader = g_variant_get_uint32(value);
      }
      g_free(key);
      g_variant_unref(value);
    }
    g_variant_iter_free(properties);
    g_variant_unref(properties_reply);

    bool graphical = type == "x11" || type == "wayland";
    bool user_session = session_class == "user" ||
      session_class.rfind("user-", 0) == 0;
    if (active && !remote && graphical && user_session) {
      selected = SessionInfo{id, path, type, display, leader};
      // A session attached to a seat is preferable to a headless active one.
      if (seat != nullptr && seat[0] != '\0') {
        break;
      }
    }
  }
  g_variant_iter_free(sessions);
  g_variant_unref(reply);
  return selected;
}

bool Manager::ApplySessionEnvironment(const SessionInfo& session) {
  std::map<std::string, std::string> environment =
    ReadSessionEnvironment(session.id, session.leader, session.type);
  const char* required = session.type == "wayland"
    ? "WAYLAND_DISPLAY" : "DISPLAY";
  if (environment.count(required) == 0 &&
      !(session.type == "x11" && !session.display.empty())) {
    g_message("(Daemon MGR) Session: Session %s is active but its display "
      "environment is not ready; retrying.", session.id.c_str());
    return false;
  }

  for (const char* key : kSessionEnvironmentKeys) {
    g_unsetenv(key);
    auto it = environment.find(key);
    if (it != environment.end() && !it->second.empty()) {
      g_setenv(key, it->second.c_str(), TRUE);
    }
  }

  g_setenv("XDG_SESSION_ID", session.id.c_str(), TRUE);
  g_setenv("XDG_SESSION_TYPE", session.type.c_str(), TRUE);
  if (g_getenv("XDG_RUNTIME_DIR") == nullptr) {
    gchar* runtime = g_strdup_printf("/run/user/%u", getuid());
    g_setenv("XDG_RUNTIME_DIR", runtime, TRUE);
    g_free(runtime);
  }
  if (session.type == "x11" && g_getenv("DISPLAY") == nullptr &&
      !session.display.empty()) {
    g_setenv("DISPLAY", session.display.c_str(), TRUE);
  }
  return true;
}

void Manager::ReconcileSession() {
  std::optional<SessionInfo> session = FindActiveGraphicalSession();
  const std::string new_id = session.has_value() ? session->id : "";
  if (new_id == active_session_id_) {
    return;
  }

  if (!active_session_id_.empty()) {
    g_message("(Daemon MGR) Session: graphical session %s ended; stopping "
      "non-resident plugins.", active_session_id_.c_str());
    StopNonResident();
    // Oneshot plugins are per graphical session when the manager is kept alive
    // by K9.
    oneshot_launched_.clear();
  }

  active_session_id_.clear();
  if (session.has_value()) {
    if (!ApplySessionEnvironment(*session)) {
      if (session_reconcile_source_ == 0) {
        session_reconcile_source_ = g_timeout_add(
          500, &Manager::OnSessionReconcile, this);
      }
      return;
    }
    active_session_id_ = new_id;
    g_message("(Daemon MGR) Session: graphical session %s (%s) became active; "
      "starting session plugins.", session->id.c_str(),
      session->type.c_str());
    StartConfiguredPlugins();
  }
}

void Manager::OnLoginSessionChanged(GDBusConnection* /*connection*/,
    const gchar* /*sender_name*/, const gchar* /*object_path*/,
    const gchar* /*interface_name*/, const gchar* /*signal_name*/,
    GVariant* /*parameters*/, gpointer user_data) {
  auto* self = static_cast<Manager*>(user_data);
  if (self->session_reconcile_source_ == 0) {
    self->session_reconcile_source_ = g_timeout_add(
      kSessionReconcileDelayMs, &Manager::OnSessionReconcile, self);
  }
}

gboolean Manager::OnSessionReconcile(gpointer user_data) {
  auto* self = static_cast<Manager*>(user_data);
  self->session_reconcile_source_ = 0;
  self->ReconcileSession();
  return G_SOURCE_REMOVE;
}

// static
void Manager::OnChildExit(GPid pid, gint status, gpointer user_data) {
  auto* ctx = static_cast<WatchContext*>(user_data);
  Manager* self = ctx->self;
  std::string name = ctx->name;

  g_spawn_close_pid(pid);

  auto it = self->supervised_.find(name);
  if (it == self->supervised_.end()) {
    return;
  }

  it->second.pid = 0;
  it->second.child_watch = 0;

  if (self->stopping_plugins_.erase(name) != 0) {
    self->supervised_.erase(it);
    if (self->stopping_plugins_.empty() &&
        self->plugin_stop_timeout_source_ != 0) {
      g_source_remove(self->plugin_stop_timeout_source_);
      self->plugin_stop_timeout_source_ = 0;
    }
    if (!self->session_tracking_ || !self->active_session_id_.empty()) {
      self->StartConfiguredPlugins();
    }
    return;
  }

  auto manifest_it = self->plugins_.find(name);
  bool session_active = !self->session_tracking_ ||
    !self->active_session_id_.empty();
  bool should_restart = manifest_it != self->plugins_.end() &&
    manifest_it->second.restart &&
    (manifest_it->second.resident || session_active);

  g_warning("(Daemon MGR) Plugin: %s exited w/ (status %d)%s", name.c_str(),
    status, should_restart ? ", now restarting..." : ", doing NOTHING.");

  if (!should_restart) {
    self->supervised_.erase(it);
    return;
  }

  it->second.restarts += 1;
  guint delay = it->second.restarts > 5 ? 5 : 1;
  auto* restart_ctx = new WatchContext{self, name};
  it->second.restart_source = g_timeout_add_seconds_full(
    G_PRIORITY_DEFAULT, delay, &Manager::OnRestartTimeout, restart_ctx,
    &Manager::DestroyWatchContext);
}

gboolean Manager::OnRestartTimeout(gpointer user_data) {
  auto* ctx = static_cast<WatchContext*>(user_data);
  Manager* self = ctx->self;
  std::string name = ctx->name;

  auto sup_it = self->supervised_.find(name);
  auto manifest_it = self->plugins_.find(name);
  if (sup_it != self->supervised_.end() &&
      manifest_it != self->plugins_.end() &&
      (manifest_it->second.resident || !self->session_tracking_ ||
       !self->active_session_id_.empty())) {
    sup_it->second.restart_source = 0;
    self->StartSupervised(manifest_it->second);
  } else if (sup_it != self->supervised_.end()) {
    self->supervised_.erase(sup_it);
  }
  return G_SOURCE_REMOVE;
}

void Manager::DestroyWatchContext(gpointer user_data) {
  delete static_cast<WatchContext*>(user_data);
}

void Manager::StopSupervised(const std::string& name, Supervised* sup) {
  if (sup->child_watch != 0) {
    g_source_remove(sup->child_watch);
    sup->child_watch = 0;
  }
  if (sup->restart_source != 0) {
    g_source_remove(sup->restart_source);
    sup->restart_source = 0;
  }
  if (sup->pid == 0) {
    return;
  }

  GPid pid = sup->pid;
  if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
    g_warning("(Daemon MGR) Plugin: Failed to stop %s (pid %d): %s",
      name.c_str(), pid, g_strerror(errno));
  }
  g_spawn_close_pid(pid);
  sup->pid = 0;
}

void Manager::StopNonResident() {
  for (auto it = supervised_.begin(); it != supervised_.end();) {
    auto manifest_it = plugins_.find(it->first);
    bool resident = manifest_it != plugins_.end() &&
      manifest_it->second.resident;
    if (resident) {
      ++it;
      continue;
    }

    Supervised& sup = it->second;
    if (sup.restart_source != 0) {
      g_source_remove(sup.restart_source);
      sup.restart_source = 0;
    }
    if (sup.pid == 0) {
      if (sup.child_watch != 0) {
        g_source_remove(sup.child_watch);
      }
      stopping_plugins_.erase(it->first);
      it = supervised_.erase(it);
      continue;
    }

    stopping_plugins_.insert(it->first);
    if (kill(sup.pid, SIGTERM) != 0 && errno != ESRCH) {
      g_warning("(Daemon MGR) Plugin: Failed to stop %s (pid %d): %s",
        it->first.c_str(), sup.pid, g_strerror(errno));
    }
    ++it;
  }

  if (!stopping_plugins_.empty()) {
    if (plugin_stop_timeout_source_ != 0) {
      g_source_remove(plugin_stop_timeout_source_);
    }
    plugin_stop_timeout_source_ = g_timeout_add(
      2000, &Manager::OnPluginStopTimeout, this);
  } else if (plugin_stop_timeout_source_ != 0) {
    g_source_remove(plugin_stop_timeout_source_);
    plugin_stop_timeout_source_ = 0;
  }
}

gboolean Manager::OnPluginStopTimeout(gpointer user_data) {
  auto* self = static_cast<Manager*>(user_data);
  self->plugin_stop_timeout_source_ = 0;
  for (const std::string& name : self->stopping_plugins_) {
    auto it = self->supervised_.find(name);
    if (it != self->supervised_.end() && it->second.pid != 0) {
      g_warning("(Daemon MGR) Plugin: %s did not stop after SIGTERM; killing "
        "pid %d.", name.c_str(), it->second.pid);
      kill(it->second.pid, SIGKILL);
    }
  }
  return G_SOURCE_REMOVE;
}

void Manager::Stop() {
  if (plugin_stop_timeout_source_ != 0) {
    g_source_remove(plugin_stop_timeout_source_);
    plugin_stop_timeout_source_ = 0;
  }
  stopping_plugins_.clear();
  for (auto& [name, sup] : supervised_) {
    auto manifest_it = plugins_.find(name);
    bool resident = manifest_it != plugins_.end() &&
      manifest_it->second.resident;
    if (resident) {
      if (sup.child_watch != 0) {
        g_source_remove(sup.child_watch);
      }
      if (sup.restart_source != 0) {
        g_source_remove(sup.restart_source);
      }
      if (sup.pid != 0) {
        g_spawn_close_pid(sup.pid);
      }
      g_message("(Daemon MGR) Plugin: Resident plugin %s is still running.",
        name.c_str());
    } else {
      StopSupervised(name, &sup);
    }
  }
  supervised_.clear();
}

bool Manager::IsNameOwned(const std::string& bus_name) const {
  GError* error = nullptr;
  GVariant* reply = g_dbus_connection_call_sync(
      connection_, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus",
      "NameHasOwner", g_variant_new("(s)", bus_name.c_str()),
      G_VARIANT_TYPE("(b)"),
      G_DBUS_CALL_FLAGS_NONE, 500, nullptr, &error);
  if (reply == nullptr) {
    g_clear_error(&error);
    return false;
  }

  gboolean owned = FALSE;
  g_variant_get(reply, "(b)", &owned);
  g_variant_unref(reply);
  return owned;
}

GVariant* Manager::BuildPluginInfo(const Manifest& manifest) const {
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&builder, "{sv}", "name", g_variant_new_string(
    manifest.name.c_str()));
  g_variant_builder_add(&builder, "{sv}", "description",
    g_variant_new_string(manifest.description.c_str()));
  g_variant_builder_add(&builder, "{sv}", "version",
    g_variant_new_string(manifest.version.c_str()));
  g_variant_builder_add(&builder, "{sv}", "exec", g_variant_new_string(
    manifest.exec.c_str()));
  g_variant_builder_add(&builder, "{sv}", "maintainer",
    g_variant_new_string(manifest.maintainer.c_str()));
  g_variant_builder_add(&builder, "{sv}", "supervise",
    g_variant_new_boolean(manifest.NeedsSupervision()));
  g_variant_builder_add(&builder, "{sv}", "resident",
    g_variant_new_boolean(manifest.resident));
  g_variant_builder_add(&builder, "{sv}", "oneshot",
    g_variant_new_boolean(manifest.oneshot));

  GVariantBuilder bus_builder;
  g_variant_builder_init(&bus_builder, G_VARIANT_TYPE("as"));
  for (const std::string& bus : manifest.bus_names) {
    g_variant_builder_add(&bus_builder, "s", bus.c_str());
  }
  g_variant_builder_add(&builder, "{sv}", "bus_names", g_variant_builder_end(
    &bus_builder));

  auto sup_it = supervised_.find(manifest.name);
  guint32 pid = (sup_it != supervised_.end()) ? static_cast<guint32>(
    sup_it->second.pid) : 0;
  bool running = pid != 0;
  if (!running) {
    for (const std::string& bus : manifest.bus_names) {
      if (IsNameOwned(bus)) {
        running = true;
        break;
      }
    }
  }
  g_variant_builder_add(&builder, "{sv}", "running", g_variant_new_boolean(
    running));
  g_variant_builder_add(&builder, "{sv}", "pid", g_variant_new_uint32(pid));

  return g_variant_builder_end(&builder);
}

void Manager::OnMethodCall(GDBusConnection* /*connection*/,
    const gchar* /*sender*/, const gchar* /*object_path*/,
    const gchar* /*interface_name*/, const gchar* method_name,
    GVariant* parameters, GDBusMethodInvocation* invocation,
    gpointer user_data) {
  auto* self = static_cast<Manager*>(user_data);
  std::string method = method_name;

  if (method == "ListPlugins") {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    for (const auto& [name, manifest] : self->plugins_) {
      g_variant_builder_add(&builder, "{sv}", name.c_str(),
        self->BuildPluginInfo(manifest));
    }
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(a{sv})",
      &builder));
  } else if (method == "GetPlugin") {
    const gchar* name = nullptr;
    g_variant_get(parameters, "(&s)", &name);
    auto it = self->plugins_.find(name != nullptr ? name : "");
    if (it == self->plugins_.end()) {
      g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
        G_DBUS_ERROR_FAILED, "No such plugin: %s", name);
      return;
    }

    GVariantBuilder wrapper;
    g_variant_builder_init(&wrapper, G_VARIANT_TYPE("(a{sv})"));
    g_variant_builder_add_value(&wrapper, self->BuildPluginInfo(it->second));
    g_dbus_method_invocation_return_value(invocation, g_variant_builder_end(
      &wrapper));
  } else if (method == "Rescan") {
    self->Rescan();
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
      G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method %s", method_name);
  }
}

}  // namespace dmgr
}  // namespace gxde
