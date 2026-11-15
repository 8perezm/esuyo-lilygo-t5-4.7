#pragma once

#include <cstddef>

namespace data {

// Copy text into a fixed buffer, keeping only what the panel fonts can draw:
// printable ASCII plus £ ° · € – — … ↑ ↓. Accented Latin is folded to ASCII
// (Zürich -> Zurich), curly quotes and dashes are mapped to characters the
// fonts carry, and everything else (emoji, CJK) is dropped, so a stray glyph
// can never reach the panel. Runs of whitespace collapse to one space and the
// result is truncated on a byte boundary.
void clean_text(const char* in, char* out, size_t cap);

}  // namespace data
