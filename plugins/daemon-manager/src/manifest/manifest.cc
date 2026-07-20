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

#include <glib.h>
#include <cjson/cJSON.h>

#include <string>
#include <vector>
#include <utility>

#include "src/manifest/manifest.h"

namespace gxde {
namespace dmgr {

namespace {

std::string GetString(const cJSON* obj, const char* key) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  return cJSON_IsString(v) ? v->valuestring : "";
}

bool GetBool(const cJSON* obj, const char* key, bool fallback) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  return cJSON_IsBool(v) ? cJSON_IsTrue(v) : fallback;
}

}  // namespace

std::optional<Manifest> LoadManifest(const std::string& file_path) {
  gchar* contents = nullptr;
  gsize length = 0;
  if (!g_file_get_contents(file_path.c_str(), &contents, &length, nullptr)) {
    return std::nullopt;
  }
  cJSON* root = cJSON_ParseWithLength(contents, length);
  g_free(contents);
  if (root == nullptr || !cJSON_IsObject(root)) {
    g_warning("(Daemon MGR) Manifest: invalid manifest %s.", file_path.c_str());
    cJSON_Delete(root);
    return std::nullopt;
  }

  Manifest manifest;
  manifest.name = GetString(root, "name");
  manifest.description = GetString(root, "description");
  manifest.version = GetString(root, "version");
  manifest.exec = GetString(root, "exec");
  manifest.maintainer = GetString(root, "maintainer");
  manifest.restart = GetBool(root, "restart", true);
  manifest.resident = GetBool(root, "resident", false);
  manifest.oneshot = GetBool(root, "oneshot", false);
  manifest.path = file_path;

  const cJSON* bus = cJSON_GetObjectItemCaseSensitive(root, "bus_names");
  if (cJSON_IsArray(bus)) {
    const cJSON* v = nullptr;
    cJSON_ArrayForEach(v, bus) {
      if (cJSON_IsString(v)) {
        manifest.bus_names.push_back(v->valuestring);
      }
    }
  }

  cJSON_Delete(root);

  if (manifest.name.empty()) {
    gchar* base = g_path_get_basename(file_path.c_str());
    if (base != nullptr) {
      std::string b = base;
      size_t dot = b.rfind(".json");
      manifest.name = dot != std::string::npos ? b.substr(0, dot) : b;
    }
    g_free(base);
  }
  return manifest;
}

std::vector<Manifest> LoadManifests(const std::string& dir) {
  std::vector<Manifest> result;

  GDir* handle = g_dir_open(dir.c_str(), 0, nullptr);
  if (handle == nullptr) {
    return result;
  }
  const gchar* name = nullptr;
  while ((name = g_dir_read_name(handle)) != nullptr) {
    if (!g_str_has_suffix(name, ".json")) {
      continue;
    }
    gchar* full = g_build_filename(dir.c_str(), name, nullptr);
    std::optional<Manifest> manifest = LoadManifest(full);
    g_free(full);
    if (manifest.has_value()) {
      result.push_back(std::move(*manifest));
    }
  }

  g_dir_close(handle);
  return result;
}

}  // namespace dmgr
}  // namespace gxde
