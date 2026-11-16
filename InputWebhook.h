#pragma once

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "AppConfig.h"

// Somente o worker usa HTTP/TLS. O loop de I/O nunca aguarda servidor remoto.
class InputWebhook {
 public:
  void begin();
  void enqueue(const DeviceConfig &config, uint8_t channel, bool active,
               uint8_t mask, uint32_t sequence, uint32_t changedMs);
  void toJson(JsonObject root);

 private:
  struct Job {
    char url[257];
    char token[129];
    char ca[2049];
    char device[33];
    uint32_t boot;
    uint32_t sequence;
    uint32_t changedMs;
    uint8_t channel;
    uint8_t mask;
    bool active;
  };
  QueueHandle_t queue_ = nullptr;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  uint32_t boot_ = 0;
  uint32_t delivered_ = 0;
  uint32_t failed_ = 0;
  uint32_t dropped_ = 0;
  int lastStatus_ = 0;
  static void task(void *context);
  void run();
};
