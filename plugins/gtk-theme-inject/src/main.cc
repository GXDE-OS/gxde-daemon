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
 * It also updates icon/theme upon theme/icon change.
 * ----------------------------------------------------------------------------
 * GSettings may emit several signal at a time, so for performance, we
 * "debounce" the signals (we wait for signal arrive, then continue waiting,
 * and only after a short silence we perform our task).
 * ----------------------------------------------------------------------------
 * Please note that ONLY gxde-wlcom is supported.
 */

#include <gio/gio.h>
#include <glib.h>

#include <cstdlib>
#include <string>

namespace {

constexpr char kWmService[] = "com.kylin.Wlcom";
constexpr char kWmPath[] = "/com/kylin/Wlcom/Theme";
constexpr char kWmInterface[] = "com.kylin.Wlcom.Theme";

constexpr char kGxdeThemeService[] = "top.gxde.Wlcom.Theme";
constexpr char kGxdeThemePath[] = "/top/gxde/Wlcom/Theme";
constexpr char kGxdeThemeInterface[] = "top.gxde.Wlcom.Theme";

constexpr char kWindowBtnService[] = "top.gxde.Wlcom.WindowBtn";
constexpr char kWindowBtnPath[] = "/top/gxde/Wlcom/WindowBtn";
constexpr char kWindowBtnInterface[] = "top.gxde.Wlcom.WindowBtn";

constexpr char kSeatPath[] = "/com/kylin/Wlcom/Seat";
constexpr char kSeatInterface[] = "com.kylin.Wlcom.Seat";

constexpr char kSchemaInterface[] = "com.deepin.dde.appearance";
constexpr char kKeyGtkTheme[] = "gtk-theme";
constexpr char kKeyIconTheme[] = "icon-theme";
constexpr char kKeyCursorTheme[] = "cursor-theme";

constexpr char kSchemaXSettings[] = "com.deepin.xsettings";
constexpr char kKeyCursorSize[] = "gtk-cursor-theme-size";

constexpr char kSchemaGtkInterface[] = "org.gnome.desktop.interface";
constexpr char kKeyGtkCursorSize[] = "cursor-size";

constexpr char kDarkSuffix[] = "-dark";

// The enum value of light/dark mode is defined in gxde-wlcom's source.
// Please refer to include/theme.h.
constexpr guint32 kThemeTypeLight = 0;
constexpr guint32 kThemeTypeDark = 1;

constexpr int kMaxRetries = 10;
constexpr int kRetryDelayUs = 1000000;
constexpr int kDebounceMs = 300;

// Debounce state: Rapid GSettings change signals.
guint debounce_source = 0;

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

// Sync Icon & themes from GSettings
void SyncGtkIconAndCursor() {
  GSettings* appearance = g_settings_new(kSchemaInterface);
  GSettings* xsettings = g_settings_new(kSchemaXSettings);
  GSettings* gtk = g_settings_new(kSchemaGtkInterface);

  g_autofree gchar* icon_theme = g_settings_get_string(appearance,
    kKeyIconTheme);
  g_autofree gchar* cursor_theme = g_settings_get_string(appearance,
    kKeyCursorTheme);
  gint cursor_size = g_settings_get_int(xsettings, kKeyCursorSize);

  if (icon_theme != nullptr && icon_theme[0] != '\0' &&
      !g_settings_set_string(gtk, kKeyIconTheme, icon_theme)) {
    g_warning("(GTK Theme) Sync: Failed to sync GTK icon theme \"%s\".",
      icon_theme);
  }
  if (cursor_theme != nullptr && cursor_theme[0] != '\0' &&
      !g_settings_set_string(gtk, kKeyCursorTheme, cursor_theme)) {
    g_warning("(GTK Theme) Sync: Failed to sync GTK cursor theme \"%s\".",
      cursor_theme);
  }
  if (cursor_size > 0 &&
      !g_settings_set_int(gtk, kKeyGtkCursorSize, cursor_size)) {
    g_warning("(GTK Theme) Sync: Failed to sync GTK cursor size %d.",
      cursor_size);
  }

  g_settings_sync();
  g_message(
    "(GTK Theme) Sync: GTK appearance synced: icon=%s cursor=%s size=%d.",
    icon_theme, cursor_theme, cursor_size);

  g_object_unref(gtk);
  g_object_unref(xsettings);
  g_object_unref(appearance);
}

// Inject the cursor theme into any SEAT.
void InjectCursor(GDBusConnection* connection) {
  GSettings* appearance = g_settings_new(kSchemaInterface);
  GSettings* xsettings = g_settings_new(kSchemaXSettings);

  g_autofree gchar* cursor_theme = g_settings_get_string(appearance,
    kKeyCursorTheme);
  guint32 cursor_size = (guint32)g_settings_get_int(xsettings, kKeyCursorSize);

  g_object_unref(appearance);
  g_object_unref(xsettings);

  if (cursor_theme == nullptr || cursor_theme[0] == '\0') {
    g_warning("(GTK Theme) Sync: No cursor theme configured, skipped.");
    return;
  }

  GError* error = nullptr;
  GVariant* seats_reply = nullptr;
  if (!CallMethod(connection, kWmService, kSeatPath, kSeatInterface,
      "ListAllSeats", nullptr, &seats_reply, &error)) {
    g_warning("(GTK Theme) Cursor: Cannot list seats. %s", error->message);
    g_clear_error(&error);
    return;
  }

  // ListAllSeats returns (a(ss)): seat name paired w/ its JSON config.
  g_autoptr(GVariantIter) iter = nullptr;
  g_variant_get(seats_reply, "(a(ss))", &iter);

  const gchar* seat_name = nullptr;
  const gchar* seat_config = nullptr;
  while (g_variant_iter_loop(iter, "(&s&s)", &seat_name, &seat_config)) {
    g_clear_error(&error);
    GVariant* cursor_params = g_variant_new("(ssu)", seat_name, cursor_theme,
      cursor_size);
    GVariant* cursor_reply = nullptr;
    if (CallMethod(connection, kWmService, kSeatPath, kSeatInterface,
        "SetCursor", cursor_params, &cursor_reply, &error)) {
      g_variant_unref(cursor_reply);
      g_message("(GTK Theme) Cursor: \"%s\" size=%u on seat \"%s\" -> OK.",
        cursor_theme, cursor_size, seat_name);
    } else {
      g_warning("(GTK Theme) Cursor: Failed on seat \"%s\". %s", seat_name,
        error->message);
    }
  }

  g_clear_error(&error);
  g_variant_unref(seats_reply);
}

// Inject GTK, widget, icon and cursor themes into WM D-Bus.
void InjectTheme() {
  GSettings* settings = g_settings_new(kSchemaInterface);

  g_autofree gchar* gtk_theme = g_settings_get_string(settings, kKeyGtkTheme);
  g_autofree gchar* icon_theme = g_settings_get_string(settings,
    kKeyIconTheme);
  bool is_dark = (gtk_theme != nullptr && g_str_has_suffix(gtk_theme,
    kDarkSuffix));
  guint32 theme_type = is_dark ? kThemeTypeDark : kThemeTypeLight;

  if (gtk_theme == nullptr || gtk_theme[0] == '\0') {
    g_warning("(GTK Theme) No GTK theme configured, aborted!");
    g_object_unref(settings);
    return;
  }

  g_message("(GTK Theme) Pending: theme=%s w/ icon=%s (%s).",
    gtk_theme,
    icon_theme != nullptr ? icon_theme : "(default)",
    is_dark ? "dark" : "light");

  SyncGtkIconAndCursor();

  GError* error = nullptr;
  GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr,
                                               &error);
  if (connection == nullptr) {
    g_warning(
      "(GTK Theme) Inject: Cannot connect to session bus: %s, aborted!!",
      error->message);
    g_clear_error(&error);
    g_object_unref(settings);
    return;
  }

  for (int retry = 0; retry < kMaxRetries; ++retry) {
    g_clear_error(&error);
    GVariant* theme_params = g_variant_new("(su)", "", theme_type);
    GVariant* reply = nullptr;
    if (CallMethod(connection, kWmService, kWmPath, kWmInterface,
        "SetWidgetTheme", theme_params, &reply, &error)) {
      gboolean ok = FALSE;
      g_variant_get(reply, "(b)", &ok);
      g_variant_unref(reply);
      g_message("(GTK Theme) Inject: Widget theme type=%u (%s) -> %s.",
        theme_type, is_dark ? "dark" : "light", ok ? "OK" : "FAILED");

      // GTK theme
      g_clear_error(&error);
      GVariant* gtk_params = g_variant_new("(s)", gtk_theme);
      GVariant* gtk_reply = nullptr;
      if (CallMethod(connection, kGxdeThemeService, kGxdeThemePath,
          kGxdeThemeInterface, "SetGTK", gtk_params, &gtk_reply, &error)) {
        gboolean gtk_ok = FALSE;
        g_variant_get(gtk_reply, "(b)", &gtk_ok);
        g_variant_unref(gtk_reply);
        if (gtk_ok) {
          g_message("(GTK Theme) Inject: GTK theme \"%s\" -> OK.", gtk_theme);
        } else {
          g_warning("(GTK Theme) Inject: GTK theme \"%s\" was rejected.",
            gtk_theme);
        }
      } else {
        g_warning("(GTK Theme) Inject: Failed to set GTK theme. %s",
          error->message);
      }

      // Window button
      g_clear_error(&error);
      GVariant* button_params = g_variant_new("(bbb)", TRUE, TRUE, TRUE);
      GVariant* button_reply = nullptr;
      if (CallMethod(connection, kWindowBtnService, kWindowBtnPath,
          kWindowBtnInterface, "SetGtkDecorationButtons", button_params,
          &button_reply, &error)) {
        gboolean button_ok = FALSE;
        g_variant_get(button_reply, "(b)", &button_ok);
        g_variant_unref(button_reply);
        g_message("(GTK Theme) Inject: GTK window buttons -> %s.",
          button_ok ? "OK" : "FAILED");
      } else {
        g_warning(
          "(GTK Theme) Inject: Failed to set GTK window buttons. %s",
          error->message);
      }

      if (icon_theme != nullptr && icon_theme[0] != '\0') {
        g_clear_error(&error);
        GVariant* icon_params = g_variant_new("(s)", icon_theme);
        GVariant* icon_reply = nullptr;
        if (CallMethod(connection, kWmService, kWmPath, kWmInterface,
            "SetIconTheme", icon_params, &icon_reply, &error)) {
          gboolean icon_ok = FALSE;
          g_variant_get(icon_reply, "(b)", &icon_ok);
          g_variant_unref(icon_reply);
          g_message("(GTK Theme) Inject: Icon theme \"%s\" -> %s.",
            icon_theme, icon_ok ? "applied" : "already set");
        } else {
          g_warning("(GTK Theme) Inject: Failed to set icon. %s",
            error->message);
        }
      }

      InjectCursor(connection);
      break;
    }

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
  g_object_unref(settings);
}

// Signal handler: Settings changed
gboolean OnDebounceTimeout(gpointer /*user_data*/) {
  debounce_source = 0;
  InjectTheme();
  return G_SOURCE_REMOVE;
}

void OnSettingsChanged(GSettings* /*settings*/, const gchar* key,
    gpointer /*user_data*/) {
  g_message("(GTK Theme) GSettings key \"%s\" changed, scheduling update...",
    key);
  if (debounce_source != 0) {
    g_source_remove(debounce_source);
  }
  debounce_source = g_timeout_add(kDebounceMs, OnDebounceTimeout, nullptr);
}

}  // namespace

// Main
int main() {
  if (!IsWaylandSession()) {
    g_message("(GTK Theme) Init: X11 session detected, exiting normally...");
    return EXIT_SUCCESS;
  }

  GSettings* settings = g_settings_new(kSchemaInterface);
  GSettings* xsettings = g_settings_new(kSchemaXSettings);

  g_signal_connect(settings, "changed::gtk-theme",
    G_CALLBACK(OnSettingsChanged), nullptr);
  g_signal_connect(settings, "changed::icon-theme",
    G_CALLBACK(OnSettingsChanged), nullptr);
  g_signal_connect(settings, "changed::cursor-theme",
    G_CALLBACK(OnSettingsChanged), nullptr);
  g_signal_connect(xsettings, "changed::gtk-cursor-theme-size",
    G_CALLBACK(OnSettingsChanged), nullptr);

  InjectTheme();

  GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
  g_main_loop_run(loop);

  g_object_unref(settings);
  g_object_unref(xsettings);
  g_main_loop_unref(loop);
  return EXIT_SUCCESS;
}
