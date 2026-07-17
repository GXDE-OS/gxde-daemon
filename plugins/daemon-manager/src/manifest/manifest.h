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

#ifndef SRC_MANIFEST_MANIFEST_H_
#define SRC_MANIFEST_MANIFEST_H_

#include <optional>
#include <string>
#include <vector>

namespace gxde {
namespace dmgr {

struct Manifest {
  std::string name;
  std::string description;
  std::string version;
  std::string exec;
  std::string maintainer;
  std::vector<std::string> bus_names;
  bool restart = true;
  bool resident = false;
  std::string path;

  // Whether the manager should launch and watch this plugin. Derived, not a
  // manifest field: a plugin needs supervision iff it wants to be restarted on
  // exit (restart) or to outlive the manager / be adopted (resident).
  bool NeedsSupervision() const { return restart || resident; }
};

std::optional<Manifest> LoadManifest(const std::string& file_path);
std::vector<Manifest> LoadManifests(const std::string& dir);

}  // namespace dmgr
}  // namespace gxde

#endif  // SRC_MANIFEST_MANIFEST_H_
