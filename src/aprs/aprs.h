#pragma once

#include <stddef.h>
#include <stdint.h>

namespace axlora::aprs {

// Parse an APRS info field. Returns true if recognized, fills summary with a
// human-readable description. summary is always NUL-terminated.
bool parse(const uint8_t* info, size_t len, char* summary, size_t cap);

// Encode an uncompressed APRS position info field (! prefix).
// lat positive = N, lon positive = E.
// symbol: 2 chars — table ID + symbol code (e.g. "/>" = car, "/-" = house).
// Returns true and fills out (NUL-terminated) on success.
bool encodePosition(double lat, double lon,
                    const char* symbol, const char* comment,
                    char* out, size_t cap);

}
