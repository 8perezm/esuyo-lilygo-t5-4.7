#include "data/text.h"

#include <cstdint>

namespace {

// Must match GLYPH_RANGES in scripts/gen_assets.mjs. Anything outside it would
// draw as an empty box on e-paper, so it is folded to ASCII or dropped instead.
bool drawable(uint32_t cp) {
  if (cp >= 0x20 && cp <= 0x7E) return true;
  switch (cp) {
    case 0xA3:    // £
    case 0xB0:    // °
    case 0xB7:    // ·
    case 0x20AC:  // €
    case 0x2013:  // –
    case 0x2014:  // —
    case 0x2026:  // …
    case 0x2191:  // ↑
    case 0x2193:  // ↓
      return true;
    default:
      return false;
  }
}

// The fonts only carry unaccented Latin, so "Zürich" or "Málaga" from the
// geocoder would draw boxes for the vowels. Fold the Latin-1 and Latin
// Extended-A letters to their base character; the panel is a glanceable
// display, not a typographic one.
uint32_t fold_latin(uint32_t cp) {
  switch (cp) {
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5:
    case 0xC6: case 0x100: case 0x102: case 0x104:
      return 'A';
    case 0xC7: case 0x106: case 0x10C:
      return 'C';
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0x112: case 0x116:
      return 'E';
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: case 0x12A:
      return 'I';
    case 0xD0: case 0x10E:
      return 'D';
    case 0xD1: case 0x143:
      return 'N';
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD8:
    case 0x14C:
      return 'O';
    case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0x16E:
      return 'U';
    case 0xDD: case 0x178:
      return 'Y';
    case 0xDE:
      return 'P';
    case 0xDF:
      return 's';
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5:
    case 0xE6: case 0x101: case 0x103: case 0x105:
      return 'a';
    case 0xE7: case 0x107: case 0x10D:
      return 'c';
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0x113: case 0x117:
      return 'e';
    case 0xEC: case 0xED: case 0xEE: case 0xEF: case 0x12B:
      return 'i';
    case 0xF0: case 0x10F:
      return 'd';
    case 0xF1: case 0x144:
      return 'n';
    case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF8:
    case 0x14D:
      return 'o';
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0x16F:
      return 'u';
    case 0xFD: case 0xFF:
      return 'y';
    case 0xFE:
      return 'p';
    case 0x152:
      return 'O';
    case 0x153:
      return 'o';
    case 0x160: case 0x15E:
      return 'S';
    case 0x161: case 0x15F:
      return 's';
    case 0x17D: case 0x179:
      return 'Z';
    case 0x17E: case 0x17A:
      return 'z';
    default:
      return cp;
  }
}

// Punctuation the APIs emit but the fonts lack. Mapping beats dropping: an
// apostrophe disappearing turns "don't" into "dont".
uint32_t normalise(uint32_t cp) {
  switch (cp) {
    case 0x2018: case 0x2019: case 0x201A: case 0x201B: case 0x2032:
      return '\'';
    case 0x201C: case 0x201D: case 0x201E: case 0x201F: case 0x2033:
      return '"';
    case 0x2022:  // bullet
      return '*';
    case 0x2010: case 0x2011: case 0x2012: case 0x2015: case 0x2212:
      return '-';
    case 0x00A0: case 0x2007: case 0x2009: case 0x200A: case 0x202F: case 0x205F:
      return ' ';
    case 0x00AD:  // soft hyphen
      return 0;
    default:
      return fold_latin(cp);
  }
}

}  // namespace

namespace data {

void clean_text(const char* in, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  if (in == nullptr) return;

  size_t wrote = 0;
  bool pending_space = false;

  auto put = [&](const char* bytes, size_t n) {
    if (wrote + n + 1 > cap) return false;
    for (size_t i = 0; i < n; i++) out[wrote++] = bytes[i];
    out[wrote] = '\0';
    return true;
  };

  const unsigned char* p = reinterpret_cast<const unsigned char*>(in);
  while (*p != '\0') {
    uint32_t cp = 0;
    int len = 1;
    if (*p < 0x80) {
      cp = *p;
    } else if ((*p & 0xE0) == 0xC0) {
      cp = *p & 0x1F;
      len = 2;
    } else if ((*p & 0xF0) == 0xE0) {
      cp = *p & 0x0F;
      len = 3;
    } else if ((*p & 0xF8) == 0xF0) {
      cp = *p & 0x07;
      len = 4;
    } else {
      p++;  // stray continuation byte
      continue;
    }

    bool ok = true;
    for (int i = 1; i < len; i++) {
      if ((p[i] & 0xC0) != 0x80) {
        ok = false;
        break;
      }
      cp = (cp << 6) | (p[i] & 0x3F);
    }
    if (!ok) {
      p++;
      continue;
    }
    p += len;

    cp = normalise(cp);
    if (cp == 0) continue;
    if (cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t') {
      pending_space = wrote > 0;
      continue;
    }
    if (!drawable(cp)) continue;

    char buf[4];
    size_t n = 0;
    if (pending_space) {
      pending_space = false;
      buf[n++] = ' ';
    }
    if (cp < 0x80) {
      buf[n++] = static_cast<char>(cp);
    } else if (cp < 0x800) {
      buf[n++] = static_cast<char>(0xC0 | (cp >> 6));
      buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      buf[n++] = static_cast<char>(0xE0 | (cp >> 12));
      buf[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    if (!put(buf, n)) break;
  }
}

}  // namespace data
