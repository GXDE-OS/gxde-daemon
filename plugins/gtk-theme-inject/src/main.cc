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
 * ----------------------------------------------------------------------------
 * This plugin reads GTK icon & theme, then injects them into WM D-Bus.
 * Note that only gxde-wlcom is supported.
 */

#include <gio/gio.h>
#include <glib.h>

#include <cstdlib>
#include <string>

namespace {

constexpr char kWmService[] = "com.kylin.Wlcom.Theme";
constexpr char kWmPath[] = "/com/kylin/Wlcom/Theme";
constexpr char kWmInterface[] = "com.kylin.Wlcom.Theme";

constexpr char kSchemaInterface[] = "org.gnome.desktop.interface";
constexpr char kKeyGtkTheme[] = "gtk-theme";
constexpr char kKeyIconTheme[] = "icon-theme";
constexpr char kKeyColorScheme[] = "color-scheme";

// The enum valur of light/dark mode is defined in gxde-wlcom's source.
// Please refer to include/theme.h.
constexpr guint32 kThemeTypeLight = 0;
constexpr guint32 kThemeTypeDark = 1;

constexpr int kMaxRetries = 10;
constexpr int kRetryDelayUs = 1000000;

// Helper: Check if running under Wayland.
bool IsWaylandSession() {
  const char* wayland_display = g_getenv("WAYLAND_DISPLAY");

  if (!wayland_display) {
    return false;
  } else if (wayland_display[0] == '\0') {
    return false;
  }

  return true;
}

// Helper: call a D-Bus method synchronously.
gboolean CallMethod(GDBusConnection* connection, const gchar* service,
    const gchar* path, const gchar* interface_name, const gchar* method,
    GVariant* params, GVariant** out_reply, GError** error) {
  *out_reply = g_dbus_connection_call_sync(connection, service, path,
    interface_name, method, params, nullptr, G_DBUS_CALL_FLAGS_NONE, 1000,
    nullptr, error);
  return *out_reply != nullptr;
}

// Inject read GTK icon/theme into WM D-Bus.
void InjectTheme() {
  GSettings* settings = g_settings_new(kSchemaInterface);

  g_autofree gchar* gtk_theme = g_settings_get_string(settings, kKeyGtkTheme);
  g_autofree gchar* icon_theme = g_settings_get_string(settings,
    kKeyIconTheme);
  g_autofree gchar* color_scheme = g_settings_get_string(settings,
    kKeyColorScheme);

  g_object_unref(settings);

  bool is_dark = (g_strcmp0(color_scheme, "prefer-dark") == 0);
  guint32 theme_type = is_dark ? kThemeTypeDark : kThemeTypeLight;

  if (gtk_theme == nullptr || gtk_theme[0] == '\0') {
    g_warning("(GTK Theme) Inject: No GTK theme configured, aborted!");
    return;
  }

  g_message("(GTK Theme) Inject: GTK theme: %s, icon: %s, %s",
    gtk_theme, icon_theme != nullptr ? icon_theme : "(default)",
    is_dark ? "dark" : "light");

  GError* error = nullptr;
  GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr,
                                               &error);
  if (connection == nullptr) {
    g_warning(
      "(GTK Theme) Inject: Cannot connect to session bus: %s, aborted!!",
      error->message);

    g_clear_error(&error);
    return;
  }

  for (int retry = 0; retry < kMaxRetries; ++retry) {
    g_clear_error(&error);

    // Set GTK theme
    GVariant* reply = nullptr;
    g_autoptr(GVariant) params = g_variant_new("(su)", gtk_theme, theme_type);
    if (CallMethod(connection, kWmService, kWmPath, kWmInterface,
        "SetWidgetTheme", params, &reply, &error)) {
      g_variant_unref(reply);
      g_message("(GTK Theme) Inject: GTK theme \"%s\" (type %u).",
        gtk_theme, theme_type);

      // 2) Set icon theme.
      if (icon_theme != nullptr && icon_theme[0] != '\0') {
        g_clear_error(&error);
        g_autoptr(GVariant) icon_params = g_variant_new("(s)", icon_theme);
        GVariant* icon_reply = nullptr;
        if (CallMethod(connection, kWmService, kWmPath, kWmInterface,
            "SetIconTheme", icon_params, &icon_reply, &error)) {
          g_variant_unref(icon_reply);
          g_message("(GTK Theme) Inject: Icon theme \"%s\".", icon_theme);
        } else {
          g_warning("(GTK Theme) Inject: Failed to set icon. %s",
            error->message);
        }
      }

      break;
    }

    // Distinguish "service not running" from other errors.
    if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER) ||
          g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN)) {
      g_message("(GTK Theme) Inject: WM not ready yet (retry %d/%d)...",
        retry + 1, kMaxRetries);
      g_usleep(kRetryDelayUs);
      continue;
    }

    g_warning("(GTK Theme) Inject: Failed to set icon/theme. %s",
      error->message);
    break;
  }

  g_object_unref(connection);
}

}  // namespace

// Main
int main(int argc, char** argv) {
  if (!IsWaylandSession()) {
    g_message("(GTK Theme) Init: X11 session detected, exiting normally...");
    return EXIT_SUCCESS;
  }

  InjectTheme();
  return EXIT_SUCCESS;
}
