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

#ifndef SRC_APP_INFO_APP_INFO_H_
#define SRC_APP_INFO_APP_INFO_H_

#include <gio/gdesktopappinfo.h>

#include <memory>
#include <string>
#include <vector>

namespace gxde {
namespace dock {

class AppInfo {
 public:
  static std::shared_ptr<AppInfo> FromFile(const std::string& path);
  static std::shared_ptr<AppInfo> FromDesktopId(const std::string& desktop_id);

  ~AppInfo();
  AppInfo(const AppInfo&) = delete;
  AppInfo& operator=(const AppInfo&) = delete;

  const std::string& id() const { return id_; }
  const std::string& file_name() const { return file_name_; }
  const std::string& name() const { return name_; }
  const std::string& icon() const { return icon_; }
  const std::string& inner_id() const { return inner_id_; }
  const std::string& startup_wm_class() const { return startup_wm_class_; }
  bool installed() const { return installed_; }

  bool Launch(const std::vector<std::string>& files) const;

 private:
  explicit AppInfo(GDesktopAppInfo* info);

  GDesktopAppInfo* info_ = nullptr;
  std::string id_;
  std::string file_name_;
  std::string name_;
  std::string icon_;
  std::string inner_id_;
  std::string startup_wm_class_;
  bool installed_ = false;
};

std::string ZipDesktopPath(const std::string& path);
std::string UnzipDesktopPath(const std::string& path);

}  // namespace dock
}  // namespace gxde

#endif  // SRC_APP_INFO_APP_INFO_H_
