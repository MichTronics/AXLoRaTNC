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

// Parsed APRS message frame (info type ':').
struct AprsMessage {
  char addressee[10]{};  // destination callsign (trimmed, NUL-terminated)
  char text[100]{};      // message body (NUL-terminated)
  char msgNum[6]{};      // message number after '{' (empty = no ack needed)
  bool isAck = false;    // true if body starts with "ack"
  bool isRej = false;    // true if body starts with "rej"
};

// Parse an APRS ':' message info field into AprsMessage.
// Returns true if the frame is a valid APRS message.
bool parseMessage(const uint8_t* info, size_t len, AprsMessage& out);

// Encode an APRS message-ACK info field: `:ADDRESSEE:ack{msgNum}`.
// addressee is padded to 9 chars; msgNum is the number from the received frame.
// Returns true and fills out (NUL-terminated) on success.
bool encodeMessageAck(const char* addressee, const char* msgNum,
                      char* out, size_t cap);

}
