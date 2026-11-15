#include "screens/quote_screen.h"

#include <cstdio>

#include "data/zenquotes.h"
#include "display/ui.h"
#include "esp_log.h"
#include "net/wifi.h"
#include "screens/common.h"

namespace {

constexpr const char* kTag = "quote-ui";

}  // namespace

void QuoteScreen::build(lv_obj_t* root) {
  // ZenQuotes asks for attribution; the header line is the only place the
  // panel can carry a link, so the source name stays visible.
  screens::header(root, "QUOTE", "ZENQUOTES.IO", &clock_, &date_);
  screens::rule(root, 100);

  text_ = screens::body_text(root, "--");
  author_ = ui::label(root, &font_roboto_medium_28, screens::kGrey, "", 48, 444);
  lv_obj_set_width(author_, 864);
  lv_obj_set_style_text_align(author_, LV_TEXT_ALIGN_RIGHT, 0);

  screens::footer(root, "< SCREEN   NEW >   HOLD > SLEEP", &updated_);
}

bool QuoteScreen::load() {
  if (!net::wifi_connected()) {
    apply();
    return false;
  }

  data::DailyQuote fresh{};
  if (!zenquotes::fetch(fresh)) {
    ESP_LOGW(kTag, "fetch failed, keeping previous");
    if (quote_.valid) quote_.stale = true;
    apply();
    return false;
  }

  quote_ = fresh;
  apply();
  return true;
}

void QuoteScreen::apply() {
  if (text_ == nullptr) return;
  screens::refresh_chrome(clock_, date_);

  ui::set_text(text_, quote_.valid ? quote_.text : "NO DATA");
  // Leave room for the author line below: 28 px of type plus the gap.
  const int bottom = screens::fit_body(text_, 392);

  if (author_ != nullptr) {
    if (quote_.valid && quote_.author[0] != '\0') {
      char line[64];
      snprintf(line, sizeof(line), "\xE2\x80\x94 %s", quote_.author);
      ui::set_text(author_, line);
    } else {
      ui::set_text(author_, "");
    }
    // Under the last line, but never over the footer.
    int y = bottom + 20;
    if (y > 424) y = 424;
    ui::place(author_, 48, y);
  }

  if (updated_ != nullptr) {
    char buf[32];
    if (quote_.valid) {
      snprintf(buf, sizeof(buf), "%s%s", quote_.stale ? "STALE " : "UPDATED ",
               quote_.updated);
      ui::set_text(updated_, buf);
    } else {
      ui::set_text(updated_, net::wifi_connected() ? "" : "NO WI-FI");
    }
  }
}
