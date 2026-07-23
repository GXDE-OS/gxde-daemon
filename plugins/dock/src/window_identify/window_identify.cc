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

#include <cjson/cJSON.h>
#include <gio/gio.h>
#include <glib.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <vector>

namespace gxde {
namespace dock {

namespace {

// ===========================================================================
// Utility
// ===========================================================================

std::string Lower(const std::string& s) {
  gchar* lowered = g_ascii_strdown(s.c_str(), -1);
  std::string result = lowered != nullptr ? lowered : s;
  g_free(lowered);
  return result;
}

std::string BaseName(const std::string& path) {
  gchar* base = g_path_get_basename(path.c_str());
  std::string result = base != nullptr ? base : "";
  g_free(base);
  return result;
}

// Mimics Go's fmt %q (strconv.Quote) closely enough for MD5 compatibility.
std::string QuoteForHash(const std::string& s) {
  std::string result = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '\\': result += "\\\\"; break;
      case '"':  result += "\\\""; break;
      case '\n': result += "\\n"; break;
      case '\t': result += "\\t"; break;
      case '\r': result += "\\r"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\x%02x", c);
          result += buf;
        } else {
          result += static_cast<char>(c);
        }
    }
  }
  result += "\"";
  return result;
}

// ===========================================================================
// ProcessInfo  —  reads /proc/<pid>/ for exe, cmdline, environ, ppid
// ===========================================================================

struct ProcessInfo {
  std::string exe;                          // /proc/pid/exe symlink target
  std::vector<std::string> cmdline;         // /proc/pid/cmdline split by \0
  std::map<std::string, std::string> environ;  // /proc/pid/environ key=value
  uint32_t ppid = 0;
  bool valid = false;
};

ProcessInfo ReadProcessInfo(uint32_t pid) {
  ProcessInfo info;
  if (pid == 0) {
    return info;
  }

  // exe — symlink target of /proc/pid/exe
  gchar* exe_link = g_strdup_printf("/proc/%u/exe", pid);
  gchar* exe_target = g_file_read_link(exe_link, nullptr);
  g_free(exe_link);
  if (exe_target != nullptr) {
    info.exe = exe_target;
    g_free(exe_target);
  }

  // cmdline — null-separated strings from /proc/pid/cmdline
  gchar* cmdline_path = g_strdup_printf("/proc/%u/cmdline", pid);
  gchar* cmdline_data = nullptr;
  gsize cmdline_len = 0;
  if (g_file_get_contents(cmdline_path, &cmdline_data, &cmdline_len, nullptr) &&
      cmdline_len > 0) {
    gsize start = 0;
    for (gsize i = 0; i <= cmdline_len; ++i) {
      if (i == cmdline_len || cmdline_data[i] == '\0') {
        if (i > start) {
          info.cmdline.emplace_back(cmdline_data + start, i - start);
        } else {
          info.cmdline.emplace_back();
        }
        start = i + 1;
      }
    }
  }
  g_free(cmdline_data);
  g_free(cmdline_path);

  // environ — null-separated key=value pairs from /proc/pid/environ
  gchar* env_path = g_strdup_printf("/proc/%u/environ", pid);
  gchar* env_data = nullptr;
  gsize env_len = 0;
  if (g_file_get_contents(env_path, &env_data, &env_len, nullptr) &&
      env_len > 0) {
    gsize start = 0;
    for (gsize i = 0; i <= env_len; ++i) {
      if (i == env_len || env_data[i] == '\0') {
        if (i > start) {
          std::string entry(env_data + start, i - start);
          auto eq = entry.find('=');
          if (eq != std::string::npos) {
            info.environ[entry.substr(0, eq)] = entry.substr(eq + 1);
          }
        }
        start = i + 1;
      }
    }
  }
  g_free(env_data);
  g_free(env_path);

  // ppid — parse /proc/pid/status for PPid line
  gchar* status_path = g_strdup_printf("/proc/%u/status", pid);
  gchar* status_data = nullptr;
  gsize status_len = 0;
  if (g_file_get_contents(status_path, &status_data, &status_len, nullptr)) {
    std::string status(status_data, status_len);
    auto pos = status.find("PPid:");
    if (pos != std::string::npos) {
      auto end = status.find('\n', pos);
      std::string line = status.substr(pos + 5, end - pos - 5);
      // trim whitespace
      size_t first = line.find_first_not_of(" \t");
      if (first != std::string::npos) {
        size_t last = line.find_last_not_of(" \t\r\n");
        info.ppid = static_cast<uint32_t>(
            std::strtoul(line.substr(first, last - first + 1).c_str(),
                         nullptr, 10));
      }
    }
  }
  g_free(status_data);
  g_free(status_path);

  info.valid = true;
  return info;
}

// Read /proc/<pid>/cmdline[0] basename (used for ppid shell check)
std::string CmdlineBaseNameForPid(uint32_t pid) {
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

// ===========================================================================
// Window inner ID generation (for Scratch and fallback)
// Matches deepin-daemon's genInnerId algorithm.
// ===========================================================================

std::string FilterFilePath(const std::vector<std::string>& args) {
  std::vector<std::string> filtered;
  for (const std::string& arg : args) {
    if (arg.find('/') != std::string::npos || arg == "." || arg == "..") {
      filtered.push_back("%F");
    } else {
      filtered.push_back(arg);
    }
  }
  std::string result;
  for (size_t i = 0; i < filtered.size(); ++i) {
    if (i > 0) {
      result += " ";
    }
    result += filtered[i];
  }
  return result;
}

std::string GenerateWindowInnerId(const BackendWindow& window,
                                  const ProcessInfo& proc) {
  std::string wm_instance = BaseName(window.wm_instance);
  std::string wm_class = window.wm_class;
  std::string exe = proc.exe;
  std::string args = FilterFilePath(
      proc.cmdline.size() > 1
          ? std::vector<std::string>(proc.cmdline.begin() + 1, proc.cmdline.end())
          : std::vector<std::string>());
  bool has_pid = window.pid != 0;
  std::string gtk_app_id = window.gtk_app_id;

  std::string str;
  if (wm_instance.empty() && wm_class.empty() && exe.empty() &&
      gtk_app_id.empty()) {
    if (!window.title.empty()) {
      str = "wmName:" + QuoteForHash(window.title);
    } else {
      str = "windowId:" + std::to_string(window.id);
    }
  } else {
    str = "wmInstance:" + QuoteForHash(wm_instance) +
          ",wmClass:" + QuoteForHash(wm_class) +
          ",exe:" + QuoteForHash(exe) +
          ",args:" + QuoteForHash(args) +
          ",hasPid:" + (has_pid ? "true" : "false") +
          ",gtkAppId:" + QuoteForHash(gtk_app_id);
  }

  GChecksum* md5 = g_checksum_new(G_CHECKSUM_MD5);
  g_checksum_update(md5, reinterpret_cast<const guchar*>(str.c_str()),
                    str.size());
  const gchar* digest = g_checksum_get_string(md5);
  std::string inner_id = std::string("w:") + (digest != nullptr ? digest : "");
  g_checksum_free(md5);
  return inner_id;
}

// ===========================================================================
// AppInfo factory helpers (existing)
// ===========================================================================

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

// ===========================================================================
// PidEnv  —  GIO_LAUNCHED_DESKTOP_FILE in process environ (most accurate)
// ===========================================================================

std::shared_ptr<AppInfo> MatchByPidEnv(uint32_t pid, const ProcessInfo& proc) {
  if (!proc.valid || pid == 0) {
    return nullptr;
  }
  auto it_file = proc.environ.find("GIO_LAUNCHED_DESKTOP_FILE");
  auto it_pid = proc.environ.find("GIO_LAUNCHED_DESKTOP_FILE_PID");
  if (it_file == proc.environ.end() || it_file->second.empty()) {
    return nullptr;
  }
  const std::string& launched_file = it_file->second;
  uint32_t launched_pid = 0;
  if (it_pid != proc.environ.end()) {
    launched_pid = static_cast<uint32_t>(
        std::strtoul(it_pid->second.c_str(), nullptr, 10));
  }

  bool try_match = false;
  if (launched_pid == pid) {
    try_match = true;
  } else if (launched_pid == proc.ppid && proc.ppid != 0) {
    // Parent process launched it — check if parent is a shell wrapper
    std::string parent_base = CmdlineBaseNameForPid(proc.ppid);
    if (parent_base == "sh" || parent_base == "bash") {
      try_match = true;
    }
  }

  if (try_match) {
    return AppInfo::FromFile(launched_file);
  }
  return nullptr;
}

// ===========================================================================
// Rule engine  —  window_patterns.json
// ===========================================================================

// Operator types: =, e (equal), c (contains), r (regexp)
// Lowercase = case-insensitive, uppercase = case-sensitive
// Second char: ':' = match, '!' = negate
struct RuleValue {
  char type = '\0';
  bool negative = false;
  bool ignore_case = false;
  std::string value;
  std::regex regex;
  bool regex_compiled = false;
  bool valid = false;
};

struct Rule {
  std::string key;
  RuleValue value;
};

struct Pattern {
  std::vector<Rule> rules;
  std::string result;
};

RuleValue ParseRuleValue(const std::string& val) {
  RuleValue rv;
  if (val.size() < 2) {
    return rv;
  }
  rv.type = val[0];
  switch (val[1]) {
    case ':': break;
    case '!': rv.negative = true; break;
    default: return rv;
  }
  rv.value = val.substr(2);

  switch (rv.type) {
    case '=':  // equal, case-sensitive
    case 'E':
      rv.valid = true;
      break;
    case 'e':  // equal, case-insensitive
      rv.ignore_case = true;
      rv.valid = true;
      break;
    case 'C':  // contains, case-sensitive
      rv.valid = true;
      break;
    case 'c':  // contains, case-insensitive
      rv.ignore_case = true;
      rv.valid = true;
      break;
    case 'R':  // regexp, case-sensitive
      try {
        rv.regex = std::regex(rv.value);
        rv.regex_compiled = true;
        rv.valid = true;
      } catch (...) {}
      break;
    case 'r':  // regexp, case-insensitive
      rv.ignore_case = true;
      try {
        rv.regex = std::regex(rv.value, std::regex::icase);
        rv.regex_compiled = true;
        rv.valid = true;
      } catch (...) {}
      break;
    default:
      break;
  }
  return rv;
}

bool MatchRuleValue(const RuleValue& rv, const std::string& key_value) {
  if (!rv.valid) {
    return false;
  }
  std::string k = key_value;
  std::string v = rv.value;
  if (rv.ignore_case) {
    k = Lower(k);
    v = Lower(v);
  }
  bool result = false;
  switch (rv.type) {
    case '=':
    case 'e':
    case 'E':
      result = (k == v);
      break;
    case 'c':
    case 'C':
      result = (k.find(v) != std::string::npos);
      break;
    case 'r':
    case 'R':
      if (rv.regex_compiled) {
        try {
          result = std::regex_search(k, rv.regex);
        } catch (...) {
          result = false;
        }
      }
      break;
  }
  return rv.negative ? !result : result;
}

std::string GetRuleKeyValue(const std::string& key, const BackendWindow& window,
                            const ProcessInfo& proc) {
  if (key == "hasPid") {
    return (proc.valid && window.pid != 0) ? "t" : "f";
  }
  if (key == "exec") {
    return proc.valid ? BaseName(proc.exe) : "";
  }
  if (key == "arg") {
    if (!proc.valid || proc.cmdline.size() < 2) {
      return "";
    }
    std::string result;
    for (size_t i = 1; i < proc.cmdline.size(); ++i) {
      if (i > 1) {
        result += " ";
      }
      result += proc.cmdline[i];
    }
    return result;
  }
  if (key == "wmi") {
    return window.wm_instance;
  }
  if (key == "wmc") {
    return window.wm_class;
  }
  if (key == "wmn") {
    return window.title;
  }
  if (key == "wmrole") {
    return window.wm_role;
  }
  // env.XXX
  const std::string env_prefix = "env.";
  if (key.compare(0, env_prefix.size(), env_prefix) == 0) {
    std::string env_name = key.substr(env_prefix.size());
    if (!proc.valid) {
      return "";
    }
    auto it = proc.environ.find(env_name);
    return it != proc.environ.end() ? it->second : "";
  }
  return "";
}

class WindowPatternEngine {
 public:
  static WindowPatternEngine& Instance() {
    static WindowPatternEngine instance;
    return instance;
  }

  std::string Match(const BackendWindow& window, const ProcessInfo& proc) {
    if (!loaded_) {
      Load();
    }
    for (const Pattern& pattern : patterns_) {
      bool all_match = true;
      for (const Rule& rule : pattern.rules) {
        std::string key_value = GetRuleKeyValue(rule.key, window, proc);
        if (!MatchRuleValue(rule.value, key_value)) {
          all_match = false;
          break;
        }
      }
      if (all_match) {
        return pattern.result;
      }
    }
    return "";
  }

 private:
  WindowPatternEngine() = default;

  void Load() {
    loaded_ = true;
    const char* env_path = g_getenv("GXDE_DOCK_WINDOW_PATTERNS");
    std::string path = env_path != nullptr
                            ? env_path
                            : "/usr/share/gxde/data/window_patterns.json";
    gchar* contents = nullptr;
    gsize length = 0;
    if (!g_file_get_contents(path.c_str(), &contents, &length, nullptr)) {
      return;
    }
    cJSON* root = cJSON_ParseWithLength(contents, length);
    g_free(contents);
    if (root == nullptr || !cJSON_IsArray(root)) {
      cJSON_Delete(root);
      return;
    }
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
      Pattern pattern;
      cJSON* ret = cJSON_GetObjectItem(item, "ret");
      cJSON* rules = cJSON_GetObjectItem(item, "rules");
      if (ret == nullptr || !cJSON_IsString(ret) ||
          rules == nullptr || !cJSON_IsArray(rules)) {
        continue;
      }
      pattern.result = ret->valuestring;
      cJSON* rule_item = nullptr;
      cJSON_ArrayForEach(rule_item, rules) {
        if (!cJSON_IsArray(rule_item) || cJSON_GetArraySize(rule_item) < 2) {
          continue;
        }
        cJSON* key_node = cJSON_GetArrayItem(rule_item, 0);
        cJSON* val_node = cJSON_GetArrayItem(rule_item, 1);
        if (key_node == nullptr || !cJSON_IsString(key_node) ||
            val_node == nullptr || !cJSON_IsString(val_node)) {
          continue;
        }
        Rule rule;
        rule.key = key_node->valuestring;
        rule.value = ParseRuleValue(val_node->valuestring);
        pattern.rules.push_back(std::move(rule));
      }
      patterns_.push_back(std::move(pattern));
    }
    cJSON_Delete(root);
  }

  std::vector<Pattern> patterns_;
  bool loaded_ = false;
};

std::shared_ptr<AppInfo> MatchByRule(const BackendWindow& window,
                                     const ProcessInfo& proc) {
  std::string ret = WindowPatternEngine::Instance().Match(window, proc);
  if (ret.empty()) {
    return nullptr;
  }
  if (ret.compare(0, 3, "id=") == 0 && ret.size() > 3) {
    return TryDesktopId(ret.substr(3));
  }
  if (ret == "env") {
    if (proc.valid) {
      auto it = proc.environ.find("GIO_LAUNCHED_DESKTOP_FILE");
      if (it != proc.environ.end() && !it->second.empty()) {
        return AppInfo::FromFile(it->second);
      }
    }
    return nullptr;
  }
  return nullptr;
}

// ===========================================================================
// Bamf  —  DBus-based window-to-desktop matching (X11 only)
// ===========================================================================

std::shared_ptr<AppInfo> MatchByBamf(uint32_t xid) {
  if (xid == 0) {
    return nullptr;
  }
  GError* error = nullptr;

  GDBusProxy* matcher = g_dbus_proxy_new_for_bus_sync(
      G_BUS_TYPE_SESSION,
      static_cast<GDBusProxyFlags>(G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START |
                                   G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS),
      nullptr, "org.ayatana.bamf", "/org/ayatana/bamf/matcher",
      "org.ayatana.bamf.matcher", nullptr, &error);
  if (matcher == nullptr) {
    g_clear_error(&error);
    return nullptr;
  }

  GVariant* result = g_dbus_proxy_call_sync(
      matcher, "ApplicationForXid", g_variant_new("(u)", xid),
      G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error);
  g_object_unref(matcher);
  if (result == nullptr) {
    g_clear_error(&error);
    return nullptr;
  }

  const gchar* app_path_str = nullptr;
  g_variant_get(result, "(&s)", &app_path_str);
  std::string app_path = app_path_str != nullptr ? app_path_str : "";
  g_variant_unref(result);

  if (app_path.empty() || app_path == "/") {
    return nullptr;
  }

  GDBusProxy* app = g_dbus_proxy_new_for_bus_sync(
      G_BUS_TYPE_SESSION,
      static_cast<GDBusProxyFlags>(G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START |
                                   G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS),
      nullptr, "org.ayatana.bamf", app_path.c_str(),
      "org.ayatana.bamf.application", nullptr, &error);
  if (app == nullptr) {
    g_clear_error(&error);
    return nullptr;
  }

  result = g_dbus_proxy_call_sync(app, "DesktopFile", nullptr,
                                  G_DBUS_CALL_FLAGS_NONE, 2000, nullptr,
                                  &error);
  g_object_unref(app);
  if (result == nullptr) {
    g_clear_error(&error);
    return nullptr;
  }

  const gchar* desktop_file = nullptr;
  g_variant_get(result, "(&s)", &desktop_file);
  std::string desktop_path = desktop_file != nullptr ? desktop_file : "";
  g_variant_unref(result);

  if (desktop_path.empty()) {
    return nullptr;
  }
  return AppInfo::FromFile(desktop_path);
}

// ===========================================================================
// Scratch  —  check ~/.config/dock/scratch/<inner_id>.desktop
// ===========================================================================

std::shared_ptr<AppInfo> MatchByScratch(const std::string& inner_id) {
  if (inner_id.empty()) {
    return nullptr;
  }
  gchar* scratch_dir = g_build_filename(g_get_user_config_dir(), "dock",
                                        "scratch", nullptr);
  gchar* file = g_build_filename(scratch_dir, (inner_id + ".desktop").c_str(),
                                 nullptr);
  g_free(scratch_dir);
  std::shared_ptr<AppInfo> result = AppInfo::FromFile(file);
  g_free(file);
  return result;
}

// ===========================================================================
// FlatpakAppID  —  parse "app/<id>/..." and try as desktop id
// ===========================================================================

std::shared_ptr<AppInfo> MatchByFlatpakAppID(const std::string& flatpak_app_id) {
  if (flatpak_app_id.empty()) {
    return nullptr;
  }
  // Format: app/<app-id>/<arch>/<branch>
  if (flatpak_app_id.compare(0, 4, "app/") != 0) {
    return nullptr;
  }
  std::string rest = flatpak_app_id.substr(4);
  auto slash = rest.find('/');
  if (slash == std::string::npos) {
    return nullptr;
  }
  return TryDesktopId(rest.substr(0, slash));
}

// ===========================================================================
// GtkAppId  —  try _GTK_APPLICATION_ID as desktop id
// ===========================================================================

std::shared_ptr<AppInfo> MatchByGtkAppId(const std::string& gtk_app_id) {
  if (gtk_app_id.empty()) {
    return nullptr;
  }
  return TryDesktopId(gtk_app_id);
}

// ===========================================================================
// Pid (MatchByExe)  —  match exe/cmdline basename against desktop Exec
// ===========================================================================

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

std::string CmdlineBaseName(uint32_t pid) {
  return CmdlineBaseNameForPid(pid);
}

std::shared_ptr<AppInfo> MatchByExe(uint32_t pid) {
  std::vector<std::string> wants;
  for (const std::string& name : {CmdlineBaseName(pid), ExeBaseName(pid)}) {
    if (!name.empty()) {
      std::string lowered = Lower(name);
      if (std::find(wants.begin(), wants.end(), lowered) == wants.end()) {
        wants.push_back(lowered);
      }
    }
  }
  if (wants.empty()) {
    return nullptr;
  }
  std::shared_ptr<AppInfo> result;
  GList* all = g_app_info_get_all();
  for (GList* l = all; l != nullptr && result == nullptr; l = l->next) {
    auto* info = static_cast<GAppInfo*>(l->data);
    if (!G_IS_DESKTOP_APP_INFO(info)) {
      continue;
    }
    const char* exec = g_app_info_get_executable(info);
    if (exec == nullptr) {
      continue;
    }
    gchar* base = g_path_get_basename(exec);
    if (base != nullptr) {
      std::string lowered = Lower(base);
      if (std::find(wants.begin(), wants.end(), lowered) != wants.end()) {
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

// ===========================================================================
// IdentifyWindow  —  the identification chain
//
// Priority order (ported from deepin-daemon):
//   1. PidEnv         — GIO_LAUNCHED_DESKTOP_FILE in process environ
//   2. FlatpakAppID   — FLATPAK_APPID window property
//   3. Rule           — window_patterns.json rule engine
//   4. Bamf           — org.ayatana.bamf DBus (X11 only)
//   5. Pid            — match exe/cmdline to desktop Exec
//   6. Scratch        — ~/.config/dock/scratch/<inner_id>.desktop
//   7. GtkAppId       — _GTK_APPLICATION_ID window property
//   8. AppId          — try window.app_id as desktop id
//   9. WmInstance     — try window.wm_instance as desktop id
//  10. StartupWMClass — match StartupWMClass in desktop files
//  11. Search         — g_desktop_app_info_search
// ===========================================================================

IdentifyResult IdentifyWindow(const BackendWindow& window) {
  IdentifyResult result;

  ProcessInfo proc = ReadProcessInfo(window.pid);
  std::string window_inner_id = GenerateWindowInnerId(window, proc);

  struct Attempt {
    const char* method;
    std::shared_ptr<AppInfo> info;
  };

  const std::array<Attempt, 11> attempts = {{
      {"PidEnv", MatchByPidEnv(window.pid, proc)},
      {"FlatpakAppID", MatchByFlatpakAppID(window.flatpak_app_id)},
      {"Rule", MatchByRule(window, proc)},
      {"Bamf", MatchByBamf(window.id)},
      {"Pid", MatchByExe(window.pid)},
      {"Scratch", MatchByScratch(window_inner_id)},
      {"GtkAppId", MatchByGtkAppId(window.gtk_app_id)},
      {"AppId", TryDesktopId(window.app_id)},
      {"WmInstance", TryDesktopId(window.wm_instance)},
      {"StartupWMClass",
       MatchStartupWmClass(window.wm_class.empty() ? window.app_id
                                                   : window.wm_class)},
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

  result.app_info = nullptr;
  result.inner_id = window_inner_id;
  result.method = "Failed";
  return result;
}

}  // namespace dock
}  // namespace gxde
