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

#ifndef SRC_DOCK_TYPES_H_
#define SRC_DOCK_TYPES_H_

#include <cstdint>

namespace gxde {
namespace dock {

enum class HideMode : int32_t {
  kKeepShowing = 0,
  kKeepHidden = 1,
  kAutoHide = 2,
  kSmartHide = 3,
};

enum class HideState : int32_t {
  kUnknown = 0,
  kShow = 1,
  kHide = 2,
};

enum class DisplayMode : int32_t {
  kFashion = 0,
  kEfficient = 1,
  kClassic = 2,
};

enum class Position : int32_t {
  kTop = 0,
  kRight = 1,
  kBottom = 2,
  kLeft = 3,
};

struct Rect {
  int32_t x = 0;
  int32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
};

inline constexpr char kBusName[] = "top.gxde.dock";
inline constexpr char kManagerPath[] = "/top/gxde/dock";
inline constexpr char kManagerInterface[] = "top.gxde.dock";
inline constexpr char kEntryPathPrefix[] = "/top/gxde/dock/entries/";
inline constexpr char kEntryInterface[] = "top.gxde.dock.Entry";

}  // namespace dock
}  // namespace gxde

#endif  // SRC_DOCK_TYPES_H_
