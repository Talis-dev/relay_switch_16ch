#pragma once
#include <WiFi.h>
#include <ETH.h>
#include <ArduinoJson.h>
#include "AppConfig.h"

class WifiGateway {
 public:
  void begin(const DeviceConfig &config);
  void loop(bool ethernetHasIp);
  void toJson(JsonObject root);
 private:
  bool started_ = false;
  bool nat_ = false;
  bool conflict_ = false;
  uint32_t lastAttemptMs_ = 0;
};
