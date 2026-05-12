#pragma once

#include <Arduino.h>

#define AX_LOG(tag, fmt, ...) \
  do { Serial.printf("[%s] " fmt "\n", tag, ##__VA_ARGS__); } while (0)

#define LOG_INFO(fmt, ...) AX_LOG("INFO", fmt, ##__VA_ARGS__)
#define LOG_RADIO(fmt, ...) AX_LOG("RADIO", fmt, ##__VA_ARGS__)
#define LOG_MESH(fmt, ...) AX_LOG("MESH", fmt, ##__VA_ARGS__)
#define LOG_PROTO(fmt, ...) AX_LOG("PROTO", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) AX_LOG("WARN", fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) AX_LOG("ERR", fmt, ##__VA_ARGS__)

