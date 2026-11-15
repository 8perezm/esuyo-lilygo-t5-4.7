#pragma once

#include "data/model.h"

namespace jokes {

// GET v2.jokeapi.dev, pinned to type=single so the reply is always one block of
// text, with the content the panel must never show filtered out server-side.
// Fills `out` only on success so the caller keeps the previous joke.
bool fetch(data::Joke& out);

}  // namespace jokes
