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

#include "src/app_entry/app_entry.h"
#include "src/dock_manager/dock_manager.h"
#include "src/dock_types.h"

namespace gxde {
namespace dock {

namespace {

const char kEntryIntrospection[] = R"XML(<node>
  <interface name='top.gxde.dock.Entry'>
    <property name='Id' type='s' access='read'/>
    <property name='IsActive' type='b' access='read'/>
    <property name='Name' type='s' access='read'/>
    <property name='Icon' type='s' access='read'/>
    <property name='DesktopFile' type='s' access='read'/>
    <property name='CurrentWindow' type='u' access='read'/>
    <property name='IsDocked' type='b' access='read'/>
    <property name='WindowInfos' type='a{u(sb)}' access='read'/>
    <property name='Menu' type='s' access='read'/>
    <method name='Activate'>
      <arg name='timestamp' type='u' direction='in'/>
    </method>
    <method name='HandleMenuItem'>
      <arg name='timestamp' type='u' direction='in'/>
      <arg name='id' type='s' direction='in'/>
    </method>
    <method name='HandleDragDrop'>
      <arg name='timestamp' type='u' direction='in'/>
      <arg name='files' type='as' direction='in'/>
    </method>
    <method name='NewInstance'>
      <arg name='timestamp' type='u' direction='in'/>
    </method>
    <method name='RequestDock'/>
    <method name='RequestUndock'/>
    <method name='PresentWindows'/>
    <method name='Check'/>
    <method name='ForceQuit'/>
    <method name='GetAllowedCloseWindows'>
      <arg name='windows' type='au' direction='out'/>
    </method>
  </interface>
</node>)XML";

GDBusInterfaceInfo* EntryInterfaceInfo() {
  static GDBusNodeInfo* node = [] {
    GError* error = nullptr;
    GDBusNodeInfo* info =
        g_dbus_node_info_new_for_xml(kEntryIntrospection, &error);
    if (info == nullptr) {
      g_error("(Dock) Introspection: Bad entry introspection: %s",
              error->message);
    }
    return info;
  }();
  return node->interfaces[0];
}

std::vector<std::string> ReadCmdline(uint32_t pid) {
  std::vector<std::string> argv;
  if (pid == 0) {
    return argv;
  }
  gchar* path = g_strdup_printf("/proc/%u/cmdline", pid);
  gchar* contents = nullptr;
  gsize length = 0;
  if (g_file_get_contents(path, &contents, &length, nullptr)) {
    gsize start = 0;
    for (gsize i = 0; i < length; ++i) {
      if (contents[i] == '\0') {
        if (i > start) {
          argv.emplace_back(contents + start);
        }
        start = i + 1;
      }
    }
  }
  g_free(contents);
  g_free(path);
  return argv;
}

}  // namespace

AppEntry::AppEntry(DockManager* manager, std::string id, std::string inner_id,
                   std::shared_ptr<AppInfo> app_info)
    : manager_(manager),
      id_(std::move(id)),
      inner_id_(std::move(inner_id)),
      app_info_(std::move(app_info)) {
  object_path_ = std::string(kEntryPathPrefix) + id_;
  set_app_info(app_info_);
}

AppEntry::~AppEntry() { Unexport(); }

void AppEntry::set_app_info(std::shared_ptr<AppInfo> info) {
  app_info_ = std::move(info);
  if (app_info_ == nullptr) {
    win_icon_preferred_ = true;
    desktop_file_.clear();
  } else {
    desktop_file_ = app_info_->file_name();
    win_icon_preferred_ = manager_->WindowIconPreferred(app_info_->id()) ||
                          app_info_->icon().empty();
  }
  name_ = CurrentName();
  icon_ = CurrentIcon();
}

bool AppEntry::Export() {
  static const GDBusInterfaceVTable vtable = {&AppEntry::OnMethodCall,
                                              &AppEntry::OnGetProperty,
                                              nullptr,
                                              {nullptr, nullptr, nullptr}};

  GError* error = nullptr;
  registration_id_ = g_dbus_connection_register_object(
      manager_->connection(), object_path_.c_str(), EntryInterfaceInfo(),
      &vtable, this, nullptr, &error);

  if (registration_id_ == 0) {
    g_warning("(Dock) Register: register entry %s failed. %s",
              object_path_.c_str(), error->message);
    g_clear_error(&error);
    return false;
  }
  return true;
}

void AppEntry::Unexport() {
  if (registration_id_ != 0) {
    g_dbus_connection_unregister_object(manager_->connection(),
                                        registration_id_);
    registration_id_ = 0;
  }
}

std::vector<uint32_t> AppEntry::window_ids() const {
  std::vector<uint32_t> ids;
  ids.reserve(windows_.size());
  for (const auto& [id, window] : windows_) {
    ids.push_back(id);
  }
  return ids;
}

void AppEntry::set_docked(bool docked) {
  if (is_docked_ == docked) {
    return;
  }
  is_docked_ = docked;
  EmitChanged("IsDocked", g_variant_new_boolean(docked));
  UpdateMenu();
}

bool AppEntry::AttachWindow(const BackendWindow& window) {
  if (windows_.count(window.id) != 0) {
    windows_[window.id] = window;
    return false;
  }
  windows_[window.id] = window;
  if (current_window_ == 0) {
    current_window_ = window.id;
    EmitChanged("CurrentWindow", g_variant_new_uint32(current_window_));
  }
  UpdateName();
  UpdateIcon();
  UpdateWindowInfos();
  UpdateIsActive();
  UpdateMenu();
  return true;
}

bool AppEntry::DetachWindow(uint32_t window_id) {
  windows_.erase(window_id);
  if (windows_.empty()) {
    if (!is_docked_) {
      return true;
    }
    current_window_ = 0;
    EmitChanged("CurrentWindow", g_variant_new_uint32(0));
  } else if (current_window_ == window_id) {
    current_window_ = windows_.begin()->first;
    EmitChanged("CurrentWindow", g_variant_new_uint32(current_window_));
  }
  UpdateName();
  UpdateIcon();
  UpdateWindowInfos();
  UpdateIsActive();
  UpdateMenu();
  return false;
}

void AppEntry::UpdateWindow(const BackendWindow& window) {
  windows_[window.id] = window;
  UpdateWindowInfos();
  if (window.id == current_window_) {
    UpdateName();
    UpdateIcon();
  }
}

void AppEntry::RefreshActiveState(uint32_t active_window_id) {
  bool active = windows_.count(active_window_id) != 0;
  if (active && current_window_ != active_window_id) {
    current_window_ = active_window_id;
    EmitChanged("CurrentWindow", g_variant_new_uint32(current_window_));
  }
  if (active != is_active_) {
    is_active_ = active;
    EmitChanged("IsActive", g_variant_new_boolean(active));
  }
}

std::string AppEntry::CurrentName() const {
  if (app_info_ != nullptr && !app_info_->name().empty()) {
    return app_info_->name();
  }
  auto it = windows_.find(current_window_);
  if (it != windows_.end() && !it->second.title.empty()) {
    return it->second.title;
  }
  return name_;
}

std::string AppEntry::CurrentIcon() const {
  auto it = windows_.find(current_window_);
  const BackendWindow* current = it != windows_.end() ? &it->second : nullptr;

  if (!windows_.empty() && current != nullptr) {
    if (win_icon_preferred_ && !current->icon.empty()) {
      return current->icon;
    }
    if (app_info_ != nullptr && !app_info_->icon().empty()) {
      return app_info_->icon();
    }
    if (!current->icon.empty()) {
      return current->icon;
    }
  }
  if (app_info_ != nullptr && !app_info_->icon().empty()) {
    return app_info_->icon();
  }
  return "application-x-executable";
}

void AppEntry::UpdateName() {
  std::string name = CurrentName();
  if (name != name_) {
    name_ = name;
    EmitChanged("Name", g_variant_new_string(name_.c_str()));
  }
}

void AppEntry::UpdateIcon() {
  std::string icon = CurrentIcon();
  if (icon != icon_) {
    icon_ = icon;
    EmitChanged("Icon", g_variant_new_string(icon_.c_str()));
  }
}

void AppEntry::UpdateDesktopFile() {
  EmitChanged("DesktopFile", g_variant_new_string(desktop_file_.c_str()));
}

void AppEntry::UpdateWindowInfos() {
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE("a{u(sb)}"));
  for (const auto& [id, window] : windows_) {
    g_variant_builder_add(&builder, "{u(sb)}", id, window.title.c_str(),
                          static_cast<gboolean>(FALSE));
  }
  EmitChanged("WindowInfos", g_variant_builder_end(&builder));
}

void AppEntry::UpdateIsActive() {
  bool active = windows_.count(manager_->active_window()) != 0;
  if (active != is_active_) {
    is_active_ = active;
    EmitChanged("IsActive", g_variant_new_boolean(active));
  }
}

void AppEntry::UpdateMenu() {
  std::string menu = BuildMenuJson();
  if (menu != menu_json_) {
    menu_json_ = menu;
    EmitChanged("Menu", g_variant_new_string(menu_json_.c_str()));
  }
}

std::string AppEntry::BuildMenuJson() const {
  GVariantBuilder items;
  g_variant_builder_init(&items, G_VARIANT_TYPE("aa{sv}"));

  auto add_item = [&items](const char* id, const char* name) {
    GVariantBuilder item;
    g_variant_builder_init(&item, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&item, "{sv}", "id", g_variant_new_string(id));
    g_variant_builder_add(&item, "{sv}", "name", g_variant_new_string(name));
    g_variant_builder_add_value(&items, g_variant_builder_end(&item));
  };

  add_item("launch", has_window() ? "Open" : "Launch");
  if (app_info_ != nullptr) {
    GDesktopAppInfo* info =
        g_desktop_app_info_new_from_filename(app_info_->file_name().c_str());
    if (info != nullptr) {
      const gchar* const* actions = g_desktop_app_info_list_actions(info);
      for (int i = 0; actions != nullptr && actions[i] != nullptr; ++i) {
        gchar* label = g_desktop_app_info_get_action_name(info, actions[i]);
        std::string action_id = std::string("action:") + actions[i];
        add_item(action_id.c_str(), label != nullptr ? label : actions[i]);
        g_free(label);
      }
      g_object_unref(info);
    }
  }

  if (has_window()) {
    add_item("all-windows", "All Windows");
    add_item("force-quit", "Force Quit");
    add_item("close-all", "Close All");
  }
  add_item(is_docked_ ? "undock" : "dock", is_docked_ ? "Undock" : "Dock");

  GVariant* array = g_variant_builder_end(&items);
  gchar* json = g_variant_print(array, TRUE);
  std::string result = json != nullptr ? json : "[]";
  g_free(json);
  g_variant_unref(g_variant_ref_sink(array));
  return result;
}

void AppEntry::EmitChanged(const char* name, GVariant* value) {
  if (registration_id_ == 0) {
    if (value != nullptr) {
      g_variant_unref(g_variant_ref_sink(value));
    }
    return;
  }
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&builder, "{sv}", name, value);
  GVariant* params = g_variant_new(
      "(s@a{sv}@as)", kEntryInterface, g_variant_builder_end(&builder),
      g_variant_new_array(G_VARIANT_TYPE_STRING, nullptr, 0));

  g_dbus_connection_emit_signal(
      manager_->connection(), nullptr, object_path_.c_str(),
      "org.freedesktop.DBus.Properties", "PropertiesChanged", params, nullptr);
}

GVariant* AppEntry::OnGetProperty(GDBusConnection* /*connection*/,
                                  const gchar* /*sender*/,
                                  const gchar* /*object_path*/,
                                  const gchar* /*interface_name*/,
                                  const gchar* property_name,
                                  GError** /*error*/, gpointer user_data) {
  auto* self = static_cast<AppEntry*>(user_data);
  std::string prop = property_name;
  if (prop == "Id") {
    return g_variant_new_string(self->id_.c_str());
  }
  if (prop == "IsActive") {
    return g_variant_new_boolean(self->is_active_);
  }
  if (prop == "Name") {
    return g_variant_new_string(self->name_.c_str());
  }
  if (prop == "Icon") {
    return g_variant_new_string(self->icon_.c_str());
  }
  if (prop == "DesktopFile") {
    return g_variant_new_string(self->desktop_file_.c_str());
  }
  if (prop == "CurrentWindow") {
    return g_variant_new_uint32(self->current_window_);
  }
  if (prop == "IsDocked") {
    return g_variant_new_boolean(self->is_docked_);
  }
  if (prop == "Menu") {
    return g_variant_new_string(self->menu_json_.c_str());
  }
  if (prop == "WindowInfos") {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{u(sb)}"));
    for (const auto& [id, window] : self->windows_) {
      g_variant_builder_add(&builder, "{u(sb)}", id, window.title.c_str(),
                            static_cast<gboolean>(FALSE));
    }
    return g_variant_builder_end(&builder);
  }
  return nullptr;
}

void AppEntry::OnMethodCall(GDBusConnection* /*connection*/,
                            const gchar* /*sender*/,
                            const gchar* /*object_path*/,
                            const gchar* /*interface_name*/,
                            const gchar* method_name, GVariant* parameters,
                            GDBusMethodInvocation* invocation,
                            gpointer user_data) {
  auto* self = static_cast<AppEntry*>(user_data);
  std::string method = method_name;

  if (method == "Activate") {
    guint32 timestamp = 0;
    g_variant_get(parameters, "(u)", &timestamp);
    self->Activate(timestamp);
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "HandleMenuItem") {
    guint32 timestamp = 0;
    const gchar* id = nullptr;
    g_variant_get(parameters, "(u&s)", &timestamp, &id);
    self->HandleMenuItem(id != nullptr ? id : "");
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "HandleDragDrop") {
    guint32 timestamp = 0;
    GVariantIter* iter = nullptr;
    g_variant_get(parameters, "(uas)", &timestamp, &iter);
    std::vector<std::string> files;
    const gchar* file = nullptr;
    while (iter != nullptr && g_variant_iter_next(iter, "&s", &file)) {
      files.emplace_back(file);
    }
    if (iter != nullptr) {
      g_variant_iter_free(iter);
    }
    self->LaunchApp(files);
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "NewInstance") {
    self->LaunchApp({});
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "RequestDock") {
    if (self->manager_->DockEntry(self)) {
      self->manager_->SaveDockedApps();
    }
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "RequestUndock") {
    self->manager_->UndockEntry(self);
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "PresentWindows") {
    g_message(
        "dock: PresentWindows is a compositor feature not exposed by the "
        "backend");
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "Check") {
    self->UpdateIsActive();
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "ForceQuit") {
    self->ForceQuit();
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "GetAllowedCloseWindows") {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("au"));
    for (uint32_t id : self->AllowedCloseWindows()) {
      g_variant_builder_add(&builder, "u", id);
    }
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(au)", &builder));
  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "Unknown method %s", method_name);
  }
}

void AppEntry::Activate(uint32_t /*timestamp*/) {
  if (!has_window()) {
    LaunchApp({});
    return;
  }
  uint32_t win =
      current_window_ != 0 ? current_window_ : windows_.begin()->first;
  WindowBackend* backend = manager_->backend();
  if (manager_->active_window() == win) {
    backend->Minimize(win);
  } else {
    backend->Activate(win);
  }
}

void AppEntry::LaunchApp(const std::vector<std::string>& files) {
  if (app_info_ != nullptr) {
    app_info_->Launch(files);
    return;
  }
  auto it = windows_.find(current_window_);
  if (it == windows_.end()) {
    return;
  }
  std::vector<std::string> argv = ReadCmdline(it->second.pid);
  if (argv.empty()) {
    g_warning("dock: cannot relaunch entry %s (no appinfo, no cmdline)",
              id_.c_str());
    return;
  }
  std::vector<char*> c_argv;
  for (std::string& a : argv) {
    c_argv.push_back(a.data());
  }
  c_argv.push_back(nullptr);
  GError* error = nullptr;
  if (!g_spawn_async(nullptr, c_argv.data(), nullptr, G_SPAWN_SEARCH_PATH,
                     nullptr, nullptr, nullptr, &error)) {
    g_warning("dock: relaunch failed: %s",
              error != nullptr ? error->message : "unknown");
    g_clear_error(&error);
  }
}

void AppEntry::HandleMenuItem(const std::string& item_id) {
  if (item_id == "launch") {
    LaunchApp({});
  } else if (item_id == "close-all") {
    CloseAllWindows();
  } else if (item_id == "force-quit") {
    ForceQuit();
  } else if (item_id == "dock") {
    if (manager_->DockEntry(this)) {
      manager_->SaveDockedApps();
    }
  } else if (item_id == "undock") {
    manager_->UndockEntry(this);
  } else if (item_id == "all-windows") {
    for (uint32_t id : window_ids()) {
      manager_->backend()->Activate(id);
    }
  } else if (item_id.rfind("action:", 0) == 0 && app_info_ != nullptr) {
    GDesktopAppInfo* info =
        g_desktop_app_info_new_from_filename(app_info_->file_name().c_str());
    if (info != nullptr) {
      g_desktop_app_info_launch_action(info, item_id.substr(7).c_str(),
                                       nullptr);
      g_object_unref(info);
    }
  }
}

void AppEntry::CloseAllWindows() {
  for (uint32_t id : window_ids()) {
    manager_->backend()->Close(id);
  }
}

void AppEntry::ForceQuit() {
  for (uint32_t id : window_ids()) {
    manager_->backend()->KillClient(id);
  }
}

std::vector<uint32_t> AppEntry::AllowedCloseWindows() const {
  std::vector<uint32_t> result;
  for (const auto& [id, window] : windows_) {
    if (window.allowed_close) {
      result.push_back(id);
    }
  }
  return result;
}

}  // namespace dock
}  // namespace gxde
