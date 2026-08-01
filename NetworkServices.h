#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ETH.h>
#include <ModbusIP_ESP8266.h>
#include <PubSubClient.h>
#include <WebServer.h>

#include "AppConfig.h"
#include "RelayController.h"

class NetworkServices {
 public:
  NetworkServices(AppConfig &config, RelayController &relays);
  void begin();
  void loop(bool ethernetHasIp);

 private:
  AppConfig &config_;
  RelayController &relays_;
  WebServer web_;
  NetworkClient mqttSocket_;
  PubSubClient mqtt_;
  ModbusIP modbus_;

  bool lastEthernetHasIp_ = false;
  bool modbusServerStarted_ = false;
  uint16_t activeModbusPort_ = 0;
  uint32_t lastMqttAttemptMs_ = 0;
  uint32_t lastHealthPublishMs_ = 0;
  uint32_t mqttReconnects_ = 0;
  uint32_t mqttMessagesReceived_ = 0;
  bool mqttReconfigurePending_ = false;
  bool relayStateDirty_ = false;

  static NetworkServices *instance_;
  static void mqttCallback(char *topic, byte *payload, unsigned int length);
  static uint16_t modbusSetCoil(TRegister *reg, uint16_t value);

  void setupHttp();
  void sendJson(int status, JsonDocument &document);
  void sendError(int status, const String &message);
  void handleHealth();
  void handleRelaysGet();
  void handleRelaysPut();
  void handleConfigGet();
  void handleConfigPut();
  void handleI2cGet();
  void handleI2cScan();
  void handleNotFound();
  void handleReboot();

  void fillHealth(JsonObject root);
  void fillRelays(JsonObject root) const;
  void fillI2cDiagnostics(JsonObject root) const;
  bool applyRelayDocument(JsonVariantConst root, String &error);
  bool applyConfigText(const String &body, String &error, bool &rebootRequired);

  void configureMqtt();
  void serviceMqtt(bool ethernetHasIp);
  void connectMqtt();
  void handleMqttMessage(const String &topic, const uint8_t *payload, size_t length);
  void publishAvailability(bool online);
  void publishRelayState(bool retained = true);
  void publishHealth();
  void publishConfigStatus(bool rebootRequired = false, const char *error = nullptr);
  String mqttPrefix() const;

  void configureModbus();
  void serviceModbus(bool ethernetHasIp);
  void syncModbusCoils();
};
