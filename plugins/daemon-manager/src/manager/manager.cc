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
#include <signal.h>

#include <string>

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
  }
}

}  // namespace

Manager::~Manager() {
  Stop();
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

  // Till now, those "supervised" plugins are NOT running yet, launch them.
  for (const auto& [name, manifest] : plugins_) {
    if (manifest.NeedsSupervision() &&
        supervised_.find(name) == supervised_.end() &&
        !manifest.exec.empty()) {
      StartSupervised(manifest);
    }
  }

  // Oneshot plugins: Just fire-and-forget.
  for (const auto& [name, manifest] : plugins_) {
    if (manifest.oneshot &&
        oneshot_launched_.find(name) == oneshot_launched_.end() &&
        !manifest.exec.empty()) {
      StartOneshot(manifest);
      oneshot_launched_.insert(name);
    }
  }
}

void Manager::StartSupervised(const Manifest& manifest) {
  // A so-called "resident" plugin may be already be running, probably survived
  // from previous manager instance, we just adpot them instead of relaunching.
  for (const std::string& bus : manifest.bus_names) {
    if (IsNameOwned(bus)) {
      g_message("(Daemon MGR) Plugin: %s is already running, adopting it.",
        manifest.name.c_str());
      return;
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
  gpointer death_signal = manifest.resident ? nullptr : GINT_TO_POINTER(1);
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
  sup.child_watch = g_child_watch_add(pid, &Manager::OnChildExit, ctx);
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

// static
void Manager::OnChildExit(GPid pid, gint status, gpointer user_data) {
  auto* ctx = static_cast<WatchContext*>(user_data);
  Manager* self = ctx->self;
  std::string name = ctx->name;
  delete ctx;

  g_spawn_close_pid(pid);

  auto it = self->supervised_.find(name);
  if (it == self->supervised_.end()) {
    return;
  }

  it->second.pid = 0;
  it->second.child_watch = 0;

  auto manifest_it = self->plugins_.find(name);
  bool should_restart =
      manifest_it != self->plugins_.end() && manifest_it->second.restart;

  g_warning("(Daemon MGR) Plugin: %s exited w/ (status %d)%s", name.c_str(),
    status, should_restart ? ", now restarting..." : ", doing NOTHING.");

  if (!should_restart) {
    self->supervised_.erase(it);
    return;
  }

  it->second.restarts += 1;
  guint delay = it->second.restarts > 5 ? 5 : 1;
  auto* restart_ctx = new WatchContext{self, name};
  it->second.restart_source =
    g_timeout_add_seconds(delay, &Manager::OnRestartTimeout, restart_ctx);
}

gboolean Manager::OnRestartTimeout(gpointer user_data) {
  auto* ctx = static_cast<WatchContext*>(user_data);
  Manager* self = ctx->self;
  std::string name = ctx->name;
  delete ctx;

  auto sup_it = self->supervised_.find(name);
  auto manifest_it = self->plugins_.find(name);
  if (sup_it != self->supervised_.end() &&
      manifest_it != self->plugins_.end()) {
    sup_it->second.restart_source = 0;
    self->StartSupervised(manifest_it->second);
  }
  return G_SOURCE_REMOVE;
}

void Manager::Stop() {
  for (auto& [name, sup] : supervised_) {
    if (sup.child_watch != 0) {
      g_source_remove(sup.child_watch);
      sup.child_watch = 0;
    }

    if (sup.restart_source != 0) {
      g_source_remove(sup.restart_source);
      sup.restart_source = 0;
    }

    if (sup.pid == 0) {
      continue;
    }

    auto manifest_it = plugins_.find(name);
    bool resident = manifest_it != plugins_.end() &&
      manifest_it->second.resident;
    if (resident) {
      g_spawn_close_pid(sup.pid);
      g_message("(Daemon MGR) Plugin: Resident plugin %s IS running!!",
        name.c_str());
    } else {
      kill(sup.pid, SIGTERM);
      g_spawn_close_pid(sup.pid);
    }
    sup.pid = 0;
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
