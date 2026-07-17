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

#include "src/backend/x11_backend/x11_backend.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <xcb/xcb_icccm.h>

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace gxde {
namespace dock {

namespace {

xcb_atom_t InternAtom(xcb_connection_t* conn, const char* name) {
  xcb_intern_atom_cookie_t cookie =
      xcb_intern_atom(conn, 0, strlen(name), name);
  xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(conn, cookie, nullptr);
  if (reply == nullptr) {
    return XCB_ATOM_NONE;
  }
  xcb_atom_t atom = reply->atom;
  free(reply);
  return atom;
}

xcb_atom_t g_wm_change_state = XCB_ATOM_NONE;

std::string ToLower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(g_ascii_tolower(c));
  }
  return s;
}

}  // namespace

X11Backend::~X11Backend() {
  if (io_watch_ != 0) {
    g_source_remove(io_watch_);
  }
  if (io_channel_ != nullptr) {
    g_io_channel_unref(io_channel_);
  }
  if (conn_ != nullptr) {
    xcb_ewmh_connection_wipe(&ewmh_);
    xcb_disconnect(conn_);
  }
}

bool X11Backend::Init(WindowObserver* observer) {
  observer_ = observer;
  conn_ = xcb_connect(nullptr, &screen_number_);
  if (conn_ == nullptr || xcb_connection_has_error(conn_) != 0) {
    return false;
  }

  xcb_intern_atom_cookie_t* cookie = xcb_ewmh_init_atoms(conn_, &ewmh_);
  if (xcb_ewmh_init_atoms_replies(&ewmh_, cookie, nullptr) == 0) {
    return false;
  }
  g_wm_change_state = InternAtom(conn_, "WM_CHANGE_STATE");

  const xcb_setup_t* setup = xcb_get_setup(conn_);
  xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
  for (int i = 0; i < screen_number_; ++i) {
    xcb_screen_next(&it);
  }
  root_ = it.data->root;

  const uint32_t mask =
      XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
  xcb_change_window_attributes(conn_, root_, XCB_CW_EVENT_MASK, &mask);
  xcb_flush(conn_);

  int fd = xcb_get_file_descriptor(conn_);
  io_channel_ = g_io_channel_unix_new(fd);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  auto watch_cond = static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR);
  io_watch_ =
      g_io_add_watch(io_channel_, watch_cond, &X11Backend::OnFdReadable, this);

  SyncClientList();
  SetActiveFromServer();
  return true;
}

gboolean X11Backend::OnFdReadable(GIOChannel* /*source*/,
                                  GIOCondition condition, gpointer data) {
  auto* self = static_cast<X11Backend*>(data);
  if ((condition & (G_IO_HUP | G_IO_ERR)) != 0) {
    g_warning("(Dock) X11: Connection lost");
    return G_SOURCE_REMOVE;
  }
  self->DispatchEvents();
  if (xcb_connection_has_error(self->conn_) != 0) {
    g_warning("(Dock) X11: Connection error");
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

void X11Backend::DispatchEvents() {
  xcb_generic_event_t* event = nullptr;
  while ((event = xcb_poll_for_event(conn_)) != nullptr) {
    uint8_t type = event->response_type & ~0x80;
    if (type == XCB_PROPERTY_NOTIFY) {
      auto* pn = reinterpret_cast<xcb_property_notify_event_t*>(event);
      if (pn->window == root_) {
        if (pn->atom == ewmh_._NET_CLIENT_LIST) {
          SyncClientList();
        } else if (pn->atom == ewmh_._NET_ACTIVE_WINDOW) {
          SetActiveFromServer();
        }
      } else {
        auto it = windows_.find(pn->window);
        if (it != windows_.end() &&
            (pn->atom == ewmh_._NET_WM_NAME || pn->atom == XCB_ATOM_WM_NAME ||
             pn->atom == ewmh_._NET_WM_STATE ||
             pn->atom == XCB_ATOM_WM_CLASS)) {
          BackendWindow w;
          if (ReadWindow(pn->window, &w)) {
            windows_[pn->window] = w;
            if (observer_ != nullptr) {
              observer_->OnWindowChanged(w);
            }
          }
        }
      }
    } else if (type == XCB_DESTROY_NOTIFY) {
      auto* dn = reinterpret_cast<xcb_destroy_notify_event_t*>(event);
      auto it = windows_.find(dn->window);
      if (it != windows_.end()) {
        windows_.erase(it);
        if (observer_ != nullptr) {
          observer_->OnWindowRemoved(dn->window);
        }
      }
    }
    free(event);
  }
}

bool X11Backend::IsTaskbarWindow(xcb_window_t win) {
  xcb_ewmh_get_atoms_reply_t types;
  xcb_get_property_cookie_t cookie = xcb_ewmh_get_wm_window_type(&ewmh_, win);
  if (xcb_ewmh_get_wm_window_type_reply(&ewmh_, cookie, &types, nullptr) == 0) {
    return true;
  }
  bool taskbar = true;
  for (uint32_t i = 0; i < types.atoms_len; ++i) {
    xcb_atom_t a = types.atoms[i];
    if (a == ewmh_._NET_WM_WINDOW_TYPE_DOCK ||
        a == ewmh_._NET_WM_WINDOW_TYPE_DESKTOP ||
        a == ewmh_._NET_WM_WINDOW_TYPE_TOOLBAR ||
        a == ewmh_._NET_WM_WINDOW_TYPE_MENU ||
        a == ewmh_._NET_WM_WINDOW_TYPE_SPLASH ||
        a == ewmh_._NET_WM_WINDOW_TYPE_UTILITY ||
        a == ewmh_._NET_WM_WINDOW_TYPE_NOTIFICATION) {
      taskbar = false;
      break;
    }
  }
  xcb_ewmh_get_atoms_reply_wipe(&types);
  return taskbar;
}

bool X11Backend::ReadWindow(xcb_window_t win, BackendWindow* out) {
  out->id = win;

  xcb_icccm_get_wm_class_reply_t wm_class;
  xcb_get_property_cookie_t class_cookie = xcb_icccm_get_wm_class(conn_, win);
  if (xcb_icccm_get_wm_class_reply(conn_, class_cookie, &wm_class, nullptr) !=
      0) {
    out->wm_instance =
        wm_class.instance_name != nullptr ? wm_class.instance_name : "";
    out->wm_class = wm_class.class_name != nullptr ? wm_class.class_name : "";
    out->app_id = ToLower(out->wm_class);
    xcb_icccm_get_wm_class_reply_wipe(&wm_class);
  }

  xcb_ewmh_get_utf8_strings_reply_t name;
  xcb_get_property_cookie_t name_cookie = xcb_ewmh_get_wm_name(&ewmh_, win);
  if (xcb_ewmh_get_wm_name_reply(&ewmh_, name_cookie, &name, nullptr) != 0) {
    out->title.assign(name.strings, name.strings_len);
    xcb_ewmh_get_utf8_strings_reply_wipe(&name);
  } else {
    xcb_icccm_get_text_property_reply_t text;
    xcb_get_property_cookie_t text_cookie = xcb_icccm_get_wm_name(conn_, win);
    if (xcb_icccm_get_wm_name_reply(conn_, text_cookie, &text, nullptr) != 0) {
      out->title.assign(text.name, text.name_len);
      xcb_icccm_get_text_property_reply_wipe(&text);
    }
  }

  uint32_t pid = 0;
  xcb_get_property_cookie_t pid_cookie = xcb_ewmh_get_wm_pid(&ewmh_, win);
  if (xcb_ewmh_get_wm_pid_reply(&ewmh_, pid_cookie, &pid, nullptr) != 0) {
    out->pid = pid;
  }

  out->minimized = false;
  out->maximized = false;
  out->skip_taskbar = false;
  bool max_vert = false;
  bool max_horz = false;
  xcb_ewmh_get_atoms_reply_t states;
  xcb_get_property_cookie_t state_cookie = xcb_ewmh_get_wm_state(&ewmh_, win);
  if (xcb_ewmh_get_wm_state_reply(&ewmh_, state_cookie, &states, nullptr) !=
      0) {
    for (uint32_t i = 0; i < states.atoms_len; ++i) {
      xcb_atom_t a = states.atoms[i];
      if (a == ewmh_._NET_WM_STATE_HIDDEN) {
        out->minimized = true;
      } else if (a == ewmh_._NET_WM_STATE_MAXIMIZED_VERT) {
        max_vert = true;
      } else if (a == ewmh_._NET_WM_STATE_MAXIMIZED_HORZ) {
        max_horz = true;
      } else if (a == ewmh_._NET_WM_STATE_SKIP_TASKBAR) {
        out->skip_taskbar = true;
      }
    }
    xcb_ewmh_get_atoms_reply_wipe(&states);
  }
  out->maximized = max_vert && max_horz;

  out->allowed_close = true;
  xcb_ewmh_get_atoms_reply_t actions;
  xcb_get_property_cookie_t act_cookie =
      xcb_ewmh_get_wm_allowed_actions(&ewmh_, win);
  if (xcb_ewmh_get_wm_allowed_actions_reply(&ewmh_, act_cookie, &actions,
                                            nullptr) != 0) {
    out->allowed_close = false;
    for (uint32_t i = 0; i < actions.atoms_len; ++i) {
      if (actions.atoms[i] == ewmh_._NET_WM_ACTION_CLOSE) {
        out->allowed_close = true;
        break;
      }
    }
    xcb_ewmh_get_atoms_reply_wipe(&actions);
  }
  return true;
}

void X11Backend::SyncClientList() {
  xcb_ewmh_get_windows_reply_t reply;
  xcb_get_property_cookie_t cookie =
      xcb_ewmh_get_client_list(&ewmh_, screen_number_);
  if (xcb_ewmh_get_client_list_reply(&ewmh_, cookie, &reply, nullptr) == 0) {
    return;
  }

  std::map<uint32_t, bool> seen;
  for (uint32_t i = 0; i < reply.windows_len; ++i) {
    xcb_window_t win = reply.windows[i];
    seen[win] = true;
    if (windows_.count(win) != 0) {
      continue;
    }
    if (!IsTaskbarWindow(win)) {
      continue;
    }
    BackendWindow w;
    if (!ReadWindow(win, &w) || w.skip_taskbar) {
      continue;
    }
    const uint32_t evmask =
        XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_change_window_attributes(conn_, win, XCB_CW_EVENT_MASK, &evmask);
    windows_[win] = w;
    if (observer_ != nullptr) {
      observer_->OnWindowAdded(w);
    }
  }
  xcb_ewmh_get_windows_reply_wipe(&reply);

  for (auto it = windows_.begin(); it != windows_.end();) {
    if (seen.count(it->first) == 0) {
      uint32_t id = it->first;
      it = windows_.erase(it);
      if (observer_ != nullptr) {
        observer_->OnWindowRemoved(id);
      }
    } else {
      ++it;
    }
  }
  xcb_flush(conn_);
}

void X11Backend::SetActiveFromServer() {
  xcb_window_t win = XCB_WINDOW_NONE;
  xcb_get_property_cookie_t cookie =
      xcb_ewmh_get_active_window(&ewmh_, screen_number_);
  if (xcb_ewmh_get_active_window_reply(&ewmh_, cookie, &win, nullptr) == 0) {
    return;
  }
  if (win != active_id_) {
    active_id_ = win;
    if (observer_ != nullptr) {
      observer_->OnActiveWindowChanged(win);
    }
  }
}

std::vector<BackendWindow> X11Backend::ListWindows() {
  std::vector<BackendWindow> result;
  result.reserve(windows_.size());
  for (const auto& [id, w] : windows_) {
    result.push_back(w);
  }
  return result;
}

uint32_t X11Backend::ActiveWindow() { return active_id_; }

void X11Backend::SendClientMessage(xcb_window_t win, xcb_atom_t type,
                                   const uint32_t data[5]) {
  xcb_client_message_event_t ev = {};
  ev.response_type = XCB_CLIENT_MESSAGE;
  ev.format = 32;
  ev.window = win;
  ev.type = type;
  for (int i = 0; i < 5; ++i) {
    ev.data.data32[i] = data[i];
  }
  xcb_send_event(
      conn_, 0, root_,
      XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
      reinterpret_cast<const char*>(&ev));
  xcb_flush(conn_);
}

bool X11Backend::Activate(uint32_t id) {
  if (windows_.count(id) == 0) {
    return false;
  }
  const uint32_t data[5] = {2, XCB_CURRENT_TIME, 0, 0, 0};
  SendClientMessage(id, ewmh_._NET_ACTIVE_WINDOW, data);
  return true;
}

bool X11Backend::Close(uint32_t id) {
  if (windows_.count(id) == 0) {
    return false;
  }
  const uint32_t data[5] = {XCB_CURRENT_TIME, 2, 0, 0, 0};
  SendClientMessage(id, ewmh_._NET_CLOSE_WINDOW, data);
  return true;
}

bool X11Backend::Minimize(uint32_t id) {
  if (windows_.count(id) == 0 || g_wm_change_state == XCB_ATOM_NONE) {
    return false;
  }
  const uint32_t data[5] = {XCB_ICCCM_WM_STATE_ICONIC, 0, 0, 0, 0};
  SendClientMessage(id, g_wm_change_state, data);
  return true;
}

bool X11Backend::Maximize(uint32_t id) {
  auto it = windows_.find(id);
  if (it == windows_.end()) {
    return false;
  }
  xcb_ewmh_wm_state_action_t action =
      it->second.maximized ? XCB_EWMH_WM_STATE_REMOVE : XCB_EWMH_WM_STATE_ADD;
  xcb_ewmh_request_change_wm_state(
      &ewmh_, screen_number_, id, action, ewmh_._NET_WM_STATE_MAXIMIZED_VERT,
      ewmh_._NET_WM_STATE_MAXIMIZED_HORZ, XCB_EWMH_CLIENT_SOURCE_TYPE_NORMAL);
  xcb_flush(conn_);
  return true;
}

bool X11Backend::MakeAbove(uint32_t id) {
  if (windows_.count(id) == 0) {
    return false;
  }
  xcb_ewmh_request_change_wm_state(&ewmh_, screen_number_, id,
                                   XCB_EWMH_WM_STATE_ADD,
                                   ewmh_._NET_WM_STATE_ABOVE, XCB_ATOM_NONE,
                                   XCB_EWMH_CLIENT_SOURCE_TYPE_NORMAL);
  xcb_flush(conn_);
  return true;
}

bool X11Backend::MoveWindow(uint32_t id) {
  if (windows_.count(id) == 0) {
    return false;
  }
  xcb_ewmh_request_wm_moveresize(
      &ewmh_, screen_number_, id, 0, 0, XCB_EWMH_WM_MOVERESIZE_MOVE_KEYBOARD,
      XCB_BUTTON_INDEX_ANY, XCB_EWMH_CLIENT_SOURCE_TYPE_NORMAL);
  xcb_flush(conn_);
  return true;
}

bool X11Backend::KillClient(uint32_t id) {
  if (windows_.count(id) == 0) {
    return false;
  }
  xcb_kill_client(conn_, id);
  xcb_flush(conn_);
  return true;
}

bool X11Backend::CaptureWindow(uint32_t id, const std::string& out_png_path) {
  if (windows_.count(id) == 0) {
    return false;
  }
  xcb_get_geometry_reply_t* geom =
      xcb_get_geometry_reply(conn_, xcb_get_geometry(conn_, id), nullptr);
  if (geom == nullptr) {
    return false;
  }
  uint16_t w = geom->width;
  uint16_t h = geom->height;
  free(geom);
  if (w == 0 || h == 0) {
    return false;
  }

  xcb_get_image_reply_t* img = xcb_get_image_reply(
      conn_,
      xcb_get_image(conn_, XCB_IMAGE_FORMAT_Z_PIXMAP, id, 0, 0, w, h, ~0U),
      nullptr);
  if (img == nullptr) {
    return false;
  }
  uint8_t* data = xcb_get_image_data(img);
  int length = xcb_get_image_data_length(img);
  if (length < w * h * 4) {
    free(img);
    return false;
  }

  auto* rgba = static_cast<guint8*>(g_malloc(static_cast<gsize>(w) * h * 4));
  for (int i = 0; i < w * h; ++i) {
    rgba[i * 4 + 0] = data[i * 4 + 2];
    rgba[i * 4 + 1] = data[i * 4 + 1];
    rgba[i * 4 + 2] = data[i * 4 + 0];
    rgba[i * 4 + 3] = 0xFF;
  }
  free(img);

  GdkPixbuf* pixbuf = gdk_pixbuf_new_from_data(
      rgba, GDK_COLORSPACE_RGB, TRUE, 8, w, h, w * 4,
      [](guchar* pixels, gpointer) { g_free(pixels); }, nullptr);
  GError* error = nullptr;
  gboolean ok =
      gdk_pixbuf_save(pixbuf, out_png_path.c_str(), "png", &error, nullptr);
  g_object_unref(pixbuf);
  if (!ok) {
    g_warning("(Dock) X11: Save capture failed: %s",
              error != nullptr ? error->message : "unknown");
    g_clear_error(&error);
    return false;
  }
  return true;
}

}  // namespace dock
}  // namespace gxde
