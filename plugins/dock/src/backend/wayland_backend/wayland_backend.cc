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
#include <wayland-client.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace gxde {
namespace dock {

namespace {

WaylandBackend* BackendOf(kywc_toplevel* toplevel) {
  kywc_context* ctx = kywc_toplevel_get_context(toplevel);
  return static_cast<WaylandBackend*>(kywc_context_get_user_data(ctx));
}

void HandleNewToplevel(kywc_context* /*ctx*/, kywc_toplevel* toplevel,
                       void* data) {
  static_cast<WaylandBackend*>(data)->HandleNewToplevel(toplevel);
}

void HandleToplevelState(kywc_toplevel* toplevel, uint32_t mask) {
  BackendOf(toplevel)->HandleToplevelState(toplevel, mask);
}

void HandleToplevelDestroy(kywc_toplevel* toplevel) {
  BackendOf(toplevel)->HandleToplevelDestroy(toplevel);
}

const struct kywc_toplevel_interface kToplevelImpl = {
    .state = HandleToplevelState,
    .destroy = HandleToplevelDestroy,
};

const struct kywc_context_interface kContextImpl = {
    .create = nullptr,
    .destroy = nullptr,
    .new_output = nullptr,
    .new_toplevel = HandleNewToplevel,
    .new_workspace = nullptr,
};

uint32_t IdOf(kywc_toplevel* toplevel) {
  return static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(kywc_toplevel_get_user_data(toplevel)));
}

}  // namespace

WaylandBackend::~WaylandBackend() {
  if (io_watch_ != 0) {
    g_source_remove(io_watch_);
  }
  if (io_channel_ != nullptr) {
    g_io_channel_unref(io_channel_);
  }
  if (context_ != nullptr) {
    kywc_context_destroy(context_);
  }
}

bool WaylandBackend::Init(WindowObserver* observer) {
  observer_ = observer;
  context_ = kywc_context_create(
      nullptr,
      KYWC_CONTEXT_CAPABILITY_TOPLEVEL | KYWC_CONTEXT_CAPABILITY_THUMBNAIL,
      &kContextImpl, this);
  if (context_ == nullptr) {
    return false;
  }

  int fd = kywc_context_get_fd(context_);
  if (fd < 0) {
    return false;
  }
  io_channel_ = g_io_channel_unix_new(fd);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  auto watch_cond = static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR);
  io_watch_ = g_io_add_watch(io_channel_, watch_cond,
                             &WaylandBackend::OnFdReadable, this);
  return true;
}

gboolean WaylandBackend::OnFdReadable(GIOChannel* /*source*/,
                                      GIOCondition condition, gpointer data) {
  auto* self = static_cast<WaylandBackend*>(data);
  if ((condition & (G_IO_HUP | G_IO_ERR)) != 0) {
    g_warning("(Dock) Wayland: Connection lost");
    return G_SOURCE_REMOVE;
  }
  if (kywc_context_process(self->context_) != 0) {
    g_warning("(Dock) Wayland: Dispatch failed");
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

void WaylandBackend::Flush() {
  if (context_ == nullptr) {
    return;
  }
  wl_display_flush(kywc_context_get_display(context_));
}

BackendWindow WaylandBackend::ToBackendWindow(const Tracked& tracked) const {
  kywc_toplevel* t = tracked.toplevel;
  BackendWindow w;
  w.id = tracked.id;
  w.app_id = t->app_id != nullptr ? t->app_id : "";
  w.title = t->title != nullptr ? t->title : "";
  w.icon = t->icon != nullptr ? t->icon : "";
  w.pid = t->pid;
  w.minimized = t->minimized;
  w.maximized = t->maximized;
  w.active = t->activated;
  w.skip_taskbar =
      (t->capabilities & KYWC_TOPLEVEL_CAPABILITY_SKIP_TASKBAR) != 0;
  w.has_parent = t->parent != nullptr;
  w.allowed_close = true;
  return w;
}

kywc_toplevel* WaylandBackend::Lookup(uint32_t id) const {
  auto it = tracked_.find(id);
  return it == tracked_.end() ? nullptr : it->second.toplevel;
}

void WaylandBackend::HandleNewToplevel(kywc_toplevel* toplevel) {
  uint32_t id = next_id_++;
  Tracked tracked;
  tracked.id = id;
  tracked.toplevel = toplevel;
  tracked.reported = false;
  tracked_[id] = tracked;

  kywc_toplevel_set_user_data(
      toplevel, reinterpret_cast<void*>(static_cast<uintptr_t>(id)));
  kywc_toplevel_set_interface(toplevel, &kToplevelImpl);

  HandleToplevelState(toplevel, KYWC_TOPLEVEL_STATE_APP_ID);
}

void WaylandBackend::HandleToplevelState(kywc_toplevel* toplevel,
                                         uint32_t /*mask*/) {
  uint32_t id = IdOf(toplevel);
  auto it = tracked_.find(id);
  if (it == tracked_.end()) {
    return;
  }
  Tracked& tracked = it->second;

  if (toplevel->activated) {
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

  bool skip =
      (toplevel->capabilities & KYWC_TOPLEVEL_CAPABILITY_SKIP_TASKBAR) != 0;
  BackendWindow window = ToBackendWindow(tracked);

  if (!tracked.reported) {
    if (skip || window.app_id.empty()) {
      return;
    }
    tracked.reported = true;
    if (observer_ != nullptr) {
      observer_->OnWindowAdded(window);
    }
    return;
  }

  if (observer_ != nullptr) {
    observer_->OnWindowChanged(window);
  }
}

void WaylandBackend::HandleToplevelDestroy(kywc_toplevel* toplevel) {
  uint32_t id = IdOf(toplevel);
  auto it = tracked_.find(id);
  if (it == tracked_.end()) {
    return;
  }
  bool reported = it->second.reported;
  tracked_.erase(it);
  if (active_id_ == id) {
    active_id_ = 0;
  }
  if (reported && observer_ != nullptr) {
    observer_->OnWindowRemoved(id);
  }
}

std::vector<BackendWindow> WaylandBackend::ListWindows() {
  std::vector<BackendWindow> result;
  for (const auto& [id, tracked] : tracked_) {
    if (tracked.reported) {
      result.push_back(ToBackendWindow(tracked));
    }
  }
  return result;
}

uint32_t WaylandBackend::ActiveWindow() { return active_id_; }

bool WaylandBackend::Activate(uint32_t id) {
  kywc_toplevel* t = Lookup(id);
  if (t == nullptr) {
    return false;
  }
  if (t->minimized) {
    kywc_toplevel_unset_minimized(t);
  }
  kywc_toplevel_activate(t);
  Flush();
  return true;
}

bool WaylandBackend::Close(uint32_t id) {
  kywc_toplevel* t = Lookup(id);
  if (t == nullptr) {
    return false;
  }
  kywc_toplevel_close(t);
  Flush();
  return true;
}

bool WaylandBackend::Minimize(uint32_t id) {
  kywc_toplevel* t = Lookup(id);
  if (t == nullptr) {
    return false;
  }
  kywc_toplevel_set_minimized(t);
  Flush();
  return true;
}

bool WaylandBackend::Maximize(uint32_t id) {
  kywc_toplevel* t = Lookup(id);
  if (t == nullptr) {
    return false;
  }
  if (t->maximized) {
    kywc_toplevel_unset_maximized(t);
  } else {
    kywc_toplevel_set_maximized(t, nullptr);
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
  kywc_toplevel* t = Lookup(id);
  if (t == nullptr) {
    return false;
  }
  if (t->pid != 0) {
    return kill(static_cast<pid_t>(t->pid), SIGKILL) == 0;
  }
  kywc_toplevel_close(t);
  Flush();
  return true;
}

bool WaylandBackend::CaptureWindow(uint32_t id,
                                   const std::string& out_png_path) {
  kywc_toplevel* t = Lookup(id);
  if (t == nullptr) {
    return false;
  }

  if (CaptureViaThumbnail(t, out_png_path)) {
    return true;
  }

  if (t->minimized || t->width == 0 || t->height == 0) {
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

}  // namespace

bool WaylandBackend::OnThumbnailBuffer(
    kywc_thumbnail* /*thumbnail*/, const struct kywc_thumbnail_buffer* buffer,
    void* data) {
  auto* req = static_cast<CaptureRequest*>(data);
  req->done = true;

  bool is_dmabuf = (buffer->flags & KYWC_THUMBNAIL_BUFFER_IS_DMABUF) != 0;
  bool mappable = !is_dmabuf || buffer->modifier == kDrmModifierLinear ||
                  buffer->modifier == kDrmModifierInvalid;
  int r = 0;
  int g = 0;
  int b = 0;
  int a = 0;
  bool has_alpha = false;
  if (!mappable ||
      !DrmChannelLayout(buffer->format, &r, &g, &b, &a, &has_alpha)) {
    return false;
  }

  size_t map_size = static_cast<size_t>(buffer->offset) +
                    static_cast<size_t>(buffer->stride) * buffer->height;
  void* map = mmap(nullptr, map_size, PROT_READ, MAP_SHARED, buffer->fd, 0);
  if (map == MAP_FAILED) {
    return false;
  }

  const uint8_t* base = static_cast<const uint8_t*>(map) + buffer->offset;
  const uint32_t width = buffer->width;
  const uint32_t height = buffer->height;
  auto* rgba =
      static_cast<guint8*>(g_malloc(static_cast<gsize>(width) * height * 4));
  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t* row = base + static_cast<size_t>(y) * buffer->stride;
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
  req->ok =
      gdk_pixbuf_save(pixbuf, req->path.c_str(), "png", &error, nullptr) != 0;
  g_object_unref(pixbuf);
  if (!req->ok) {
    g_warning("(Dock) Wayland: Save thumbnail failed: %s",
              error != nullptr ? error->message : "unknown");
    g_clear_error(&error);
  }
  return false;
}

void WaylandBackend::OnThumbnailDestroy(kywc_thumbnail* /*thumbnail*/,
                                        void* data) {
  static_cast<CaptureRequest*>(data)->destroyed = true;
}

bool WaylandBackend::CaptureViaThumbnail(kywc_toplevel* toplevel,
                                         const std::string& out_png_path) {
  if (toplevel->uuid == nullptr) {
    return false;
  }
  CaptureRequest req;
  req.path = out_png_path;
  const struct kywc_thumbnail_interface impl = {
      &WaylandBackend::OnThumbnailBuffer, &WaylandBackend::OnThumbnailDestroy};
  kywc_thumbnail* thumbnail = kywc_thumbnail_create_from_toplevel(
      context_, toplevel->uuid, /*without_decoration=*/true, &impl, &req);
  if (thumbnail == nullptr) {
    return false;
  }

  struct wl_display* display = kywc_context_get_display(context_);
  gint64 deadline = g_get_monotonic_time() + G_USEC_PER_SEC;
  while (!req.done && g_get_monotonic_time() < deadline) {
    if (wl_display_roundtrip(display) < 0) {
      break;
    }
  }
  if (!req.destroyed) {
    kywc_thumbnail_destroy(thumbnail);
  }
  return req.ok;
}

}  // namespace dock
}  // namespace gxde
