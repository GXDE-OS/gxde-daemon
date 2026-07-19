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

#include "src/dock_manager/dock_manager.h"

#include <glib/gstdio.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/app_info/app_info.h"
#include "src/window_identify/window_identify.h"

namespace gxde {
namespace dock {

namespace {

const char kManagerIntrospection[] = R"XML(<node>
  <interface name='top.gxde.daemon.dock'>
    <property name='HideMode' type='i' access='readwrite'/>
    <property name='DisplayMode' type='i' access='readwrite'/>
    <property name='Position' type='i' access='readwrite'/>
    <property name='IconSize' type='u' access='readwrite'/>
    <property name='ShowTimeout' type='u' access='readwrite'/>
    <property name='HideTimeout' type='u' access='readwrite'/>
    <property name='WindowSplit' type='b' access='readwrite'/>
    <property name='DockedApps' type='as' access='read'/>
    <property name='Entries' type='ao' access='read'/>
    <property name='Opacity' type='d' access='read'/>
    <property name='HideState' type='i' access='read'/>
    <property name='FrontendWindowRect' type='(iiuu)' access='read'/>
    <method name='ActivateWindow'><arg name='win' type='u' direction='in'/></method>
    <method name='CloseWindow'><arg name='win' type='u' direction='in'/></method>
    <method name='MaximizeWindow'><arg name='win' type='u' direction='in'/></method>
    <method name='MinimizeWindow'><arg name='win' type='u' direction='in'/></method>
    <method name='MakeWindowAbove'><arg name='win' type='u' direction='in'/></method>
    <method name='MoveWindow'><arg name='win' type='u' direction='in'/></method>
    <method name='PreviewWindow'><arg name='win' type='u' direction='in'/></method>
    <method name='CancelPreviewWindow'/>
    <method name='CaptureWindow'>
      <arg name='win' type='u' direction='in'/>
      <arg name='path' type='s' direction='in'/>
      <arg name='ok' type='b' direction='out'/>
    </method>
    <method name='GetEntryIDs'><arg name='list' type='as' direction='out'/></method>
    <method name='SetFrontendWindowRect'>
      <arg name='x' type='i' direction='in'/>
      <arg name='y' type='i' direction='in'/>
      <arg name='width' type='u' direction='in'/>
      <arg name='height' type='u' direction='in'/>
    </method>
    <method name='IsDocked'>
      <arg name='desktopFile' type='s' direction='in'/>
      <arg name='value' type='b' direction='out'/>
    </method>
    <method name='RequestDock'>
      <arg name='desktopFile' type='s' direction='in'/>
      <arg name='index' type='i' direction='in'/>
      <arg name='ok' type='b' direction='out'/>
    </method>
    <method name='RequestUndock'>
      <arg name='desktopFile' type='s' direction='in'/>
      <arg name='ok' type='b' direction='out'/>
    </method>
    <method name='MoveEntry'>
      <arg name='index' type='i' direction='in'/>
      <arg name='newIndex' type='i' direction='in'/>
    </method>
    <method name='IsOnDock'>
      <arg name='desktopFile' type='s' direction='in'/>
      <arg name='value' type='b' direction='out'/>
    </method>
    <method name='QueryWindowIdentifyMethod'>
      <arg name='win' type='u' direction='in'/>
      <arg name='identifyMethod' type='s' direction='out'/>
    </method>
    <method name='GetDockedAppsDesktopFiles'>
      <arg name='desktopFiles' type='as' direction='out'/>
    </method>
    <method name='SetPluginSettings'><arg name='jsonStr' type='s' direction='in'/></method>
    <method name='GetPluginSettings'><arg name='jsonStr' type='s' direction='out'/></method>
    <method name='MergePluginSettings'><arg name='jsonStr' type='s' direction='in'/></method>
    <method name='RemovePluginSettings'>
      <arg name='key1' type='s' direction='in'/>
      <arg name='key2List' type='as' direction='in'/>
    </method>
    <signal name='ServiceRestarted'/>
    <signal name='ServiceStarted'/>
    <signal name='ServiceStopped'/>
    <signal name='EntryAdded'>
      <arg name='path' type='o'/>
      <arg name='index' type='i'/>
    </signal>
    <signal name='EntryRemoved'><arg name='entryId' type='s'/></signal>
    <signal name='PluginSettingsSynced'/>
  </interface>
</node>)XML";

GDBusInterfaceInfo* ManagerInterfaceInfo() {
  static GDBusNodeInfo* node = [] {
    GError* error = nullptr;
    GDBusNodeInfo* info =
        g_dbus_node_info_new_for_xml(kManagerIntrospection, &error);
    if (info == nullptr) {
      g_error("(Dock) MGR: bad manager introspection: %s", error->message);
    }
    return info;
  }();
  return node->interfaces[0];
}

std::string ToLocalPath(const std::string& path) {
  if (path.rfind("file://", 0) == 0) {
    gchar* local = g_filename_from_uri(path.c_str(), nullptr, nullptr);
    std::string result = local != nullptr ? local : path;
    g_free(local);
    return result;
  }
  return path;
}

std::vector<std::pair<std::string, std::string>> SplitTopLevel(
    const std::string& object) {
  std::vector<std::pair<std::string, std::string>> pairs;
  size_t i = 0;
  auto skip_ws = [&]() {
    while (i < object.size() && g_ascii_isspace(object[i])) ++i;
  };
  skip_ws();
  if (i >= object.size() || object[i] != '{') {
    return pairs;
  }
  ++i;
  while (i < object.size()) {
    skip_ws();
    if (i < object.size() && object[i] == '}') break;
    if (object[i] != '"') break;
    size_t key_start = ++i;
    std::string key;
    while (i < object.size() && object[i] != '"') {
      if (object[i] == '\\' && i + 1 < object.size()) ++i;
      key.push_back(object[i]);
      ++i;
    }
    (void)key_start;
    ++i;
    skip_ws();
    if (i < object.size() && object[i] == ':') ++i;
    skip_ws();
    size_t val_start = i;
    int depth = 0;
    bool in_str = false;
    while (i < object.size()) {
      char c = object[i];
      if (in_str) {
        if (c == '\\' && i + 1 < object.size()) {
          ++i;
        } else if (c == '"') {
          in_str = false;
        }
      } else if (c == '"') {
        in_str = true;
      } else if (c == '{' || c == '[') {
        ++depth;
      } else if (c == '}' || c == ']') {
        if (depth == 0) break;
        --depth;
      } else if (c == ',' && depth == 0) {
        break;
      }
      ++i;
    }
    std::string value = object.substr(val_start, i - val_start);
    size_t begin = value.find_first_not_of(" \t\n\r");
    size_t end = value.find_last_not_of(" \t\n\r");
    if (begin != std::string::npos) {
      value = value.substr(begin, end - begin + 1);
    } else {
      value.clear();
    }
    pairs.emplace_back(key, value);
    if (i < object.size() && object[i] == ',') ++i;
  }
  return pairs;
}

std::string SerializeTopLevel(
    const std::vector<std::pair<std::string, std::string>>& pairs) {
  std::string out = "{";
  for (size_t i = 0; i < pairs.size(); ++i) {
    if (i > 0) out += ",";
    out += "\"" + pairs[i].first + "\":" + pairs[i].second;
  }
  out += "}";
  return out;
}

}  // namespace

DockManager::~DockManager() {
  if (registration_id_ != 0 && connection_ != nullptr) {
    g_dbus_connection_unregister_object(connection_, registration_id_);
  }
}

bool DockManager::Start(GDBusConnection* connection) {
  connection_ = connection;

  if (!settings_.Init()) {
    g_warning("(Dock) MGR: settings unavailable, continuing with defaults");
  }
  settings_.set_change_handler([this](const std::string& key) {
    if (key == "hide-mode") {
      EmitManagerPropertyChanged("HideMode",
                                 g_variant_new_int32(settings_.hide_mode()));
    } else if (key == "display-mode") {
      EmitManagerPropertyChanged("DisplayMode",
                                 g_variant_new_int32(settings_.display_mode()));
    } else if (key == "position") {
      EmitManagerPropertyChanged("Position",
                                 g_variant_new_int32(settings_.position()));
    } else if (key == "icon-size") {
      EmitManagerPropertyChanged("IconSize",
                                 g_variant_new_uint32(settings_.icon_size()));
    } else if (key == "show-timeout") {
      EmitManagerPropertyChanged(
          "ShowTimeout", g_variant_new_uint32(settings_.show_timeout()));
    } else if (key == "hide-timeout") {
      EmitManagerPropertyChanged(
          "HideTimeout", g_variant_new_uint32(settings_.hide_timeout()));
    } else if (key == "window-split") {
      EmitManagerPropertyChanged(
          "WindowSplit", g_variant_new_boolean(settings_.window_split()));
    } else if (key == "docked-apps") {
      GVariantBuilder b;
      g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
      for (const std::string& app : settings_.docked_apps()) {
        g_variant_builder_add(&b, "s", app.c_str());
      }
      EmitManagerPropertyChanged("DockedApps", g_variant_builder_end(&b));
    } else if (key == "plugin-settings") {
      g_dbus_connection_emit_signal(connection_, nullptr, kManagerPath,
                                    kManagerInterface, "PluginSettingsSynced",
                                    nullptr, nullptr);
    }
  });

  static const GDBusInterfaceVTable vtable = {&DockManager::OnMethodCall,
                                              &DockManager::OnGetProperty,
                                              &DockManager::OnSetProperty,
                                              {nullptr, nullptr, nullptr}};
  GError* error = nullptr;
  registration_id_ = g_dbus_connection_register_object(
      connection_, kManagerPath, ManagerInterfaceInfo(), &vtable, this, nullptr,
      &error);
  if (registration_id_ == 0) {
    g_warning("(Dock) MGR: register manager failed: %s", error->message);
    g_clear_error(&error);
    return false;
  }

  backend_ = CreateWindowBackend();
  if (!backend_->Init(this)) {
    g_warning("(Dock) MGR: window backend init failed");
    return false;
  }
  g_message("(Dock) MGR: backend=%s", backend_->Name());

  LoadDockedApps();

  for (const BackendWindow& window : backend_->ListWindows()) {
    OnWindowAdded(window);
  }
  active_window_ = backend_->ActiveWindow();
  for (AppEntry* entry : entries_.items()) {
    entry->RefreshActiveState(active_window_);
  }
  return true;
}

std::string DockManager::AllocEntryId() {
  return std::to_string(next_entry_id_++);
}

bool DockManager::WindowIconPreferred(const std::string& app_id) const {
  for (const std::string& app : settings_.win_icon_preferred_apps()) {
    if (app == app_id) {
      return true;
    }
  }
  return false;
}

void DockManager::LoadDockedApps() {
  int index = 0;
  for (const std::string& zipped : settings_.docked_apps()) {
    std::string path = UnzipDesktopPath(zipped);
    std::shared_ptr<AppInfo> info = AppInfo::FromFile(path);
    if (info == nullptr) {
      gchar* base = g_path_get_basename(path.c_str());
      info = AppInfo::FromDesktopId(base != nullptr ? base : "");
      g_free(base);
    }
    if (info == nullptr) {
      g_warning("(Dock) MGR: docked app %s not found", zipped.c_str());
      continue;
    }
    auto entry = std::make_unique<AppEntry>(this, AllocEntryId(),
                                            info->inner_id(), info);
    entry->set_docked(true);
    entry->Export();
    AppEntry* raw = entry.get();
    entries_.Insert(std::move(entry), -1);
    EmitEntryAdded(raw->object_path(), index++);
  }
}

AppEntry* DockManager::AttachOrCreateEntry(const BackendWindow& window) {
  IdentifyResult ident = IdentifyWindow(window);
  identify_methods_[window.id] = ident.method;

  AppEntry* entry = entries_.GetByInnerId(ident.inner_id);
  if (entry == nullptr) {
    auto owned = std::make_unique<AppEntry>(this, AllocEntryId(),
                                            ident.inner_id, ident.app_info);
    entry = owned.get();
    entry->Export();
    entries_.Insert(std::move(owned), -1);
    EmitEntryAdded(entry->object_path(), entries_.IndexOf(entry));
  }
  entry->AttachWindow(window);
  return entry;
}

void DockManager::OnWindowAdded(const BackendWindow& window) {
  AttachOrCreateEntry(window);
}

void DockManager::OnWindowChanged(const BackendWindow& window) {
  AppEntry* entry = entries_.GetByWindow(window.id);
  if (entry != nullptr) {
    entry->UpdateWindow(window);
  } else {
    AttachOrCreateEntry(window);
  }
}

void DockManager::OnWindowRemoved(uint32_t id) {
  identify_methods_.erase(id);
  AppEntry* entry = entries_.GetByWindow(id);
  if (entry == nullptr) {
    return;
  }
  if (entry->DetachWindow(id)) {
    RemoveEntry(entry);
  }
}

void DockManager::OnActiveWindowChanged(uint32_t id) {
  active_window_ = id;
  for (AppEntry* entry : entries_.items()) {
    entry->RefreshActiveState(id);
  }
}

void DockManager::RemoveEntry(AppEntry* entry) {
  EmitEntryRemoved(entry->id());
  std::unique_ptr<AppEntry> owned = entries_.Remove(entry);
}

bool DockManager::DockEntry(AppEntry* entry) {
  if (entry->is_docked()) {
    return false;
  }
  std::shared_ptr<AppInfo> info = entry->app_info();
  bool need_scratch = info == nullptr;
  if (info != nullptr && !info->installed()) {
    gchar* scratch_dir =
        g_build_filename(g_get_user_config_dir(), "dock/scratch/", nullptr);
    need_scratch =
        info->file_name().compare(0, strlen(scratch_dir), scratch_dir) != 0;
    g_free(scratch_dir);
  }
  if (need_scratch) {
    std::string path = CreateScratchDesktop(entry);
    if (!path.empty()) {
      std::shared_ptr<AppInfo> scratch = AppInfo::FromFile(path);
      if (scratch != nullptr) {
        entry->set_app_info(scratch);
        entry->set_inner_id(scratch->inner_id());
      }
    }
  }
  entry->set_docked(true);
  return true;
}

std::string DockManager::CreateScratchDesktop(AppEntry* entry) {
  gchar* scratch_dir =
      g_build_filename(g_get_user_config_dir(), "dock", "scratch", nullptr);
  g_mkdir_with_parents(scratch_dir, 0755);

  BackendWindow current;
  bool have_current = false;
  for (const BackendWindow& w : backend_->ListWindows()) {
    if (w.id == entry->current_window()) {
      current = w;
      have_current = true;
      break;
    }
  }

  std::string title =
      have_current && !current.title.empty() ? current.title : entry->id();
  std::string icon = have_current && !current.icon.empty()
                         ? current.icon
                         : "application-x-executable";
  std::string exec;
  if (have_current && current.pid != 0) {
    gchar* cmd_path = g_strdup_printf("/proc/%u/cmdline", current.pid);
    gchar* contents = nullptr;
    gsize length = 0;
    if (g_file_get_contents(cmd_path, &contents, &length, nullptr) &&
        length > 0) {
      for (gsize i = 0; i < length - 1; ++i) {
        if (contents[i] == '\0') contents[i] = ' ';
      }
      exec = contents;
    }
    g_free(contents);
    g_free(cmd_path);
  }
  if (exec.empty()) {
    g_free(scratch_dir);
    return "";
  }

  GKeyFile* key_file = g_key_file_new();
  g_key_file_set_string(key_file, "Desktop Entry", "Type", "Application");
  g_key_file_set_string(key_file, "Desktop Entry", "Name", title.c_str());
  g_key_file_set_string(key_file, "Desktop Entry", "Icon", icon.c_str());
  g_key_file_set_string(key_file, "Desktop Entry", "Exec", exec.c_str());
  g_key_file_set_boolean(key_file, "Desktop Entry", "Terminal", FALSE);

  gchar* base = g_strdup_printf("%s.desktop", entry->inner_id().c_str());
  for (char* p = base; *p != '\0'; ++p) {
    if (*p == '/' || *p == ':') *p = '_';
  }
  gchar* file_path = g_build_filename(scratch_dir, base, nullptr);
  g_key_file_save_to_file(key_file, file_path, nullptr);
  std::string result = file_path;

  g_key_file_unref(key_file);
  g_free(base);
  g_free(file_path);
  g_free(scratch_dir);
  return result;
}

void DockManager::UndockEntry(AppEntry* entry) {
  if (!entry->is_docked()) {
    return;
  }
  std::shared_ptr<AppInfo> info = entry->app_info();
  if (info != nullptr) {
    gchar* scratch_dir =
        g_build_filename(g_get_user_config_dir(), "dock/scratch/", nullptr);
    if (info->file_name().compare(0, strlen(scratch_dir), scratch_dir) == 0) {
      g_remove(info->file_name().c_str());
    }
    g_free(scratch_dir);
  }
  if (!entry->has_window()) {
    RemoveEntry(entry);
  } else {
    entry->set_docked(false);
  }
  SaveDockedApps();
}

void DockManager::SaveDockedApps() {
  std::vector<std::string> list;
  for (AppEntry* entry : entries_.FilterDocked()) {
    if (entry->app_info() != nullptr) {
      list.push_back(ZipDesktopPath(entry->app_info()->file_name()));
    }
  }
  settings_.set_docked_apps(list);
}

GVariant* DockManager::BuildEntriesVariant() const {
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE("ao"));
  for (AppEntry* entry : entries_.items()) {
    g_variant_builder_add(&builder, "o", entry->object_path().c_str());
  }
  return g_variant_builder_end(&builder);
}

void DockManager::EmitEntriesChanged() {
  EmitManagerPropertyChanged("Entries", BuildEntriesVariant());
}

void DockManager::EmitEntryAdded(const std::string& object_path,
                                 int32_t index) {
  g_dbus_connection_emit_signal(
      connection_, nullptr, kManagerPath, kManagerInterface, "EntryAdded",
      g_variant_new("(oi)", object_path.c_str(), index), nullptr);
  EmitEntriesChanged();
}

void DockManager::EmitEntryRemoved(const std::string& entry_id) {
  g_dbus_connection_emit_signal(
      connection_, nullptr, kManagerPath, kManagerInterface, "EntryRemoved",
      g_variant_new("(s)", entry_id.c_str()), nullptr);
  EmitEntriesChanged();
}

void DockManager::EmitServiceStarted() {
  g_dbus_connection_emit_signal(connection_, nullptr, kManagerPath,
                                kManagerInterface, "ServiceStarted", nullptr,
                                nullptr);
}

void DockManager::EmitServiceStopped() {
  g_dbus_connection_emit_signal(connection_, nullptr, kManagerPath,
                                kManagerInterface, "ServiceStopped", nullptr,
                                nullptr);
  g_dbus_connection_flush_sync(connection_, nullptr, nullptr);
}

void DockManager::EmitManagerPropertyChanged(const char* name,
                                             GVariant* value) {
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&builder, "{sv}", name, value);
  GVariant* params = g_variant_new(
      "(s@a{sv}@as)", kManagerInterface, g_variant_builder_end(&builder),
      g_variant_new_array(G_VARIANT_TYPE_STRING, nullptr, 0));
  g_dbus_connection_emit_signal(connection_, nullptr, kManagerPath,
                                "org.freedesktop.DBus.Properties",
                                "PropertiesChanged", params, nullptr);
}

GVariant* DockManager::OnGetProperty(GDBusConnection* /*connection*/,
                                     const gchar* /*sender*/,
                                     const gchar* /*object_path*/,
                                     const gchar* /*interface_name*/,
                                     const gchar* property_name,
                                     GError** /*error*/, gpointer user_data) {
  auto* self = static_cast<DockManager*>(user_data);
  std::string prop = property_name;
  if (prop == "HideMode")
    return g_variant_new_int32(self->settings_.hide_mode());
  if (prop == "DisplayMode")
    return g_variant_new_int32(self->settings_.display_mode());
  if (prop == "Position")
    return g_variant_new_int32(self->settings_.position());
  if (prop == "IconSize")
    return g_variant_new_uint32(self->settings_.icon_size());
  if (prop == "ShowTimeout")
    return g_variant_new_uint32(self->settings_.show_timeout());
  if (prop == "HideTimeout")
    return g_variant_new_uint32(self->settings_.hide_timeout());
  if (prop == "WindowSplit")
    return g_variant_new_boolean(self->settings_.window_split());
  if (prop == "Opacity") return g_variant_new_double(self->settings_.opacity());
  if (prop == "HideState")
    return g_variant_new_int32(static_cast<int32_t>(self->hide_state_));
  if (prop == "FrontendWindowRect") {
    const Rect& r = self->frontend_window_rect_;
    return g_variant_new("(iiuu)", r.x, r.y, r.width, r.height);
  }
  if (prop == "DockedApps") {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
    for (const std::string& app : self->settings_.docked_apps()) {
      g_variant_builder_add(&builder, "s", app.c_str());
    }
    return g_variant_builder_end(&builder);
  }
  if (prop == "Entries") return self->BuildEntriesVariant();
  return nullptr;
}

gboolean DockManager::OnSetProperty(GDBusConnection* /*connection*/,
                                    const gchar* /*sender*/,
                                    const gchar* /*object_path*/,
                                    const gchar* /*interface_name*/,
                                    const gchar* property_name, GVariant* value,
                                    GError** /*error*/, gpointer user_data) {
  auto* self = static_cast<DockManager*>(user_data);
  std::string prop = property_name;
  if (prop == "HideMode") {
    self->settings_.set_hide_mode(g_variant_get_int32(value));
  } else if (prop == "DisplayMode") {
    self->settings_.set_display_mode(g_variant_get_int32(value));
  } else if (prop == "Position") {
    self->settings_.set_position(g_variant_get_int32(value));
  } else if (prop == "IconSize") {
    self->settings_.set_icon_size(g_variant_get_uint32(value));
  } else if (prop == "ShowTimeout") {
    self->settings_.set_show_timeout(g_variant_get_uint32(value));
  } else if (prop == "HideTimeout") {
    self->settings_.set_hide_timeout(g_variant_get_uint32(value));
  } else if (prop == "WindowSplit") {
    self->settings_.set_window_split(g_variant_get_boolean(value));
  } else {
    return FALSE;
  }
  return TRUE;
}

void DockManager::OnMethodCall(GDBusConnection* /*connection*/,
                               const gchar* /*sender*/,
                               const gchar* /*object_path*/,
                               const gchar* /*interface_name*/,
                               const gchar* method_name, GVariant* parameters,
                               GDBusMethodInvocation* invocation,
                               gpointer user_data) {
  auto* self = static_cast<DockManager*>(user_data);
  std::string method = method_name;
  WindowBackend* backend = self->backend_.get();

  auto win_arg = [&]() {
    guint32 win = 0;
    g_variant_get(parameters, "(u)", &win);
    return win;
  };

  if (method == "ActivateWindow") {
    backend->Activate(win_arg());
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "CloseWindow") {
    backend->Close(win_arg());
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "MaximizeWindow") {
    backend->Maximize(win_arg());
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "MinimizeWindow") {
    backend->Minimize(win_arg());
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "MakeWindowAbove") {
    backend->MakeAbove(win_arg());
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "MoveWindow") {
    backend->MoveWindow(win_arg());
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "PreviewWindow") {
    guint32 win = win_arg();
    gchar* dir =
        g_build_filename(g_get_user_runtime_dir(), "gxde-dock", nullptr);
    g_mkdir_with_parents(dir, 0700);
    gchar* path = g_strdup_printf("%s/preview-%u.png", dir, win);
    if (backend->CaptureWindow(win, path)) {
      g_message("(Dock) MGR: window %u preview at %s", win, path);
    }
    g_free(path);
    g_free(dir);
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "CancelPreviewWindow") {
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "CaptureWindow") {
    guint32 win = 0;
    const gchar* path = nullptr;
    g_variant_get(parameters, "(u&s)", &win, &path);
    gboolean ok = backend->CaptureWindow(win, path != nullptr ? path : "");
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", ok));
  } else if (method == "GetEntryIDs") {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
    for (AppEntry* entry : self->entries_.items()) {
      g_variant_builder_add(&builder, "s", entry->id().c_str());
    }
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(as)", &builder));
  } else if (method == "SetFrontendWindowRect") {
    gint32 x = 0;
    gint32 y = 0;
    guint32 w = 0;
    guint32 h = 0;
    g_variant_get(parameters, "(iiuu)", &x, &y, &w, &h);
    self->frontend_window_rect_ = {x, y, w, h};
    self->EmitManagerPropertyChanged("FrontendWindowRect",
                                     g_variant_new("(iiuu)", x, y, w, h));
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "IsDocked" || method == "IsOnDock") {
    const gchar* file = nullptr;
    g_variant_get(parameters, "(&s)", &file);
    std::string path = ToLocalPath(file != nullptr ? file : "");
    AppEntry* entry = self->entries_.GetByDesktopFile(path);
    gboolean value =
        entry != nullptr && (method == "IsOnDock" || entry->is_docked());
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(b)", value));
  } else if (method == "RequestDock") {
    const gchar* file = nullptr;
    gint32 index = 0;
    g_variant_get(parameters, "(&si)", &file, &index);
    std::string path = ToLocalPath(file != nullptr ? file : "");
    std::shared_ptr<AppInfo> info = AppInfo::FromFile(path);
    gboolean ok = FALSE;
    if (info != nullptr) {
      AppEntry* entry = self->entries_.GetByInnerId(info->inner_id());
      bool created = entry == nullptr;
      if (created) {
        auto owned = std::make_unique<AppEntry>(self, self->AllocEntryId(),
                                                info->inner_id(), info);
        entry = owned.get();
        entry->Export();
        self->entries_.Insert(std::move(owned), index);
        self->EmitEntryAdded(entry->object_path(),
                             self->entries_.IndexOf(entry));
      }
      ok = self->DockEntry(entry);
      if (ok || created) {
        self->SaveDockedApps();
      }
    }
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", ok));
  } else if (method == "RequestUndock") {
    const gchar* file = nullptr;
    g_variant_get(parameters, "(&s)", &file);
    std::string path = ToLocalPath(file != nullptr ? file : "");
    AppEntry* entry = self->entries_.GetByDesktopFile(path);
    gboolean ok = FALSE;
    if (entry != nullptr && entry->is_docked()) {
      self->UndockEntry(entry);
      ok = TRUE;
    }
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", ok));
  } else if (method == "MoveEntry") {
    gint32 index = 0;
    gint32 new_index = 0;
    g_variant_get(parameters, "(ii)", &index, &new_index);
    self->entries_.Move(index, new_index);
    self->SaveDockedApps();
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "QueryWindowIdentifyMethod") {
    guint32 win = win_arg();
    auto it = self->identify_methods_.find(win);
    std::string value = it != self->identify_methods_.end() ? it->second : "";
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(s)", value.c_str()));
  } else if (method == "GetDockedAppsDesktopFiles") {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
    for (AppEntry* entry : self->entries_.FilterDocked()) {
      if (entry->app_info() != nullptr) {
        g_variant_builder_add(&builder, "s",
                              entry->app_info()->file_name().c_str());
      }
    }
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(as)", &builder));
  } else if (method == "GetPluginSettings") {
    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(s)", self->settings_.plugin_settings().c_str()));
  } else if (method == "SetPluginSettings") {
    const gchar* json = nullptr;
    g_variant_get(parameters, "(&s)", &json);
    self->settings_.set_plugin_settings(json != nullptr ? json : "{}");
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "MergePluginSettings") {
    const gchar* json = nullptr;
    g_variant_get(parameters, "(&s)", &json);
    auto merged = SplitTopLevel(self->settings_.plugin_settings());
    for (auto& [k, v] : SplitTopLevel(json != nullptr ? json : "{}")) {
      bool replaced = false;
      for (auto& existing : merged) {
        if (existing.first == k) {
          existing.second = v;
          replaced = true;
          break;
        }
      }
      if (!replaced) merged.emplace_back(k, v);
    }
    self->settings_.set_plugin_settings(SerializeTopLevel(merged));
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "RemovePluginSettings") {
    const gchar* key1 = nullptr;
    GVariantIter* iter = nullptr;
    g_variant_get(parameters, "(&sas)", &key1, &iter);
    std::vector<std::string> key2;
    const gchar* k = nullptr;
    while (iter != nullptr && g_variant_iter_next(iter, "&s", &k))
      key2.emplace_back(k);
    if (iter != nullptr) g_variant_iter_free(iter);

    auto top = SplitTopLevel(self->settings_.plugin_settings());
    std::string want = key1 != nullptr ? key1 : "";
    if (key2.empty()) {
      top.erase(std::remove_if(top.begin(), top.end(),
                               [&](const auto& p) { return p.first == want; }),
                top.end());
    } else {
      for (auto& p : top) {
        if (p.first == want) {
          auto sub = SplitTopLevel(p.second);
          sub.erase(std::remove_if(sub.begin(), sub.end(),
                                   [&](const auto& s) {
                                     return std::find(key2.begin(), key2.end(),
                                                      s.first) != key2.end();
                                   }),
                    sub.end());
          p.second = SerializeTopLevel(sub);
        }
      }
    }
    self->settings_.set_plugin_settings(SerializeTopLevel(top));
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "Unknown method %s", method_name);
  }
}

}  // namespace dock
}  // namespace gxde
