#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <IPAddress.h>
#include <Preferences.h>

struct DeviceConfig {
  char deviceName[33];

  bool dhcp;
  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns1;
  IPAddress dns2;

  bool mqttEnabled;
  char mqttHost[65];
  uint16_t mqttPort;
  char mqttUser[65];
  char mqttPassword[97];
  char mqttBaseTopic[65];
  uint16_t mqttKeepAlive;

  bool modbusEnabled;
  uint16_t modbusPort;
  uint8_t modbusUnitId;
};

class AppConfig {
 public:
  void begin();
  const DeviceConfig &data() const { return config_; }

  // Aplica um JSON parcial. Campos ausentes sao preservados.
  bool update(JsonVariantConst root, String &error, bool &rebootRequired);
  bool save();
  void toJson(JsonObject target, bool includeSecret = false) const;

 private:
  Preferences preferences_;
  DeviceConfig config_;

  void setDefaults();
  bool load();
  static bool parseIp(JsonVariantConst value, IPAddress &result);
  static bool validName(const char *value);
  static void copyText(char *destination, size_t capacity, const char *source);
};
