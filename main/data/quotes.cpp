#include "data/quotes.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include <cctype>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/http_client.h"
#include "net/wifi.h"

namespace {

constexpr const char* kTag = "quotes";

struct Alias {
  const char* id;
  const char* symbol;
};

const Alias kAliases[] = {
    {"bitcoin", "BTC-USD"},   {"ethereum", "ETH-USD"}, {"solana", "SOL-USD"},
    {"ripple", "XRP-USD"},    {"cardano", "ADA-USD"},  {"dogecoin", "DOGE-USD"},
    {"binancecoin", "BNB-USD"}, {"tether", "USDT-USD"}, {"litecoin", "LTC-USD"},
    {"polkadot", "DOT-USD"},  {"chainlink", "LINK-USD"}, {"avalanche-2", "AVAX-USD"},
};

const cJSON* item(const cJSON* obj, const char* key) {
  return cJSON_GetObjectItemCaseSensitive(obj, key);
}

double number(const cJSON* obj, const char* key, double fallback = 0) {
  const cJSON* v = item(obj, key);
  return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

}  // namespace

namespace quotes {

void yahoo_symbol(const char* id, char* out, size_t cap) {
  if (id == nullptr || *id == '\0') {
    strlcpy(out, "", cap);
    return;
  }
  for (const auto& a : kAliases) {
    if (strcasecmp(a.id, id) == 0) {
      strlcpy(out, a.symbol, cap);
      return;
    }
  }
  // Already a ticker if it has a market marker; otherwise treat a bare word as
  // a crypto id and use the USD pair.
  if (strpbrk(id, "^=.-") != nullptr) {
    strlcpy(out, id, cap);
    return;
  }
  snprintf(out, cap, "%s-USD", id);
  for (char* p = out; *p; ++p) *p = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
}

bool fetch(const char* const* symbols, int count, data::QuoteSet& out) {
  data::QuoteSet set{};
  strlcpy(set.currency, "USD", sizeof(set.currency));
  net::format_clock(set.updated, sizeof(set.updated));

  for (int i = 0; i < count && set.count < 8; i++) {
    char ticker[32];
    yahoo_symbol(symbols[i], ticker, sizeof(ticker));
    if (ticker[0] == '\0') continue;

    char url[384];
    snprintf(url, sizeof(url),
             "https://query1.finance.yahoo.com/v8/finance/chart/%s?range=5d&interval=1d",
             net::url_encode(ticker).c_str());

    std::string body;
    if (!net::http_get(url, body, 12000)) {
      ESP_LOGW(kTag, "%s failed", ticker);
      continue;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    const cJSON* result = cJSON_GetArrayItem(item(item(root, "chart"), "result"), 0);
    if (result == nullptr) {
      cJSON_Delete(root);
      continue;
    }
    const cJSON* meta = item(result, "meta");

    data::Quote q{};
    strlcpy(q.symbol, ticker, sizeof(q.symbol));

    const cJSON* name = item(meta, "shortName");
    if (!cJSON_IsString(name)) name = item(meta, "longName");
    strlcpy(q.name, cJSON_IsString(name) ? name->valuestring : ticker, sizeof(q.name));

    q.price = static_cast<float>(number(meta, "regularMarketPrice"));
    q.change_pct = static_cast<float>(number(meta, "regularMarketChangePercent", NAN));

    const cJSON* closes = item(item(item(result, "indicators"), "quote"), "close");
    for (int k = 0; k < cJSON_GetArraySize(closes) && q.spark_count < 5; k++) {
      const cJSON* c = cJSON_GetArrayItem(closes, k);
      if (!cJSON_IsNumber(c)) continue;
      q.spark[q.spark_count++] = static_cast<float>(c->valuedouble);
    }

    if (std::isnan(q.change_pct) && q.spark_count >= 2) {
      float prev = q.spark[q.spark_count - 2];
      float last = q.spark[q.spark_count - 1];
      if (prev != 0) q.change_pct = (last - prev) / prev * 100.0f;
    }
    if (q.price == 0 && q.spark_count > 0) q.price = q.spark[q.spark_count - 1];

    int hint = static_cast<int>(number(meta, "priceHint", 2));
    q.decimals = hint < 0 ? 0 : (hint > 6 ? 6 : hint);
    if (q.price >= 1000) q.decimals = 0;
    q.valid = q.price > 0;

    if (cJSON_IsString(item(meta, "currency"))) {
      strlcpy(set.currency, item(meta, "currency")->valuestring, sizeof(set.currency));
    }

    cJSON_Delete(root);
    if (q.valid) set.items[set.count++] = q;

    vTaskDelay(pdMS_TO_TICKS(120));
  }

  set.valid = set.count > 0;
  set.stale = !set.valid && out.valid;
  if (set.valid) {
    out = set;
  } else {
    out.stale = true;
  }
  ESP_LOGI(kTag, "%d/%d quotes, valid=%d", set.count, count, set.valid);
  return set.valid;
}

}  // namespace quotes
