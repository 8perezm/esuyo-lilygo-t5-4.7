#include "screens/market_screen.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "app/settings.h"
#include "data/quotes.h"
#include "display/ui.h"
#include "esp_log.h"
#include "screens/common.h"

namespace {

constexpr const char* kTag = "market-ui";
constexpr int kRowY = 140;
constexpr int kRowH = 70;

int spark_min(const data::Quote& q) {
  float v = q.spark[0];
  for (int i = 1; i < q.spark_count; i++) v = fminf(v, q.spark[i]);
  return static_cast<int>(v);
}

int spark_max(const data::Quote& q) {
  float v = q.spark[0];
  for (int i = 1; i < q.spark_count; i++) v = fmaxf(v, q.spark[i]);
  return static_cast<int>(v);
}

}  // namespace

void MarketScreen::build(lv_obj_t* root) {
  lv_obj_t* clock = nullptr;
  lv_obj_t* date = nullptr;
  title_label_ = screens::header(root, title_, "5-DAY MARKET SNAPSHOT", &clock, &date);
  subtitle_ = ui::label(root, &font_roboto_16, screens::kGrey, "", 30, 70);
  screens::rule(root, 100);

  ui::label(root, &font_roboto_14, screens::kFaint, "ASSET", 64, 114);
  screens::right_text(root, &font_roboto_14, screens::kFaint, "LAST", 380, 230, 114);
  screens::right_text(root, &font_roboto_14, screens::kFaint, "CHANGE", 630, 120, 114);
  ui::label(root, &font_roboto_14, screens::kFaint, "5 DAYS", 790, 114);

  for (int i = 0; i < 5; i++) {
    int y = kRowY + i * kRowH;
    if (i > 0) ui::panel(root, 28, y - 12, 904, 1, screens::kHairline);

    icon_[i] = ui::image(root, icon_get("trending-up", 24), 28, y + 22);
    name_[i] = ui::label(root, &font_roboto_medium_20, screens::kInk, "--", 64, y + 6);
    symbol_[i] = ui::label(root, &font_roboto_14, screens::kGrey, "", 64, y + 34);
    price_[i] = ui::label(root, &font_roboto_bold_48, screens::kInk, "--", 380, y + 2);
    lv_obj_set_width(price_[i], 230);
    lv_obj_set_style_text_align(price_[i], LV_TEXT_ALIGN_RIGHT, 0);

    change_[i] = ui::label(root, &font_roboto_medium_20, screens::kInk, "--", 630, y + 20);
    lv_obj_set_width(change_[i], 120);
    lv_obj_set_style_text_align(change_[i], LV_TEXT_ALIGN_RIGHT, 0);

    for (int b = 0; b < 5; b++) {
      bars_[i][b] = ui::panel(root, 790 + b * 26, y + 40, 14, 4, 0xC9C4BB);
    }
  }

  screens::footer(root, "< SCREEN   REFRESH ON > ", &updated_);
}

int MarketScreen::symbols(const char* out[], int max) {
  Settings& s = settings::get();
  int count = 0;
  if (kind_ == Kind::Crypto) {
    for (int i = 0; i < s.coin_count && count < max; i++) out[count++] = s.coins[i];
  } else if (kind_ == Kind::Indices) {
    for (int i = 0; i < s.index_count && count < max; i++) out[count++] = s.indices[i];
  } else {
    for (int i = 0; i < s.commodity_count && count < max; i++) out[count++] = s.commodities[i];
  }
  return count;
}

bool MarketScreen::load() {
  const char* list[8] = {};
  int n = symbols(list, 8);
  if (n == 0) {
    apply();
    return false;
  }

  data::QuoteSet fresh = set_;
  if (!quotes::fetch(list, n, fresh)) {
    ESP_LOGW(kTag, "fetch failed for %s", id_);
    apply();
    return false;
  }
  set_ = fresh;
  apply();
  return true;
}

void MarketScreen::apply() {
  if (price_[0] == nullptr) return;

  const char* configured[8] = {};
  const int configured_count = symbols(configured, 8);

  char buf[48];
  for (int i = 0; i < 5; i++) {
    bool have = set_.valid && i < set_.count;
    if (!have) {
      if (i < configured_count) {
        ui::set_text(name_[i], configured[i]);
        ui::set_text(symbol_[i], "NO DATA");
      } else {
        ui::set_text(name_[i], "--");
        ui::set_text(symbol_[i], "");
      }
      ui::set_text(price_[i], "--");
      ui::set_text(change_[i], "");
      for (int b = 0; b < 5; b++) ui::resize(bars_[i][b], 14, 0);
      continue;
    }

    const data::Quote& q = set_.items[i];
    ui::set_text(name_[i], q.name);
    ui::set_text(symbol_[i], q.symbol);

    screens::format_price(buf, sizeof(buf), q.price, q.decimals);
    ui::set_text(price_[i], buf);

    screens::format_change(buf, sizeof(buf), q.change_pct);
    ui::set_text(change_[i], buf);

    const lv_image_dsc_t* ic = icon_get(q.change_pct < 0 ? "trending-down" : "trending-up", 24);
    if (icon_[i] != nullptr && ic != nullptr) lv_image_set_src(icon_[i], ic);

    int lo = spark_min(q);
    int hi = spark_max(q);
    int span = hi - lo;
    for (int b = 0; b < 5; b++) {
      if (b >= q.spark_count) {
        ui::resize(bars_[i][b], 14, 0);
        continue;
      }
      int h = span > 0 ? 4 + static_cast<int>((q.spark[b] - lo) * 32.0f / span) : 18;
      ui::resize(bars_[i][b], 14, h);
      ui::place(bars_[i][b], 790 + b * 26, kRowY + i * kRowH + 44 - h);
    }
  }

  snprintf(buf, sizeof(buf), "5-DAY %s", set_.currency);
  ui::set_text(subtitle_, buf);

  if (updated_ != nullptr) {
    snprintf(buf, sizeof(buf), "%s%s", set_.stale ? "STALE " : "UPDATED ", set_.updated);
    ui::set_text(updated_, set_.valid ? buf : "");
  }
}
