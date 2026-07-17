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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/app_entries/app_entries.h"

namespace gxde {
namespace dock {

AppEntry* AppEntries::GetByInnerId(const std::string& inner_id) const {
  for (const auto& entry : items_) {
    if (entry->inner_id() == inner_id) {
      return entry.get();
    }
  }
  return nullptr;
}

AppEntry* AppEntries::GetById(const std::string& id) const {
  for (const auto& entry : items_) {
    if (entry->id() == id) {
      return entry.get();
    }
  }
  return nullptr;
}

AppEntry* AppEntries::GetByWindow(uint32_t window_id) const {
  for (const auto& entry : items_) {
    if (entry->ContainsWindow(window_id)) {
      return entry.get();
    }
  }
  return nullptr;
}

AppEntry* AppEntries::GetByDesktopFile(const std::string& path) const {
  for (const auto& entry : items_) {
    if (entry->app_info() != nullptr &&
        entry->app_info()->file_name() == path) {
      return entry.get();
    }
  }
  return nullptr;
}

void AppEntries::Insert(std::unique_ptr<AppEntry> entry, int index) {
  if (index < 0 || index > static_cast<int>(items_.size())) {
    items_.push_back(std::move(entry));
  } else {
    items_.insert(items_.begin() + index, std::move(entry));
  }
}

std::unique_ptr<AppEntry> AppEntries::Remove(AppEntry* entry) {
  for (auto it = items_.begin(); it != items_.end(); ++it) {
    if (it->get() == entry) {
      std::unique_ptr<AppEntry> owned = std::move(*it);
      items_.erase(it);
      return owned;
    }
  }
  return nullptr;
}

bool AppEntries::Move(int from, int to) {
  int count = static_cast<int>(items_.size());
  if (from < 0 || from >= count || to < 0 || to >= count) {
    return false;
  }
  std::unique_ptr<AppEntry> moved = std::move(items_[from]);
  items_.erase(items_.begin() + from);
  items_.insert(items_.begin() + to, std::move(moved));
  return true;
}

std::vector<AppEntry*> AppEntries::items() const {
  std::vector<AppEntry*> result;
  result.reserve(items_.size());
  for (const auto& entry : items_) {
    result.push_back(entry.get());
  }
  return result;
}

std::vector<AppEntry*> AppEntries::FilterDocked() const {
  std::vector<AppEntry*> result;
  for (const auto& entry : items_) {
    if (entry->is_docked()) {
      result.push_back(entry.get());
    }
  }
  return result;
}

int AppEntries::IndexOf(AppEntry* entry) const {
  for (size_t i = 0; i < items_.size(); ++i) {
    if (items_[i].get() == entry) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace dock
}  // namespace gxde
