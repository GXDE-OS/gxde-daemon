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

#include "src/window_identify/window_identify.h"

#include <gio/gio.h>

#include <array>
#include <memory>
#include <string>

namespace gxde {
namespace dock {

namespace {

std::string Lower(const std::string& s) {
  gchar* lowered = g_ascii_strdown(s.c_str(), -1);
  std::string result = lowered != nullptr ? lowered : s;
  g_free(lowered);
  return result;
}

std::shared_ptr<AppInfo> TryDesktopId(const std::string& raw) {
  if (raw.empty()) {
    return nullptr;
  }
  for (const std::string& candidate : {raw, Lower(raw)}) {
    std::shared_ptr<AppInfo> info = AppInfo::FromDesktopId(candidate);
    if (info != nullptr) {
      return info;
    }
  }
  return nullptr;
}

std::shared_ptr<AppInfo> MatchStartupWmClass(const std::string& wm_class) {
  if (wm_class.empty()) {
    return nullptr;
  }
  std::string want = Lower(wm_class);
  std::shared_ptr<AppInfo> result;
  GList* all = g_app_info_get_all();
  for (GList* l = all; l != nullptr && result == nullptr; l = l->next) {
    auto* info = static_cast<GAppInfo*>(l->data);
    if (!G_IS_DESKTOP_APP_INFO(info)) {
      continue;
    }
    gchar* start_class = g_desktop_app_info_get_string(G_DESKTOP_APP_INFO(info),
                                                       "StartupWMClass");
    if (start_class != nullptr && Lower(start_class) == want) {
      const char* file =
          g_desktop_app_info_get_filename(G_DESKTOP_APP_INFO(info));
      if (file != nullptr) {
        result = AppInfo::FromFile(file);
      }
    }
    g_free(start_class);
  }
  g_list_free_full(all, g_object_unref);
  return result;
}

std::shared_ptr<AppInfo> SearchDesktop(const std::string& term) {
  if (term.empty()) {
    return nullptr;
  }
  gchar*** groups = g_desktop_app_info_search(term.c_str());
  std::shared_ptr<AppInfo> result;
  if (groups != nullptr) {
    if (groups[0] != nullptr && groups[0][0] != nullptr) {
      result = AppInfo::FromDesktopId(groups[0][0]);
    }
    for (gchar*** g = groups; *g != nullptr; ++g) {
      g_strfreev(*g);
    }
    g_free(groups);
  }
  return result;
}

std::string ExeBaseName(uint32_t pid) {
  if (pid == 0) {
    return "";
  }
  gchar* link = g_strdup_printf("/proc/%u/exe", pid);
  gchar* target = g_file_read_link(link, nullptr);
  g_free(link);
  if (target == nullptr) {
    return "";
  }
  gchar* base = g_path_get_basename(target);
  std::string result = base != nullptr ? base : "";
  g_free(base);
  g_free(target);
  return result;
}

std::shared_ptr<AppInfo> MatchByExe(uint32_t pid) {
  std::string exe = ExeBaseName(pid);
  if (exe.empty()) {
    return nullptr;
  }
  std::string want = Lower(exe);
  std::shared_ptr<AppInfo> result;
  GList* all = g_app_info_get_all();
  for (GList* l = all; l != nullptr && result == nullptr; l = l->next) {
    auto* info = static_cast<GAppInfo*>(l->data);
    const char* exec = g_app_info_get_executable(info);
    if (exec != nullptr) {
      gchar* base = g_path_get_basename(exec);
      if (base != nullptr && Lower(base) == want &&
          G_IS_DESKTOP_APP_INFO(info)) {
        const char* file =
            g_desktop_app_info_get_filename(G_DESKTOP_APP_INFO(info));
        if (file != nullptr) {
          result = AppInfo::FromFile(file);
        }
      }
      g_free(base);
    }
  }
  g_list_free_full(all, g_object_unref);
  return result;
}

}  // namespace

IdentifyResult IdentifyWindow(const BackendWindow& window) {
  IdentifyResult result;

  struct Attempt {
    const char* method;
    std::shared_ptr<AppInfo> info;
  };
  const std::array<Attempt, 5> attempts = {{
      {"AppId", TryDesktopId(window.app_id)},
      {"WmInstance", TryDesktopId(window.wm_instance)},
      {"StartupWMClass",
       MatchStartupWmClass(window.wm_class.empty() ? window.app_id
                                                   : window.wm_class)},
      {"Pid", MatchByExe(window.pid)},
      {"Search", SearchDesktop(window.app_id)},
  }};

  for (const Attempt& attempt : attempts) {
    if (attempt.info != nullptr) {
      result.app_info = attempt.info;
      result.inner_id = attempt.info->inner_id();
      result.method = attempt.method;
      return result;
    }
  }

  std::string key = !window.app_id.empty()     ? window.app_id
                    : !window.wm_class.empty() ? window.wm_class
                                               : window.title;
  result.app_info = nullptr;
  result.inner_id = std::string("w:") + Lower(key);
  result.method = "Failed";
  return result;
}

}  // namespace dock
}  // namespace gxde
