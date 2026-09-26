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

#include "src/backend/wayland_backend/wayland_backend.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "kywc-capture-v1-client-protocol.h"
#include "kywc-toplevel-v1-client-protocol.h"

namespace gxde {
namespace dock {

namespace {

using Toplevel = WaylandBackend::Toplevel;

// Highest protocol versions this backend understands.
constexpr uint32_t kToplevelManagerVersion = 1;
constexpr uint32_t kCaptureManagerVersion = 1;

template <typename T>
void UpdateField(Toplevel* toplevel, T* field, const T& value) {
  if (*field != value) {
    *field = value;
    toplevel->dirty = true;
  }
}

std::string SafeString(const char* s) { return s != nullptr ? s : ""; }

// KYWC TOPLEVEL_V1
void ToplevelClosed(void* data, kywc_toplevel_v1* /*handle*/) {
  auto* t = static_cast<Toplevel*>(data);
  t->backend->HandleToplevelClosed(t);
}

void ToplevelDone(void* data, kywc_toplevel_v1* /*handle*/) {
  auto* t = static_cast<Toplevel*>(data);
  t->backend->HandleToplevelDone(t);
}

void ToplevelTitle(void* data, kywc_toplevel_v1* /*handle*/,
                   const char* title) {
  auto* t = static_cast<Toplevel*>(data);
  UpdateField(t, &t->title, SafeString(title));
}

void ToplevelAppId(void* data, kywc_toplevel_v1* /*handle*/,
                   const char* app_id) {
  auto* t = static_cast<Toplevel*>(data);
  UpdateField(t, &t->app_id, SafeString(app_id));
}

void ToplevelPrimaryOutput(void* /*data*/, kywc_toplevel_v1* /*handle*/,
                           const char* /*output*/) {}

void ToplevelWorkspaceEnter(void* /*data*/, kywc_toplevel_v1* /*handle*/,
                            const char* /*workspace*/) {}

void ToplevelWorkspaceLeave(void* /*data*/, kywc_toplevel_v1* /*handle*/,
                            const char* /*workspace*/) {}

void ToplevelCapabilities(void* data, kywc_toplevel_v1* /*handle*/,
                          uint32_t flags) {
  auto* t = static_cast<Toplevel*>(data);
  UpdateField(t, &t->capabilities, flags);
}

void ToplevelState(void* data, kywc_toplevel_v1* /*handle*/, uint32_t state) {
  auto* t = static_cast<Toplevel*>(data);
  UpdateField(t, &t->state, state);
}

void ToplevelParent(void* data, kywc_toplevel_v1* /*handle*/,
                    kywc_toplevel_v1* parent) {
  auto* t = static_cast<Toplevel*>(data);
  Toplevel* parent_toplevel =
      parent != nullptr
          ? static_cast<Toplevel*>(kywc_toplevel_v1_get_user_data(parent))
          : nullptr;
  UpdateField(t, &t->parent, parent_toplevel);
}

void ToplevelIcon(void* data, kywc_toplevel_v1* /*handle*/, const char* name) {
  auto* t = static_cast<Toplevel*>(data);
  UpdateField(t, &t->icon, SafeString(name));
}

void ToplevelGeometry(void* data, kywc_toplevel_v1* /*handle*/, int32_t x,
                      int32_t y, uint32_t width, uint32_t height) {
  auto* t = static_cast<Toplevel*>(data);
  UpdateField(t, &t->x, x);
  UpdateField(t, &t->y, y);
  UpdateField(t, &t->width, width);
  UpdateField(t, &t->height, height);
}

void ToplevelPid(void* data, kywc_toplevel_v1* /*handle*/, uint32_t pid) {
  auto* t = static_cast<Toplevel*>(data);
  UpdateField(t, &t->pid, pid);
}

const kywc_toplevel_v1_listener kToplevelListener = {
    .closed = ToplevelClosed,
    .done = ToplevelDone,
    .title = ToplevelTitle,
    .app_id = ToplevelAppId,
    .primary_output = ToplevelPrimaryOutput,
    .workspace_enter = ToplevelWorkspaceEnter,
    .workspace_leave = ToplevelWorkspaceLeave,
    .capabilities = ToplevelCapabilities,
    .state = ToplevelState,
    .parent = ToplevelParent,
    .icon = ToplevelIcon,
    .geometry = ToplevelGeometry,
    .pid = ToplevelPid,
};

void ManagerToplevel(void* data, kywc_toplevel_manager_v1* /*manager*/,
                     kywc_toplevel_v1* handle, const char* uuid) {
  static_cast<WaylandBackend*>(data)->HandleNewToplevel(handle, uuid);
}

void ManagerFinished(void* data, kywc_toplevel_manager_v1* /*manager*/) {
  static_cast<WaylandBackend*>(data)->HandleManagerFinished();
}

const kywc_toplevel_manager_v1_listener kManagerListener = {
    .toplevel = ManagerToplevel,
    .finished = ManagerFinished,
};

void RegistryGlobal(void* data, wl_registry* registry, uint32_t name,
                    const char* interface, uint32_t version) {
  static_cast<WaylandBackend*>(data)->HandleGlobal(registry, name, interface,
                                                   version);
}

void RegistryGlobalRemove(void* /*data*/, wl_registry* /*registry*/,
                          uint32_t /*name*/) {}

const wl_registry_listener kRegistryListener = {
    .global = RegistryGlobal,
    .global_remove = RegistryGlobalRemove,
};

}  // namespace

WaylandBackend::~WaylandBackend() {
  if (io_watch_ != 0) {
    g_source_remove(io_watch_);
  }
  if (io_channel_ != nullptr) {
    g_io_channel_unref(io_channel_);
  }
  for (auto& [id, toplevel] : tracked_) {
    kywc_toplevel_v1_destroy(toplevel->handle);
  }
  tracked_.clear();
  if (toplevel_manager_ != nullptr) {
    kywc_toplevel_manager_v1_stop(toplevel_manager_);
    kywc_toplevel_manager_v1_destroy(toplevel_manager_);
  }
  if (capture_manager_ != nullptr) {
    kywc_capture_manager_v1_destroy(capture_manager_);
  }
  if (registry_ != nullptr) {
    wl_registry_destroy(registry_);
  }
  if (display_ != nullptr) {
    wl_display_flush(display_);
    wl_display_disconnect(display_);
  }
}

bool WaylandBackend::Init(WindowObserver* observer) {
  observer_ = observer;
  display_ = wl_display_connect(nullptr);
  if (display_ == nullptr) {
    g_warning("(Dock) Wayland: Connect to compositor failed");
    return false;
  }

  registry_ = wl_display_get_registry(display_);
  wl_registry_add_listener(registry_, &kRegistryListener, this);
  if (wl_display_roundtrip(display_) < 0) {
    g_warning("(Dock) Wayland: Registry roundtrip failed");
    return false;
  }
  if (toplevel_manager_ == nullptr) {
    g_warning("(Dock) Wayland: Compositor lacks %s",
              kywc_toplevel_manager_v1_interface.name);
    return false;
  }
  if (capture_manager_ == nullptr) {
    g_message("(Dock) Wayland: Compositor lacks %s, falling back to grim",
              kywc_capture_manager_v1_interface.name);
  }
  // Receive the initial set of toplevels and their properties.
  wl_display_roundtrip(display_);

  int fd = wl_display_get_fd(display_);
  io_channel_ = g_io_channel_unix_new(fd);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  auto watch_cond = static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR);
  io_watch_ = g_io_add_watch(io_channel_, watch_cond,
                             &WaylandBackend::OnFdReadable, this);
  return true;
}

void WaylandBackend::HandleGlobal(wl_registry* registry, uint32_t name,
                                  const char* interface, uint32_t version) {
  if (toplevel_manager_ == nullptr &&
      std::strcmp(interface, kywc_toplevel_manager_v1_interface.name) == 0) {
    toplevel_manager_ = static_cast<kywc_toplevel_manager_v1*>(wl_registry_bind(
        registry, name, &kywc_toplevel_manager_v1_interface,
        std::min(version, kToplevelManagerVersion)));
    kywc_toplevel_manager_v1_add_listener(toplevel_manager_, &kManagerListener,
                                          this);
  } else if (capture_manager_ == nullptr &&
             std::strcmp(interface, kywc_capture_manager_v1_interface.name) ==
                 0) {
    capture_manager_ = static_cast<kywc_capture_manager_v1*>(wl_registry_bind(
        registry, name, &kywc_capture_manager_v1_interface,
        std::min(version, kCaptureManagerVersion)));
  }
}

gboolean WaylandBackend::OnFdReadable(GIOChannel* /*source*/,
                                      GIOCondition condition, gpointer data) {
  auto* self = static_cast<WaylandBackend*>(data);
  if ((condition & (G_IO_HUP | G_IO_ERR)) != 0) {
    g_warning("(Dock) Wayland: Connection lost");
    self->io_watch_ = 0;
    return G_SOURCE_REMOVE;
  }
  
  while (wl_display_prepare_read(self->display_) != 0) {
    wl_display_dispatch_pending(self->display_);
  }
  if (wl_display_read_events(self->display_) < 0 ||
      wl_display_dispatch_pending(self->display_) < 0) {
    g_warning("(Dock) Wayland: Dispatch failed");
    self->io_watch_ = 0;
    return G_SOURCE_REMOVE;
  }
  if (wl_display_flush(self->display_) < 0 && errno != EAGAIN) {
    g_warning("(Dock) Wayland: Flush failed");
    self->io_watch_ = 0;
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

void WaylandBackend::Flush() {
  if (display_ == nullptr) {
    return;
  }
  wl_display_flush(display_);
}

BackendWindow WaylandBackend::ToBackendWindow(const Toplevel& t) const {
  BackendWindow w;
  w.id = t.id;
  w.app_id = t.app_id;
  w.title = t.title;
  w.icon = t.icon;
  w.pid = t.pid;
  w.minimized = (t.state & KYWC_TOPLEVEL_V1_STATE_MINIMIZED) != 0;
  w.maximized = (t.state & KYWC_TOPLEVEL_V1_STATE_MAXIMIZED) != 0;
  w.active = (t.state & KYWC_TOPLEVEL_V1_STATE_ACTIVATED) != 0;
  w.skip_taskbar =
      (t.capabilities & KYWC_TOPLEVEL_V1_CAPABILITY_SKIP_TASKBAR) != 0;
  w.has_parent = t.parent != nullptr;
  w.allowed_close = true;
  w.geometry.x = static_cast<int>(t.x);
  w.geometry.y = static_cast<int>(t.y);
  w.geometry.width = static_cast<int>(t.width);
  w.geometry.height = static_cast<int>(t.height);
  return w;
}

Toplevel* WaylandBackend::Lookup(uint32_t id) const {
  auto it = tracked_.find(id);
  return it == tracked_.end() ? nullptr : it->second.get();
}

void WaylandBackend::HandleNewToplevel(kywc_toplevel_v1* handle,
                                       const char* uuid) {
  auto toplevel = std::make_unique<Toplevel>();
  toplevel->backend = this;
  toplevel->handle = handle;
  toplevel->id = next_id_++;
  toplevel->uuid = SafeString(uuid);
  kywc_toplevel_v1_add_listener(handle, &kToplevelListener, toplevel.get());
  tracked_[toplevel->id] = std::move(toplevel);
}

void WaylandBackend::HandleToplevelDone(Toplevel* toplevel) {
  if (toplevel->initialized && !toplevel->dirty) {
    return;
  }
  toplevel->initialized = true;
  toplevel->dirty = false;

  uint32_t id = toplevel->id;
  if ((toplevel->state & KYWC_TOPLEVEL_V1_STATE_ACTIVATED) != 0) {
    if (active_id_ != id) {
      active_id_ = id;
      if (observer_ != nullptr) {
        observer_->OnActiveWindowChanged(id);
      }
    }
  } else if (active_id_ == id) {
    active_id_ = 0;
    if (observer_ != nullptr) {
      observer_->OnActiveWindowChanged(0);
    }
  }

  BackendWindow window = ToBackendWindow(*toplevel);

  if (!toplevel->reported) {
    if (window.skip_taskbar || window.app_id.empty()) {
      return;
    }
    toplevel->reported = true;
    if (observer_ != nullptr) {
      observer_->OnWindowAdded(window);
    }
    return;
  }

  if (observer_ != nullptr) {
    observer_->OnWindowChanged(window);
  }
}

void WaylandBackend::HandleToplevelClosed(Toplevel* toplevel) {
  uint32_t id = toplevel->id;
  bool reported = toplevel->reported;

  for (auto& [other_id, other] : tracked_) {
    if (other->parent == toplevel) {
      other->parent = nullptr;
    }
  }
  kywc_toplevel_v1_destroy(toplevel->handle);
  tracked_.erase(id);

  if (active_id_ == id) {
    active_id_ = 0;
  }
  if (reported && observer_ != nullptr) {
    observer_->OnWindowRemoved(id);
  }
}

void WaylandBackend::HandleManagerFinished() {
  g_warning("(Dock) Wayland: Compositor finished the toplevel manager");
  kywc_toplevel_manager_v1_destroy(toplevel_manager_);
  toplevel_manager_ = nullptr;
}

std::vector<BackendWindow> WaylandBackend::ListWindows() {
  std::vector<BackendWindow> result;
  for (const auto& [id, toplevel] : tracked_) {
    if (toplevel->reported) {
      result.push_back(ToBackendWindow(*toplevel));
    }
  }
  return result;
}

uint32_t WaylandBackend::ActiveWindow() { return active_id_; }

BackendRect WaylandBackend::GetWindowGeometry(uint32_t id) {
  BackendRect rect{};
  Toplevel* t = Lookup(id);
  if (t != nullptr) {
    rect.x = static_cast<int>(t->x);
    rect.y = static_cast<int>(t->y);
    rect.width = static_cast<int>(t->width);
    rect.height = static_cast<int>(t->height);
  }
  return rect;
}

uint32_t WaylandBackend::GetWindowGroupLeader(uint32_t id) {
  return id;
}

bool WaylandBackend::Activate(uint32_t id) {
  Toplevel* t = Lookup(id);
  if (t == nullptr) {
    return false;
  }
  if ((t->state & KYWC_TOPLEVEL_V1_STATE_MINIMIZED) != 0) {
    kywc_toplevel_v1_unset_minimized(t->handle);
  }
  kywc_toplevel_v1_activate(t->handle);
  Flush();
  return true;
}

bool WaylandBackend::Close(uint32_t id) {
  Toplevel* t = Lookup(id);
  if (t == nullptr) {
    return false;
  }
  kywc_toplevel_v1_close(t->handle);
  Flush();
  return true;
}

bool WaylandBackend::Minimize(uint32_t id) {
  Toplevel* t = Lookup(id);
  if (t == nullptr) {
    return false;
  }
  kywc_toplevel_v1_set_minimized(t->handle);
  Flush();
  return true;
}

bool WaylandBackend::Maximize(uint32_t id) {
  Toplevel* t = Lookup(id);
  if (t == nullptr) {
    return false;
  }
  if ((t->state & KYWC_TOPLEVEL_V1_STATE_MAXIMIZED) != 0) {
    kywc_toplevel_v1_unset_maximized(t->handle);
  } else {
    kywc_toplevel_v1_set_maximized(t->handle, nullptr);
  }
  Flush();
  return true;
}

bool WaylandBackend::MakeAbove(uint32_t id) { return Activate(id); }

bool WaylandBackend::MoveWindow(uint32_t id) {
  (void)id;
  g_message(
      "(Dock) Wayland: MoveWindow is not supported by the Wayland backend");
  return false;
}

bool WaylandBackend::KillClient(uint32_t id) {
  Toplevel* t = Lookup(id);
  if (t == nullptr) {
    return false;
  }
  if (t->pid != 0) {
    return kill(static_cast<pid_t>(t->pid), SIGKILL) == 0;
  }
  kywc_toplevel_v1_close(t->handle);
  Flush();
  return true;
}

bool WaylandBackend::CaptureWindow(uint32_t id,
                                   const std::string& out_png_path) {
  Toplevel* t = Lookup(id);
  if (t == nullptr) {
    return false;
  }

  if (CaptureViaThumbnail(t, out_png_path)) {
    return true;
  }

  // The capture roundtrips dispatch events, so the window may be gone now.
  t = Lookup(id);
  if (t == nullptr || (t->state & KYWC_TOPLEVEL_V1_STATE_MINIMIZED) != 0 ||
      t->width == 0 || t->height == 0) {
    return false;
  }
  gchar* geometry =
      g_strdup_printf("%d,%d %ux%u", t->x, t->y, t->width, t->height);
  const gchar* argv[] = {"grim", "-g", geometry, out_png_path.c_str(), nullptr};
  GError* error = nullptr;
  gint status = 0;
  gboolean ok = g_spawn_sync(nullptr, const_cast<gchar**>(argv), nullptr,
                             G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr,
                             nullptr, &status, &error);
  g_free(geometry);
  if (!ok) {
    g_warning("(Dock) Wayland: Grim failed: %s",
              error != nullptr ? error->message : "unknown");
    g_clear_error(&error);
    return false;
  }
  return g_spawn_check_wait_status(status, nullptr);
}

namespace {

constexpr uint64_t kDrmModifierLinear = 0;
constexpr uint64_t kDrmModifierInvalid = 0x00ffffffffffffffULL;

constexpr uint32_t FourCC(char a, char b, char c, char d) {
  return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) |
         (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(d) << 24);
}

bool DrmChannelLayout(uint32_t format, int* r, int* g, int* b, int* a,
                      bool* has_alpha) {
  switch (format) {
    case 0x34325241:
      *b = 0;
      *g = 1;
      *r = 2;
      *a = 3;
      *has_alpha = true;
      return true;
    case 0x34325258:
      *b = 0;
      *g = 1;
      *r = 2;
      *a = 3;
      *has_alpha = false;
      return true;
    case 0x34324241:
      *r = 0;
      *g = 1;
      *b = 2;
      *a = 3;
      *has_alpha = true;
      return true;
    case 0x34324258:
      *r = 0;
      *g = 1;
      *b = 2;
      *a = 3;
      *has_alpha = false;
      return true;
    default:
      (void)FourCC;
      return false;
  }
}

// Converts a captured frame into a PNG at |path|.
bool SaveFrame(int fd, uint32_t format, uint32_t width, uint32_t height,
               uint32_t offset, uint32_t stride, uint64_t modifier,
               uint32_t flags, const std::string& path) {
  bool is_dmabuf = (flags & KYWC_CAPTURE_FRAME_V1_FLAGS_DMABUF) != 0;
  bool mappable = !is_dmabuf || modifier == kDrmModifierLinear ||
                  modifier == kDrmModifierInvalid;
  int r = 0;
  int g = 0;
  int b = 0;
  int a = 0;
  bool has_alpha = false;
  if (!mappable || !DrmChannelLayout(format, &r, &g, &b, &a, &has_alpha)) {
    return false;
  }

  size_t map_size =
      static_cast<size_t>(offset) + static_cast<size_t>(stride) * height;
  void* map = mmap(nullptr, map_size, PROT_READ, MAP_SHARED, fd, 0);
  if (map == MAP_FAILED) {
    return false;
  }

  const uint8_t* base = static_cast<const uint8_t*>(map) + offset;
  auto* rgba =
      static_cast<guint8*>(g_malloc(static_cast<gsize>(width) * height * 4));
  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t* row = base + static_cast<size_t>(y) * stride;
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t* px = row + static_cast<size_t>(x) * 4;
      guint8* out = rgba + (static_cast<size_t>(y) * width + x) * 4;
      out[0] = px[r];
      out[1] = px[g];
      out[2] = px[b];
      out[3] = has_alpha ? px[a] : 0xFF;
    }
  }
  munmap(map, map_size);

  GdkPixbuf* pixbuf = gdk_pixbuf_new_from_data(
      rgba, GDK_COLORSPACE_RGB, TRUE, 8, width, height, width * 4,
      [](guchar* pixels, gpointer) { g_free(pixels); }, nullptr);
  GError* error = nullptr;
  bool ok = gdk_pixbuf_save(pixbuf, path.c_str(), "png", &error, nullptr) != 0;
  g_object_unref(pixbuf);
  if (!ok) {
    g_warning("(Dock) Wayland: Save thumbnail failed: %s",
              error != nullptr ? error->message : "unknown");
    g_clear_error(&error);
  }
  return ok;
}

void FrameFailed(void* data, kywc_capture_frame_v1* /*frame*/) {
  static_cast<WaylandBackend::CaptureRequest*>(data)->done = true;
}

void FrameCancelled(void* data, kywc_capture_frame_v1* /*frame*/) {
  static_cast<WaylandBackend::CaptureRequest*>(data)->done = true;
}

void FrameBuffer(void* data, kywc_capture_frame_v1* frame, int32_t fd,
                 uint32_t format, uint32_t width, uint32_t height,
                 uint32_t offset, uint32_t stride, uint32_t modifier_hi,
                 uint32_t modifier_lo, uint32_t flags) {
  auto* req = static_cast<WaylandBackend::CaptureRequest*>(data);
  uint64_t modifier = (static_cast<uint64_t>(modifier_hi) << 32) | modifier_lo;
  req->ok = SaveFrame(fd, format, width, height, offset, stride, modifier,
                      flags, req->path);
  req->done = true;
  close(fd);
  kywc_capture_frame_v1_release_buffer(frame, /*want_buffer=*/0);
}

// Only sent to version 2 clients; we bind version 1.
void FrameBufferWithPlane(void* /*data*/, kywc_capture_frame_v1* /*frame*/,
                          uint32_t /*index*/, int32_t fd, uint32_t /*offset*/,
                          uint32_t /*stride*/) {
  close(fd);
}

void FrameBufferDone(void* /*data*/, kywc_capture_frame_v1* /*frame*/) {}

const kywc_capture_frame_v1_listener kFrameListener = {
    .failed = FrameFailed,
    .cancelled = FrameCancelled,
    .buffer = FrameBuffer,
    .buffer_with_plane = FrameBufferWithPlane,
    .buffer_done = FrameBufferDone,
};

}  // namespace

bool WaylandBackend::CaptureViaThumbnail(Toplevel* toplevel,
                                         const std::string& out_png_path) {
  if (capture_manager_ == nullptr || toplevel->uuid.empty()) {
    return false;
  }
  CaptureRequest req;
  req.path = out_png_path;
  kywc_capture_frame_v1* frame = kywc_capture_manager_v1_capture_toplevel(
      capture_manager_, toplevel->uuid.c_str(), /*without_decoration=*/1);
  kywc_capture_frame_v1_add_listener(frame, &kFrameListener, &req);

  gint64 deadline = g_get_monotonic_time() + G_USEC_PER_SEC;
  while (!req.done && g_get_monotonic_time() < deadline) {
    if (wl_display_roundtrip(display_) < 0) {
      break;
    }
  }
  kywc_capture_frame_v1_destroy(frame);
  Flush();
  return req.ok;
}

}  // namespace dock
}  // namespace gxde
