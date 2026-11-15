#pragma once

#include "data/model.h"
#include "screens/screen.h"

// One joke per fetch, always a single block of text (the API is pinned to
// type=single). The middle button triggers a refresh, which fetches the next
// joke; the subtitle shows the category it came from.
class JokeScreen : public Screen {
 public:
  const char* id() const override { return "joke"; }
  const char* title() const override { return "Joke"; }

  void build(lv_obj_t* root) override;
  bool load() override;

 private:
  void apply();

  data::Joke joke_{};
  lv_obj_t* subtitle_ = nullptr;
  lv_obj_t* text_ = nullptr;
  lv_obj_t* updated_ = nullptr;
};
