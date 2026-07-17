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

#include <gio/gio.h>
#include <glib-unix.h>

#include <memory>

#include "src/manager/manager.h"

namespace {

std::unique_ptr<gxde::dmgr::Manager> daemon_mgr;
GMainLoop* g_loop = nullptr;

void OnBusAcquired(GDBusConnection* connection, const gchar* /*name*/,
    gpointer /*user_data*/) {
  daemon_mgr = std::make_unique<gxde::dmgr::Manager>();
  if (!daemon_mgr->Start(connection)) {
    g_critical(
      "(Daemon MGR) Init: Failed to connect to daemon manager, halted!");
    g_main_loop_quit(g_loop);
    return;
  }

  g_message("(Daemon MGR) Init: Daemon manager acquired bus name %s.",
    gxde::dmgr::kBusName);
}

void OnNameLost(GDBusConnection* /*connection*/, const gchar* name,
    gpointer /*user_data*/) {
  g_warning("(Daemon MGR) Conn: Could not acquire bus name %s, halted!", name);
  g_main_loop_quit(g_loop);
}

gboolean OnTerminate(gpointer /*user_data*/) {
  g_message(
    "(Daemon MGR) Term: Terminating and stopping supervised plugin(s)...");

  if (daemon_mgr != nullptr) {
    daemon_mgr->Stop();
  }

  g_main_loop_quit(g_loop);
  return G_SOURCE_REMOVE;
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
  g_loop = g_main_loop_new(nullptr, FALSE);

  g_unix_signal_add(SIGTERM, &OnTerminate, nullptr);
  g_unix_signal_add(SIGINT, &OnTerminate, nullptr);

  guint owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
    gxde::dmgr::kBusName, G_BUS_NAME_OWNER_FLAGS_NONE,
    &OnBusAcquired, nullptr, &OnNameLost,
    nullptr, nullptr);

  g_main_loop_run(g_loop);

  if (daemon_mgr != nullptr) {
    daemon_mgr->Stop();
  }
  g_bus_unown_name(owner_id);
  daemon_mgr.reset();
  g_main_loop_unref(g_loop);

  return 0;
}
