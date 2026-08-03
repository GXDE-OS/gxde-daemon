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

#ifndef SRC_I18N_I18N_H_
#define SRC_I18N_I18N_H_

#include <string>

namespace gxde {
namespace dock {
namespace i18n {

// Initialize the i18n subsystem. Must be called once at startup.
// `translations_dir` is the path to the directory containing locale JSON files.
void init(const std::string& translations_dir);

// Translate a string. Returns the translation for the current locale if
// available, otherwise returns the original string unchanged.
const char* tr(const char* str);

}  // namespace i18n
}  // namespace dock
}  // namespace gxde

// Convenience macro: _(x) is the standard gettext convention.
#define _(x) gxde::dock::i18n::tr(x)

#endif  // SRC_I18N_I18N_H_