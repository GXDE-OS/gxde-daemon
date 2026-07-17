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

#ifndef SRC_APP_ENTRIES_APP_ENTRIES_H_
#define SRC_APP_ENTRIES_APP_ENTRIES_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "src/app_entry/app_entry.h"

namespace gxde {
namespace dock {

class AppEntries {
 public:
  AppEntry* GetByInnerId(const std::string& inner_id) const;
  AppEntry* GetById(const std::string& id) const;
  AppEntry* GetByWindow(uint32_t window_id) const;
  AppEntry* GetByDesktopFile(const std::string& path) const;

  void Insert(std::unique_ptr<AppEntry> entry, int index);
  std::unique_ptr<AppEntry> Remove(AppEntry* entry);
  bool Move(int from, int to);

  std::vector<AppEntry*> items() const;
  std::vector<AppEntry*> FilterDocked() const;
  int IndexOf(AppEntry* entry) const;
  size_t size() const { return items_.size(); }

 private:
  std::vector<std::unique_ptr<AppEntry>> items_;
};

}  // namespace dock
}  // namespace gxde

#endif  // SRC_APP_ENTRIES_APP_ENTRIES_H_
