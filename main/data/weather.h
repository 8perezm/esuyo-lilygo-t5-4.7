#pragma once

#include "app/settings.h"
#include "data/model.h"

namespace weather {

// Open-Meteo, keyless. Fills `out` and marks it stale when the fetch fails.
bool fetch(const CityRef& city, bool fahrenheit, data::Weather& out);

const char* icon_for_code(int code);

}  // namespace weather
