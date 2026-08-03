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

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

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

// /proc/<pid>/cmdline[0] is the path used to start the process, which usually
// matches the Exec key of a .desktop file.  /proc/<pid>/exe may point to a
// different binary (e.g. firefox script exec's firefox-bin), so we need both.
std::string CmdlineBaseName(uint32_t pid) {
  if (pid == 0) {
    return "";
  }
  gchar* path = g_strdup_printf("/proc/%u/cmdline", pid);
  gchar* contents = nullptr;
  gsize length = 0;
  std::string result;
  if (g_file_get_contents(path, &contents, &length, nullptr) && length > 0) {
    gsize end = 0;
    while (end < length && contents[end] != '\0') ++end;
    gchar* base = g_path_get_basename(std::string(contents, end).c_str());
    result = base != nullptr ? base : "";
    g_free(base);
  }
  g_free(contents);
  g_free(path);
  return result;
}

std::shared_ptr<AppInfo> MatchByExe(uint32_t pid) {
  // Collect candidate binary names from both /proc/pid/cmdline[0] and
  // /proc/pid/exe.  cmdline[0] matches the Exec key more reliably when a
  // wrapper script exec's into a differently-named binary, so a desktop
  // file matching cmdline[0] is preferred over one matching only the exe.
  std::vector<std::string> cmdline_wants;
  std::string cmdline_base = Lower(CmdlineBaseName(pid));
  if (!cmdline_base.empty()) {
    cmdline_wants.push_back(cmdline_base);
  }
  std::vector<std::string> exe_wants;
  std::string exe_base = Lower(ExeBaseName(pid));
  if (!exe_base.empty() && exe_base != cmdline_base) {
    exe_wants.push_back(exe_base);
  }
  if (cmdline_wants.empty() && exe_wants.empty()) {
    return nullptr;
  }
  auto in = [](const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
  };

  // Several desktop files may share the same Exec basename (e.g. the
  // installed firefox-spark.desktop and an auto-generated, hidden
  // userapp-Firefox-*.desktop).  Prefer the canonical entry: visible
  // (not NoDisplay), installed under a system applications directory, and
  // matching the command the process was actually launched with.
  std::shared_ptr<AppInfo> best;
  int best_score = -1;
  GList* all = g_app_info_get_all();
  for (GList* l = all; l != nullptr; l = l->next) {
    auto* info = static_cast<GAppInfo*>(l->data);
    if (!G_IS_DESKTOP_APP_INFO(info)) {
      continue;
    }
    const char* exec = g_app_info_get_executable(info);
    if (exec == nullptr) {
      continue;
    }
    gchar* base = g_path_get_basename(exec);
    if (base == nullptr) {
      continue;
    }
    std::string lowered = Lower(base);
    g_free(base);

    bool from_cmdline = in(cmdline_wants, lowered);
    bool from_exe = in(exe_wants, lowered);
    if (!from_cmdline && !from_exe) {
      continue;
    }
    const char* file =
        g_desktop_app_info_get_filename(G_DESKTOP_APP_INFO(info));
    if (file == nullptr) {
      continue;
    }

    bool hidden = g_desktop_app_info_get_nodisplay(
                      G_DESKTOP_APP_INFO(info)) != 0;
    bool installed = false;
    for (const char* dir :
         {"/usr/share/applications/", "/usr/local/share/applications/"}) {
      if (strncmp(file, dir, strlen(dir)) == 0) {
        installed = true;
        break;
      }
    }

    int score = (from_cmdline ? 1 : 0) + (installed ? 2 : 0) +
                (hidden ? 0 : 4);
    if (score > best_score) {
      best_score = score;
      best = AppInfo::FromFile(file);
    }
  }
  g_list_free_full(all, g_object_unref);
  return best;
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
