#pragma once

#include <cstddef>

#include "data/model.h"

namespace quotes {

// One Yahoo Finance chart request per symbol (keyless). Symbols may be Yahoo
// tickers ("^GSPC", "GC=F", "BTC-USD") or CoinGecko ids ("bitcoin"), which are
// mapped before the request.
bool fetch(const char* const* symbols, int count, data::QuoteSet& out);

void yahoo_symbol(const char* id, char* out, size_t cap);

}  // namespace quotes
