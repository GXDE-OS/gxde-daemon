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

#include "src/dock_manager/dock_manager.h"
#include "src/dock_types.h"

namespace {

std::unique_ptr<gxde::dock::DockManager> g_manager;
GMainLoop* g_loop = nullptr;

void OnBusAcquired(GDBusConnection* connection, const gchar* /*name*/,
                   gpointer /*user_data*/) {
  g_manager = std::make_unique<gxde::dock::DockManager>();
  if (!g_manager->Start(connection)) {
    g_warning("dock: failed to start manager");
    g_main_loop_quit(g_loop);
    return;
  }
  g_manager->EmitServiceStarted();
  g_message("dock: %s acquired", gxde::dock::kBusName);
}

gboolean OnTerminate(gpointer /*user_data*/) {
  if (g_manager != nullptr) {
    g_manager->EmitServiceStopped();
  }
  g_main_loop_quit(g_loop);
  return G_SOURCE_REMOVE;
}

void OnNameLost(GDBusConnection* /*connection*/, const gchar* name,
                gpointer /*user_data*/) {
  g_warning("dock: lost or could not acquire bus name %s, exiting", name);
  g_main_loop_quit(g_loop);
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
  g_loop = g_main_loop_new(nullptr, FALSE);

  g_unix_signal_add(SIGTERM, &OnTerminate, nullptr);
  g_unix_signal_add(SIGINT, &OnTerminate, nullptr);

  guint owner_id = g_bus_own_name(G_BUS_TYPE_SESSION, gxde::dock::kBusName,
                                  G_BUS_NAME_OWNER_FLAGS_NONE, &OnBusAcquired,
                                  nullptr, &OnNameLost, nullptr, nullptr);

  g_main_loop_run(g_loop);

  g_bus_unown_name(owner_id);
  g_manager.reset();
  g_main_loop_unref(g_loop);
  return 0;
}
