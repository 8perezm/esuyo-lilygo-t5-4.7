#pragma once

#include "screens/screen.h"

class StatusScreen : public Screen {
 public:
  const char* id() const override { return "status"; }
  const char* title() const override { return "Status"; }

  void build(lv_obj_t* root) override;
  bool load() override;

 private:
  lv_obj_t* value_[9] = {};
  lv_obj_t* updated_ = nullptr;
};
