/*
 * Copyright (C) 2026 GXDE OS Maintainers
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

#include "src/i18n/i18n.h"

#include <cjson/cJSON.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace gxde {
namespace dock {
namespace i18n {

namespace {

std::unordered_map<std::string, std::string> g_translations;

// Detect the current locale from environment variables.
// Follows the standard precedence: LC_ALL > LC_MESSAGES > LANG.
std::string DetectLocale() {
  const char* val = std::getenv("LC_ALL");
  if (val == nullptr || *val == '\0') {
    val = std::getenv("LC_MESSAGES");
  }
  if (val == nullptr || *val == '\0') {
    val = std::getenv("LANG");
  }
  if (val == nullptr || *val == '\0') {
    return "en_US";
  }

  std::string s(val);
  // Strip encoding suffix: "zh_CN.UTF-8" → "zh_CN"
  auto dot = s.find('.');
  if (dot != std::string::npos) {
    s = s.substr(0, dot);
  }
  return s;
}

// Load translations from a JSON file.
// Format: { "original": "translated", ... }
bool LoadFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return false;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();

  cJSON* root = cJSON_Parse(content.c_str());
  if (root == nullptr) {
    return false;
  }

  cJSON* item = root->child;
  while (item != nullptr) {
    if (item->string != nullptr && cJSON_IsString(item)) {
      g_translations[item->string] = item->valuestring;
    }
    item = item->next;
  }

  cJSON_Delete(root);
  return true;
}

}  // namespace

void init(const std::string& translations_dir) {
  std::string locale = DetectLocale();

  // Skip loading for English / C / POSIX — the source strings are English.
  if (locale == "en_US" || locale == "C" || locale == "POSIX") {
    return;
  }

  // Try exact locale match first (e.g. "zh_CN.json")
  std::string path = translations_dir + "/" + locale + ".json";
  if (!LoadFile(path)) {
    // Fall back to language-only code (e.g. "zh.json")
    auto underscore = locale.find('_');
    if (underscore != std::string::npos) {
      std::string short_locale = locale.substr(0, underscore);
      path = translations_dir + "/" + short_locale + ".json";
      LoadFile(path);
    }
  }
}

const char* tr(const char* str) {
  if (str == nullptr) {
    return nullptr;
  }
  auto it = g_translations.find(str);
  if (it != g_translations.end()) {
    return it->second.c_str();
  }
  return str;
}

}  // namespace i18n
}  // namespace dock
}  // namespace gxde