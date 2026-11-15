#include "data/jokes.h"

#include <string>

#include "cJSON.h"
#include "data/text.h"
#include "esp_log.h"
#include "net/http_client.h"
#include "net/wifi.h"

namespace {

constexpr const char* kTag = "jokes";

// type=single keeps the payload to one "joke" string; without it the API
// randomly returns setup/delivery instead, which needs a second layout.
// blacklistFlags is applied by the server: nsfw, racist and explicit never
// reach the panel.
constexpr const char* kUrl =
    "https://v2.jokeapi.dev/joke/Any?type=single"
    "&blacklistFlags=nsfw,racist,explicit";

const cJSON* item(const cJSON* obj, const char* key) {
  return cJSON_GetObjectItemCaseSensitive(obj, key);
}

}  // namespace

namespace jokes {

bool fetch(data::Joke& out) {
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

  // JokeAPI reports its own errors inside an HTTP 200 (`"error": true`), so the
  // body decides success, not the status code.
  const cJSON* err = item(root, "error");
  const bool failed = cJSON_IsTrue(err);
  const cJSON* text = item(root, "joke");
  const cJSON* category = item(root, "category");

  data::Joke fresh{};
  if (!failed && cJSON_IsString(text)) data::clean_text(text->valuestring, fresh.text, sizeof(fresh.text));
  if (cJSON_IsString(category)) data::clean_text(category->valuestring, fresh.category, sizeof(fresh.category));
  cJSON_Delete(root);

  if (failed || fresh.text[0] == '\0') {
    ESP_LOGW(kTag, "no usable joke%s", failed ? " (api error)" : "");
    return false;
  }

  fresh.valid = true;
  net::format_clock(fresh.updated, sizeof(fresh.updated));
  ESP_LOGI(kTag, "%s: %s", fresh.category, fresh.text);
  out = fresh;
  return true;
}

}  // namespace jokes
