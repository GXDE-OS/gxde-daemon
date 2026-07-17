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

#include "src/settings/settings.h"

#include <string>
#include <vector>

namespace gxde {
namespace dock {

namespace {

constexpr char kDockSchema[] = "com.deepin.dde.dock";
constexpr char kAppearanceSchema[] = "com.deepin.dde.appearance";

GSettings* MakeSettings(const char* schema_id) {
  GSettingsSchemaSource* source = g_settings_schema_source_get_default();
  if (source == nullptr) {
    return nullptr;
  }
  GSettingsSchema* schema =
      g_settings_schema_source_lookup(source, schema_id, TRUE);
  if (schema == nullptr) {
    g_warning("(Dock) SET: GSettings schema %s not installed", schema_id);
    return nullptr;
  }
  g_settings_schema_unref(schema);
  return g_settings_new(schema_id);
}

std::vector<std::string> ReadStrv(GSettings* settings, const char* key) {
  std::vector<std::string> result;
  gchar** values = g_settings_get_strv(settings, key);
  if (values == nullptr) {
    return result;
  }
  for (gchar** p = values; *p != nullptr; ++p) {
    result.emplace_back(*p);
  }
  g_strfreev(values);
  return result;
}

}  // namespace

Settings::~Settings() {
  if (dock_ != nullptr && changed_id_ != 0) {
    g_signal_handler_disconnect(dock_, changed_id_);
  }
  g_clear_object(&dock_);
  g_clear_object(&appearance_);
}

bool Settings::Init() {
  dock_ = MakeSettings(kDockSchema);
  if (dock_ == nullptr) {
    return false;
  }
  appearance_ = MakeSettings(kAppearanceSchema);
  changed_id_ = g_signal_connect(dock_, "changed",
                                 G_CALLBACK(&Settings::OnDockChanged), this);
  return true;
}

void Settings::OnDockChanged(GSettings* /*settings*/, const gchar* key,
                             gpointer user_data) {
  auto* self = static_cast<Settings*>(user_data);
  if (self->handler_) {
    self->handler_(key != nullptr ? key : "");
  }
}

int32_t Settings::hide_mode() const {
  return g_settings_get_enum(dock_, "hide-mode");
}
void Settings::set_hide_mode(int32_t value) {
  g_settings_set_enum(dock_, "hide-mode", value);
}

int32_t Settings::display_mode() const {
  return g_settings_get_enum(dock_, "display-mode");
}
void Settings::set_display_mode(int32_t value) {
  g_settings_set_enum(dock_, "display-mode", value);
}

int32_t Settings::position() const {
  return g_settings_get_enum(dock_, "position");
}
void Settings::set_position(int32_t value) {
  g_settings_set_enum(dock_, "position", value);
}

uint32_t Settings::icon_size() const {
  return g_settings_get_uint(dock_, "icon-size");
}
void Settings::set_icon_size(uint32_t value) {
  g_settings_set_uint(dock_, "icon-size", value);
}

uint32_t Settings::show_timeout() const {
  return g_settings_get_uint(dock_, "show-timeout");
}
void Settings::set_show_timeout(uint32_t value) {
  g_settings_set_uint(dock_, "show-timeout", value);
}

uint32_t Settings::hide_timeout() const {
  return g_settings_get_uint(dock_, "hide-timeout");
}
void Settings::set_hide_timeout(uint32_t value) {
  g_settings_set_uint(dock_, "hide-timeout", value);
}

bool Settings::window_split() const {
  return g_settings_get_boolean(dock_, "window-split") != 0;
}
void Settings::set_window_split(bool value) {
  g_settings_set_boolean(dock_, "window-split", value ? TRUE : FALSE);
}

std::vector<std::string> Settings::docked_apps() const {
  return ReadStrv(dock_, "docked-apps");
}

void Settings::set_docked_apps(const std::vector<std::string>& apps) {
  std::vector<const gchar*> ptrs;
  ptrs.reserve(apps.size() + 1);
  for (const std::string& app : apps) {
    ptrs.push_back(app.c_str());
  }
  ptrs.push_back(nullptr);
  g_settings_set_strv(dock_, "docked-apps", ptrs.data());
}

std::vector<std::string> Settings::win_icon_preferred_apps() const {
  return ReadStrv(dock_, "win-icon-preferred-apps");
}

std::string Settings::plugin_settings() const {
  gchar* value = g_settings_get_string(dock_, "plugin-settings");
  std::string result = value != nullptr ? value : "{}";
  g_free(value);
  return result;
}

void Settings::set_plugin_settings(const std::string& json) {
  g_settings_set_string(dock_, "plugin-settings", json.c_str());
}

double Settings::opacity() const {
  if (appearance_ == nullptr) {
    return 0.4;
  }
  return g_settings_get_double(appearance_, "opacity");
}

}  // namespace dock
}  // namespace gxde
