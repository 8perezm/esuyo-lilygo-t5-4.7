#pragma once

#include "data/model.h"
#include "screens/screen.h"

class WeatherScreen : public Screen {
 public:
  const char* id() const override { return "weather"; }
  const char* title() const override { return "Weather"; }

  void build(lv_obj_t* root) override;
  bool load() override;
  void on_action(int dir) override;

 private:
  void apply();
  void cycle_city(int dir);

  data::Weather w_{};
  lv_obj_t* title_ = nullptr;
  lv_obj_t* icon_ = nullptr;
  lv_obj_t* temp_ = nullptr;
  lv_obj_t* unit_ = nullptr;
  lv_obj_t* cond_ = nullptr;
  lv_obj_t* feels_ = nullptr;
  lv_obj_t* stats_[3] = {};
  lv_obj_t* updated_ = nullptr;

  lv_obj_t* day_label_[4] = {};
  lv_obj_t* day_icon_[4] = {};
  lv_obj_t* day_max_[4] = {};
  lv_obj_t* day_min_[4] = {};
  lv_obj_t* day_rain_[4] = {};
};
