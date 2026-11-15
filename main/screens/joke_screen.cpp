#include "screens/joke_screen.h"

#include <cctype>
#include <cstdio>

#include "data/jokes.h"
#include "display/ui.h"
#include "esp_log.h"
#include "net/wifi.h"
#include "screens/common.h"

namespace {

constexpr const char* kTag = "joke-ui";

}  // namespace

void JokeScreen::build(lv_obj_t* root) {
  screens::header(root, "JOKE", "", &clock_, &date_);
  // Dynamic: the category changes with every joke.
  subtitle_ = ui::label(root, &font_roboto_medium_28, screens::kGrey, "JOKEAPI", 30, 64);
  screens::rule(root, 100);

  text_ = screens::body_text(root, "--");

  screens::footer(root, "< SCREEN   NEW >   HOLD > SLEEP", &updated_);
}

bool JokeScreen::load() {
  if (!net::wifi_connected()) {
    apply();
    return false;
  }

  data::Joke fresh{};
  if (!jokes::fetch(fresh)) {
    ESP_LOGW(kTag, "fetch failed, keeping previous");
    if (joke_.valid) joke_.stale = true;
    apply();
    return false;
  }

  joke_ = fresh;
  apply();
  return true;
}

void JokeScreen::apply() {
  if (text_ == nullptr) return;
  screens::refresh_chrome(clock_, date_);

  ui::set_text(text_, joke_.valid ? joke_.text : "NO DATA");
  screens::fit_body(text_, 460);

  if (subtitle_ != nullptr) {
    char buf[32] = "JOKEAPI";
    if (joke_.valid && joke_.category[0] != '\0') {
      snprintf(buf, sizeof(buf), "JOKEAPI \xC2\xB7 %s", joke_.category);
      for (char* p = buf; *p != '\0'; ++p) {
        *p = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
      }
    }
    ui::set_text(subtitle_, buf);
  }

  if (updated_ != nullptr) {
    char buf[32];
    if (joke_.valid) {
      snprintf(buf, sizeof(buf), "%s%s", joke_.stale ? "STALE " : "UPDATED ", joke_.updated);
      ui::set_text(updated_, buf);
    } else {
      ui::set_text(updated_, net::wifi_connected() ? "" : "NO WI-FI");
    }
  }
}
