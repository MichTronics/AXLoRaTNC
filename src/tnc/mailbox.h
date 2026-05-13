#pragma once

#include <stdint.h>
#include <stddef.h>

namespace axlora::tnc {

class Mailbox {
 public:
  static constexpr uint8_t MAX_MESSAGES = 12;
  static constexpr uint8_t MAX_FROM     = 10;
  static constexpr uint8_t MAX_TO       = 10;
  static constexpr uint8_t MAX_BODY     = 200;

  struct Message {
    char    from[MAX_FROM + 1]{};
    char    to[MAX_TO + 1]{};
    char    body[MAX_BODY + 1]{};
    bool    read   = false;
    bool    active = false;
  };

  void    begin();
  bool    post(const char* from, const char* to, const char* body);
  bool    markRead(uint8_t index);
  bool    kill(uint8_t index, const char* caller);
  const Message* get(uint8_t index) const;
  uint8_t maxMessages() const { return MAX_MESSAGES; }

  // Write a formatted listing into buf. Filters by toCallsign when non-null.
  size_t  list(char* buf, size_t cap, const char* toCallsign = nullptr) const;

 private:
  void load();
  void saveSlot(uint8_t index);

  Message messages_[MAX_MESSAGES]{};
};

}
