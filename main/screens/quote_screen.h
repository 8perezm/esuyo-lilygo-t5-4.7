#pragma once

#include "data/model.h"
#include "screens/screen.h"

// One quote per fetch. The middle button (on_action) does not cycle anything:
// the app refreshes the active screen after it, which fetches the next quote.
class QuoteScreen : public Screen {
 public:
  const char* id() const override { return "quote"; }
  const char* title() const override { return "Quote"; }

  void build(lv_obj_t* root) override;
  bool load() override;

 private:
  void apply();

  data::DailyQuote quote_{};
  lv_obj_t* clock_ = nullptr;
  lv_obj_t* date_ = nullptr;
  lv_obj_t* text_ = nullptr;
  lv_obj_t* author_ = nullptr;
  lv_obj_t* updated_ = nullptr;
};
