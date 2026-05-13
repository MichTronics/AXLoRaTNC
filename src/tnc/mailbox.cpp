#include "mailbox.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include <stdio.h>

namespace {
void formatUptime(uint32_t ms, char* buf, size_t cap) {
  const uint32_t s = ms / 1000;
  snprintf(buf, cap, "%02lu:%02lu:%02lu",
           static_cast<unsigned long>(s / 3600),
           static_cast<unsigned long>((s % 3600) / 60),
           static_cast<unsigned long>(s % 60));
}
}

namespace axlora::tnc {

static void copyField(char* dst, size_t cap, const char* src) {
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  size_t i = 0;
  for (; i < cap - 1 && src[i] != '\0'; ++i) {
    dst[i] = src[i];
  }
  dst[i] = '\0';
}

static bool callsignMatch(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) return false;
  size_t i = 0;
  while (a[i] != '\0' && b[i] != '\0') {
    char ca = a[i] >= 'a' && a[i] <= 'z' ? static_cast<char>(a[i] - 32) : a[i];
    char cb = b[i] >= 'a' && b[i] <= 'z' ? static_cast<char>(b[i] - 32) : b[i];
    if (ca != cb) return false;
    ++i;
  }
  return a[i] == '\0' && b[i] == '\0';
}

void Mailbox::begin() {
  load();
}

void Mailbox::load() {
  Preferences prefs;
  if (!prefs.begin("axlbbs", true)) return;
  for (uint8_t i = 0; i < MAX_MESSAGES; ++i) {
    char key[6]{};
    snprintf(key, sizeof(key), "a%u", static_cast<unsigned>(i));
    if (!prefs.getBool(key, false)) continue;
    messages_[i].active = true;
    snprintf(key, sizeof(key), "f%u", static_cast<unsigned>(i));
    prefs.getString(key, messages_[i].from, sizeof(messages_[i].from));
    snprintf(key, sizeof(key), "t%u", static_cast<unsigned>(i));
    prefs.getString(key, messages_[i].to, sizeof(messages_[i].to));
    snprintf(key, sizeof(key), "b%u", static_cast<unsigned>(i));
    prefs.getString(key, messages_[i].body, sizeof(messages_[i].body));
    snprintf(key, sizeof(key), "r%u", static_cast<unsigned>(i));
    messages_[i].read = prefs.getBool(key, false);
    snprintf(key, sizeof(key), "m%u", static_cast<unsigned>(i));
    messages_[i].postedMs = prefs.getULong(key, 0);
  }
  prefs.end();
}

void Mailbox::saveSlot(uint8_t index) {
  if (index >= MAX_MESSAGES) return;
  Preferences prefs;
  if (!prefs.begin("axlbbs", false)) return;
  char key[6]{};
  const Message& m = messages_[index];
  snprintf(key, sizeof(key), "a%u", static_cast<unsigned>(index));
  prefs.putBool(key, m.active);
  if (m.active) {
    snprintf(key, sizeof(key), "f%u", static_cast<unsigned>(index));
    prefs.putString(key, m.from);
    snprintf(key, sizeof(key), "t%u", static_cast<unsigned>(index));
    prefs.putString(key, m.to);
    snprintf(key, sizeof(key), "b%u", static_cast<unsigned>(index));
    prefs.putString(key, m.body);
    snprintf(key, sizeof(key), "r%u", static_cast<unsigned>(index));
    prefs.putBool(key, m.read);
    snprintf(key, sizeof(key), "m%u", static_cast<unsigned>(index));
    prefs.putULong(key, m.postedMs);
  }
  prefs.end();
}

bool Mailbox::post(const char* from, const char* to, const char* body) {
  if (from == nullptr || to == nullptr || body == nullptr) return false;
  uint8_t slot = MAX_MESSAGES;
  for (uint8_t i = 0; i < MAX_MESSAGES; ++i) {
    if (!messages_[i].active) { slot = i; break; }
  }
  if (slot == MAX_MESSAGES) return false;
  messages_[slot].active   = true;
  messages_[slot].read     = false;
  messages_[slot].postedMs = static_cast<uint32_t>(millis());
  copyField(messages_[slot].from, sizeof(messages_[slot].from), from);
  copyField(messages_[slot].to,   sizeof(messages_[slot].to),   to);
  copyField(messages_[slot].body, sizeof(messages_[slot].body), body);
  saveSlot(slot);
  return true;
}

bool Mailbox::markRead(uint8_t index) {
  if (index >= MAX_MESSAGES || !messages_[index].active) return false;
  messages_[index].read = true;
  saveSlot(index);
  return true;
}

bool Mailbox::kill(uint8_t index, const char* caller) {
  if (index >= MAX_MESSAGES || !messages_[index].active) return false;
  const Message& m = messages_[index];
  if (!callsignMatch(m.from, caller) && !callsignMatch(m.to, caller)) return false;
  messages_[index] = Message{};
  saveSlot(index);
  return true;
}

const Mailbox::Message* Mailbox::get(uint8_t index) const {
  if (index >= MAX_MESSAGES || !messages_[index].active) return nullptr;
  return &messages_[index];
}

size_t Mailbox::list(char* buf, size_t cap, const char* toCallsign) const {
  size_t pos = 0;
  auto append = [&](const char* s) {
    for (; *s != '\0' && pos + 1 < cap; ++s) buf[pos++] = *s;
  };
  bool any = false;
  for (uint8_t i = 0; i < MAX_MESSAGES; ++i) {
    const Message& m = messages_[i];
    if (!m.active) continue;
    if (toCallsign != nullptr && !callsignMatch(m.to, toCallsign)) continue;
    char ts[10]{};
    if (m.postedMs > 0) {
      formatUptime(m.postedMs, ts, sizeof(ts));
    } else {
      strncpy(ts, "--:--:--", sizeof(ts) - 1);
    }
    char row[72]{};
    snprintf(row, sizeof(row), "%2u %s %-10s->%-10s %s\r",
             static_cast<unsigned>(i + 1), ts, m.from, m.to,
             m.read ? "(read)" : "(new)");
    append(row);
    any = true;
  }
  if (!any) append("No messages\r");
  if (pos < cap) buf[pos] = '\0';
  return pos;
}

}
