#pragma once

#include "data/model.h"
#include "screens/screen.h"

class MarketScreen : public Screen {
 public:
  enum class Kind { Crypto, Indices, Commodities };

  MarketScreen(Kind kind, const char* id, const char* title)
      : kind_(kind), id_(id), title_(title) {}

  const char* id() const override { return id_; }
  const char* title() const override { return title_; }

  void build(lv_obj_t* root) override;
  bool load() override;

 private:
  void apply();
  int symbols(const char* out[], int max);

  Kind kind_;
  const char* id_;
  const char* title_;
  data::QuoteSet set_{};

  lv_obj_t* title_label_ = nullptr;
  lv_obj_t* subtitle_ = nullptr;
  lv_obj_t* updated_ = nullptr;
  lv_obj_t* name_[5] = {};
  lv_obj_t* symbol_[5] = {};
  lv_obj_t* price_[5] = {};
  lv_obj_t* change_[5] = {};
  lv_obj_t* icon_[5] = {};
  lv_obj_t* bars_[5][5] = {};
};
