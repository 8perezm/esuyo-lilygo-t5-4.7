#pragma once

#include "lvgl.h"

// One dashboard. Screens own their widgets and their data; the app owns the
// carousel and the panel refresh, so adding a screen is just a new subclass
// plus a registry entry.
struct Screen {
  virtual ~Screen() = default;

  virtual const char* id() const = 0;
  virtual const char* title() const = 0;

  // Create widgets under `root` (called on every switch).
  virtual void build(lv_obj_t* root) = 0;

  // Fetch or refresh data and update the widgets. Returns false on failure,
  // in which case the previous data (or the stale badge) stays visible.
  virtual bool load() = 0;

  // -1 = previous context, +1 = next context, 0 = select. Ignored by default.
  virtual void on_action(int dir) { (void)dir; }
};
