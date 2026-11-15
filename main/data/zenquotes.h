#pragma once

#include "data/model.h"

namespace zenquotes {

// GET https://zenquotes.io/api/random (keyless). Fills `out` only on success so
// the caller keeps the previous quote when the network or the rate limit bites.
// Attribution ("ZENQUOTES.IO") is shown in the screen header per their terms.
bool fetch(data::DailyQuote& out);

}  // namespace zenquotes
