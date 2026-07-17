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

#ifndef SRC_WINDOW_IDENTIFY_WINDOW_IDENTIFY_H_
#define SRC_WINDOW_IDENTIFY_WINDOW_IDENTIFY_H_

#include <memory>
#include <string>

#include "src/app_info/app_info.h"
#include "src/backend/window_backend.h"

namespace gxde {
namespace dock {

struct IdentifyResult {
  std::shared_ptr<AppInfo> app_info;
  std::string inner_id;
  std::string method;
};

IdentifyResult IdentifyWindow(const BackendWindow& window);

}  // namespace dock
}  // namespace gxde

#endif  // SRC_WINDOW_IDENTIFY_WINDOW_IDENTIFY_H_
