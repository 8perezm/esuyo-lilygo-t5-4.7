#include "data/zenquotes.h"

#include <cstring>
#include <string>

#include "cJSON.h"
#include "data/text.h"
#include "esp_log.h"
#include "net/http_client.h"
#include "net/wifi.h"

namespace {

constexpr const char* kTag = "zenquotes";
constexpr const char* kUrl = "https://zenquotes.io/api/random";

const cJSON* item(const cJSON* obj, const char* key) {
  return cJSON_GetObjectItemCaseSensitive(obj, key);
}

}  // namespace

namespace zenquotes {

bool fetch(data::DailyQuote& out) {
  if (!net::wifi_connected()) return false;

  std::string body;
  if (!net::http_get(kUrl, body, 15000)) {
    ESP_LOGW(kTag, "fetch failed");
    return false;
  }

  cJSON* root = cJSON_Parse(body.c_str());
  if (root == nullptr) {
    ESP_LOGW(kTag, "bad json (%u bytes)", static_cast<unsigned>(body.size()));
    return false;
  }

  // Success is a one-element array; a rate-limited reply is also HTTP 200, so
  // the body has to be checked rather than the status code.
  const cJSON* first = cJSON_IsArray(root) ? cJSON_GetArrayItem(root, 0) : root;
  const cJSON* q = item(first, "q");
  const cJSON* a = item(first, "a");

  data::DailyQuote fresh{};
  if (cJSON_IsString(q)) data::clean_text(q->valuestring, fresh.text, sizeof(fresh.text));
  if (cJSON_IsString(a)) data::clean_text(a->valuestring, fresh.author, sizeof(fresh.author));

  const bool rate_limited = strncmp(fresh.text, "Too many requests", 17) == 0;
  const bool valid = fresh.text[0] != '\0' && !rate_limited;
  cJSON_Delete(root);

  if (!valid) {
    ESP_LOGW(kTag, "no usable quote%s", rate_limited ? " (rate limited)" : "");
    return false;
  }

  fresh.valid = true;
  net::format_clock(fresh.updated, sizeof(fresh.updated));
  ESP_LOGI(kTag, "quote: %s", fresh.text);
  out = fresh;
  return true;
}

}  // namespace zenquotes
