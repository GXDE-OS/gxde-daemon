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

#ifndef SRC_SETTINGS_SETTINGS_H_
#define SRC_SETTINGS_SETTINGS_H_

#include <gio/gio.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace gxde {
namespace dock {

class Settings {
 public:
  using ChangeHandler = std::function<void(const std::string& key)>;

  Settings() = default;
  ~Settings();

  bool Init();
  void set_change_handler(ChangeHandler handler) {
    handler_ = std::move(handler);
  }

  int32_t hide_mode() const;
  void set_hide_mode(int32_t value);
  int32_t display_mode() const;
  void set_display_mode(int32_t value);
  int32_t position() const;
  void set_position(int32_t value);
  uint32_t icon_size() const;
  void set_icon_size(uint32_t value);
  uint32_t show_timeout() const;
  void set_show_timeout(uint32_t value);
  uint32_t hide_timeout() const;
  void set_hide_timeout(uint32_t value);
  bool window_split() const;
  void set_window_split(bool value);

  std::vector<std::string> docked_apps() const;
  void set_docked_apps(const std::vector<std::string>& apps);
  std::vector<std::string> win_icon_preferred_apps() const;

  std::string plugin_settings() const;
  void set_plugin_settings(const std::string& json);

  double opacity() const;

 private:
  static void OnDockChanged(GSettings* settings, const gchar* key,
                            gpointer user_data);

  GSettings* dock_ = nullptr;
  GSettings* appearance_ = nullptr;
  gulong changed_id_ = 0;
  ChangeHandler handler_;
};

}  // namespace dock
}  // namespace gxde

#endif  // SRC_SETTINGS_SETTINGS_H_
