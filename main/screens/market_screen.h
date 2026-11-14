#pragma once

#include "data/model.h"
#include "screens/screen.h"

// One Markets screen with three sub-screens. The middle button cycles
// Crypto -> Indices -> Commodities, exactly like the weather screen cycles
// cities. The selected sub-screen is persisted by the app.
class MarketScreen : public Screen {
 public:
  enum class Kind { Crypto, Indices, Commodities };
  static constexpr int kKindCount = 3;

  MarketScreen(Kind kind, const char* id, const char* title)
      : kind_(kind), id_(id), title_(title) {}

  const char* id() const override { return id_; }
  const char* title() const override { return title_; }

  static const char* kind_label(Kind kind);
  static const char* kind_id(Kind kind);

  Kind kind() const { return kind_; }
  void set_kind(Kind kind);
  void on_action(int dir) override;

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
