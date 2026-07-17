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

#include "src/app_info/app_info.h"

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace gxde {
namespace dock {

namespace {

struct DirCode {
  const char* dir;
  const char* code;
};

std::array<DirCode, 4> DirCodes() {
  static std::string home_local =
      std::string(g_get_home_dir()) + "/.local/share/applications/";
  static std::string scratch =
      std::string(g_get_user_config_dir()) + "/dock/scratch/";
  return {{
      {"/usr/share/applications/", "/S@"},
      {"/usr/local/share/applications/", "/L@"},
      {home_local.c_str(), "/H@"},
      {scratch.c_str(), "/D@"},
  }};
}

std::string TrimDesktopExt(const std::string& s) {
  const std::string ext = ".desktop";
  if (s.size() >= ext.size() &&
      s.compare(s.size() - ext.size(), ext.size(), ext) == 0) {
    return s.substr(0, s.size() - ext.size());
  }
  return s;
}

std::string AddDesktopExt(const std::string& s) {
  const std::string ext = ".desktop";
  if (s.size() >= ext.size() &&
      s.compare(s.size() - ext.size(), ext.size(), ext) == 0) {
    return s;
  }
  return s + ext;
}

}  // namespace

std::string ZipDesktopPath(const std::string& path) {
  std::string result = path;
  for (const DirCode& dc : DirCodes()) {
    std::string dir = dc.dir;
    if (result.compare(0, dir.size(), dir) == 0) {
      result = std::string(dc.code) + result.substr(dir.size());
      break;
    }
  }
  return TrimDesktopExt(result);
}

std::string UnzipDesktopPath(const std::string& path) {
  if (path.size() >= 3) {
    std::string head = path.substr(0, 3);
    for (const DirCode& dc : DirCodes()) {
      if (head == dc.code) {
        return AddDesktopExt(std::string(dc.dir) + path.substr(3));
      }
    }
  }
  return AddDesktopExt(path);
}

AppInfo::AppInfo(GDesktopAppInfo* info) : info_(info) {
  const char* file = g_desktop_app_info_get_filename(info_);
  if (file != nullptr) {
    file_name_ = file;
  }

  const char* name = g_app_info_get_name(G_APP_INFO(info_));
  if (name != nullptr) {
    name_ = name;
  }

  gchar* icon = g_desktop_app_info_get_string(info_, "Icon");
  if (icon != nullptr) {
    icon_ = icon;
    g_free(icon);
  }

  gchar* wm_class = g_desktop_app_info_get_string(info_, "StartupWMClass");
  if (wm_class != nullptr) {
    startup_wm_class_ = wm_class;
    g_free(wm_class);
  }

  const char* desktop_id = g_app_info_get_id(G_APP_INFO(info_));
  if (desktop_id != nullptr) {
    id_ = desktop_id;
  } else if (!file_name_.empty()) {
    gchar* base = g_path_get_basename(file_name_.c_str());
    id_ = base != nullptr ? base : "";
    g_free(base);
  }

  for (const char* dir :
       {"/usr/share/applications/", "/usr/local/share/applications/"}) {
    if (file_name_.compare(0, strlen(dir), dir) == 0) {
      installed_ = true;
      break;
    }
  }
  if (!installed_) {
    gchar* user_apps = g_build_filename(g_get_home_dir(),
                                        ".local/share/applications/", nullptr);
    installed_ = file_name_.compare(0, strlen(user_apps), user_apps) == 0;
    g_free(user_apps);
  }

  const char* cmdline = g_app_info_get_commandline(G_APP_INFO(info_));
  gchar* digest = g_compute_checksum_for_string(
      G_CHECKSUM_MD5, cmdline != nullptr ? cmdline : id_.c_str(), -1);
  inner_id_ = std::string("d:") + (digest != nullptr ? digest : id_);
  g_free(digest);
}

AppInfo::~AppInfo() { g_clear_object(&info_); }

std::shared_ptr<AppInfo> AppInfo::FromFile(const std::string& path) {
  GDesktopAppInfo* info = g_desktop_app_info_new_from_filename(path.c_str());
  if (info == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<AppInfo>(new AppInfo(info));
}

std::shared_ptr<AppInfo> AppInfo::FromDesktopId(const std::string& desktop_id) {
  std::string id = desktop_id;
  if (id.find(".desktop") == std::string::npos) {
    id += ".desktop";
  }
  GDesktopAppInfo* info = g_desktop_app_info_new(id.c_str());
  if (info == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<AppInfo>(new AppInfo(info));
}

bool AppInfo::Launch(const std::vector<std::string>& files) const {
  GList* file_list = nullptr;
  for (const std::string& f : files) {
    GFile* gfile = g_file_new_for_commandline_arg(f.c_str());
    file_list = g_list_append(file_list, gfile);
  }
  GError* error = nullptr;
  gboolean ok =
      g_app_info_launch(G_APP_INFO(info_), file_list, nullptr, &error);
  g_list_free_full(file_list, g_object_unref);
  if (!ok) {
    g_warning("dock: launch %s failed: %s", id_.c_str(),
              error != nullptr ? error->message : "unknown");
    g_clear_error(&error);
  }
  return ok;
}

}  // namespace dock
}  // namespace gxde
