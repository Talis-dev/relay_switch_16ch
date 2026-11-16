#include "NetworkServices.h"

#include <ArduinoOTA.h>
#include <ESP.h>
#include "WebPage.h"
#include "ApiDocsPage.h"

namespace {
constexpr uint16_t HTTP_PORT = 80;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 10000;
constexpr uint32_t HEALTH_PUBLISH_INTERVAL_MS = 30000;
constexpr size_t MQTT_BUFFER_SIZE = 6144;

bool parseOnValue(JsonVariantConst value, bool &on) {
  if (value.is<bool>()) {
    on = value.as<bool>();
    return true;
  }
  if (value.is<int>()) {
    const int number = value.as<int>();
    if (number == 0 || number == 1) {
      on = number == 1;
      return true;
    }
  }
  if (value.is<const char *>()) {
    String text = value.as<const char *>();
    text.trim();
    text.toUpperCase();
    if (text == "ON" || text == "TRUE" || text == "1") { on = true; return true; }
    if (text == "OFF" || text == "FALSE" || text == "0") { on = false; return true; }
  }
  return false;
}
}  // namespace

NetworkServices *NetworkServices::instance_ = nullptr;

NetworkServices::NetworkServices(AppConfig &config, RelayController &relays)
    : config_(config), relays_(relays), web_(HTTP_PORT), mqtt_(mqttSocket_) {
  instance_ = this;
}

void NetworkServices::begin() {
  gateway_.begin(config_.data());
  webhook_.begin();
  setupHttp();
  web_.begin();
  configureMqtt();
  configureModbus();
  Serial.printf("[HTTP] API REST pronta na porta %u\n", HTTP_PORT);
}

void NetworkServices::loop(bool ethernetHasIp) {
  gateway_.loop(ethernetHasIp);
  const uint8_t changes = relays_.takeInputChanges();
  if (lastInputsValid_ != relays_.inputsValid()) inputsDirty_ = true;
  lastInputsValid_ = relays_.inputsValid();
  if (changes) {
    inputsDirty_ = true;
    for (uint8_t channel = 1; channel <= RelayController::INPUT_COUNT; ++channel) {
      if (changes & (1U << (channel - 1)))
        webhook_.enqueue(config_.data(), channel, relays_.input(channel), relays_.inputMask(),
                         relays_.inputSequence(), relays_.inputChangedMs(channel));
    }
  }
  web_.handleClient();
  serviceMqtt(ethernetHasIp);
  if (inputsDirty_) publishInputs();
  serviceModbus(ethernetHasIp);

  if (lastEthernetHasIp_ && !ethernetHasIp && mqtt_.connected()) {
    mqtt_.disconnect();
  }
  lastEthernetHasIp_ = ethernetHasIp;
}

void NetworkServices::setupHttp() {
  web_.on("/", HTTP_GET, [this]() {
    web_.send_P(200, "text/html; charset=utf-8", WEB_PAGE);
  });
  web_.on("/api/docs", HTTP_GET, [this]() {
    web_.send_P(200, "text/html; charset=utf-8", API_DOCS_PAGE);
  });
  web_.on("/api/v1/inputs", HTTP_GET, [this]() {
    JsonDocument doc;
    doc["ok"] = relays_.inputsValid();
    fillInputs(doc["inputs"].to<JsonObject>());
    sendJson(relays_.inputsValid() ? 200 : 503, doc);
  });
  web_.on("/api/v1/health", HTTP_GET, [this]() { handleHealth(); });
  web_.on("/api/v1/relays", HTTP_GET, [this]() { handleRelaysGet(); });
  web_.on("/api/v1/relays", HTTP_PUT, [this]() { handleRelaysPut(); });
  web_.on("/api/v1/config", HTTP_GET, [this]() { handleConfigGet(); });
  web_.on("/api/v1/config", HTTP_PUT, [this]() { handleConfigPut(); });
  web_.on("/api/v1/i2c", HTTP_GET, [this]() { handleI2cGet(); });
  web_.on("/api/v1/i2c/scan", HTTP_POST, [this]() { handleI2cScan(); });
  web_.on("/api/v1/reboot", HTTP_POST, [this]() { handleReboot(); });
  web_.onNotFound([this]() { handleNotFound(); });
}

void NetworkServices::sendJson(int status, JsonDocument &document) {
  String response;
  serializeJson(document, response);
  web_.sendHeader("Cache-Control", "no-store");
  web_.send(status, "application/json; charset=utf-8", response);
}

void NetworkServices::sendError(int status, const String &message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message;
  sendJson(status, doc);
}

void NetworkServices::fillRelays(JsonObject root) const {
  root["mask"] = relays_.mask();
  root["pcf8574_ok"] = relays_.pcfHealthy();
  root["state_type"] = "commanded";
  root["outputs_enabled"] = relays_.outputsEnabled();
  root["write_errors"] = relays_.writeErrors();
  JsonArray channels = root["channels"].to<JsonArray>();
  for (uint8_t channel = 1; channel <= RelayController::CHANNEL_COUNT; ++channel) {
    JsonObject item = channels.add<JsonObject>();
    item["channel"] = channel;
    item["on"] = relays_.get(channel);
  }
}

void NetworkServices::fillHealth(JsonObject root) {
  root["status"] = relays_.pcfHealthy() ? "ok" : "degraded";
  root["device_name"] = config_.data().deviceName;
  root["uptime_ms"] = millis();
  root["free_heap"] = ESP.getFreeHeap();
  root["min_free_heap"] = ESP.getMinFreeHeap();

  JsonObject ethernet = root["ethernet"].to<JsonObject>();
  ethernet["link"] = ETH.linkUp();
  ethernet["ip"] = ETH.localIP().toString();
  ethernet["mac"] = ETH.macAddress();
  ethernet["speed_mbps"] = ETH.linkSpeed();
  ethernet["full_duplex"] = ETH.fullDuplex();
  ethernet["dhcp"] = config_.data().dhcp;

  JsonObject relay = root["relay_controller"].to<JsonObject>();
  relay["pcf8574_ok"] = relays_.pcfHealthy();
  relay["mask"] = relays_.mask();
  relay["write_errors"] = relays_.writeErrors();
  relay["enable_gpio"] = RelayController::RELAY_ENABLE_GPIO;
  relay["outputs_enabled"] = relays_.outputsEnabled();

  JsonObject mqtt = root["mqtt"].to<JsonObject>();
  mqtt["enabled"] = config_.data().mqttEnabled;
  mqtt["connected"] = mqtt_.connected();
  mqtt["state"] = mqtt_.state();
  mqtt["reconnects"] = mqttReconnects_;
  mqtt["messages_received"] = mqttMessagesReceived_;

  JsonObject modbus = root["modbus"].to<JsonObject>();
  modbus["enabled"] = config_.data().modbusEnabled;
  modbus["server_started"] = modbusServerStarted_;
  modbus["configured_port"] = config_.data().modbusPort;
  modbus["active_port"] = activeModbusPort_;
  modbus["unit_id"] = config_.data().modbusUnitId;
  fillInputs(root["inputs"].to<JsonObject>());
  gateway_.toJson(root["wifi_ap"].to<JsonObject>());
  webhook_.toJson(root["webhook"].to<JsonObject>());
}

void NetworkServices::handleHealth() {
  JsonDocument doc;
  doc["ok"] = true;
  fillHealth(doc["health"].to<JsonObject>());
  sendJson(200, doc);
}

void NetworkServices::fillInputs(JsonObject root) const {
  root["valid"] = relays_.inputsValid();
  root["mask"] = relays_.inputMask();
  root["sequence"] = relays_.inputSequence();
  root["sample_ms"] = relays_.inputSampleMs();
  JsonArray channels = root["channels"].to<JsonArray>();
  for (uint8_t i = 1; i <= RelayController::INPUT_COUNT; ++i) {
    JsonObject item = channels.add<JsonObject>();
    item["channel"] = i;
    item["active"] = relays_.input(i);
    item["changed_ms"] = relays_.inputChangedMs(i);
  }
}

void NetworkServices::publishInputs() {
  if (!mqtt_.connected()) return;
  JsonDocument doc;
  fillInputs(doc.to<JsonObject>());
  String payload;
  serializeJson(doc, payload);
  if (mqtt_.publish((mqttPrefix() + "/inputs/state").c_str(), payload.c_str(), true))
    inputsDirty_ = false;
}

void NetworkServices::handleRelaysGet() {
  JsonDocument doc;
  doc["ok"] = true;
  fillRelays(doc["relays"].to<JsonObject>());
  sendJson(200, doc);
}

void NetworkServices::fillI2cDiagnostics(JsonObject root) const {
  JsonObject bus = root["bus"].to<JsonObject>();
  bus["sda_gpio"] = RelayController::I2C_SDA;
  bus["scl_gpio"] = RelayController::I2C_SCL;
  bus["frequency_hz"] = RelayController::I2C_FREQUENCY;
  bus["timeout_ms"] = RelayController::I2C_TIMEOUT_MS;
  bus["sda_level"] = relays_.sdaLevel() ? "HIGH" : "LOW";
  bus["scl_level"] = relays_.sclLevel() ? "HIGH" : "LOW";
  bus["stuck_low"] = !relays_.sdaLevel() || !relays_.sclLevel();

  JsonObject expected = root["expected_device"].to<JsonObject>();
  expected["name"] = "PCF8574";
  char addressText[5];
  snprintf(addressText, sizeof(addressText), "0x%02X", relays_.address());
  expected["address"] = addressText;
  expected["ack"] = relays_.pcfHealthy();
  expected["last_error_code"] = relays_.lastI2cError();
  expected["last_error"] = relays_.lastI2cErrorText();
  expected["last_transaction_us"] = relays_.lastTransactionDurationUs();
  expected["last_success_ms"] = relays_.lastSuccessMs();
  expected["last_failure_ms"] = relays_.lastFailureMs();
  expected["write_errors"] = relays_.writeErrors();

  JsonObject scan = root["scan"].to<JsonObject>();
  scan["running"] = relays_.scanRunning();
  scan["completed"] = relays_.scanCompleted();
  scan["current_address"] = relays_.scanCurrentAddress();
  scan["duration_ms"] = relays_.scanDurationMs();
  scan["nack_count"] = relays_.scanNackCount();
  scan["timeout_count"] = relays_.scanTimeoutCount();
  scan["other_error_count"] = relays_.scanOtherErrorCount();
  scan["results_truncated"] = relays_.scanResultsTruncated();
  JsonArray found = scan["found_addresses"].to<JsonArray>();
  bool expectedFound = false;
  for (uint8_t i = 0; i < relays_.scanFoundCount(); ++i) {
    const uint8_t address = relays_.scanFoundAddress(i);
    char formatted[5];
    snprintf(formatted, sizeof(formatted), "0x%02X", address);
    found.add(formatted);
    if (address == relays_.address()) expectedFound = true;
  }

  if (!relays_.sdaLevel() || !relays_.sclLevel()) {
    root["diagnosis"] = "barramento preso em LOW: verifique curto, nivel logico, pull-ups e alimentacao";
  } else if (relays_.pcfHealthy()) {
    root["diagnosis"] = "PCF8574 respondeu e aceitou escrita";
  } else if (relays_.scanRunning()) {
    root["diagnosis"] = "scan I2C em andamento sem bloquear a rede";
  } else if (relays_.scanCompleted() && relays_.scanTimeoutCount() > 0) {
    root["diagnosis"] = "houve timeout I2C; confira pull-ups, nivel de tensao, curto e integridade de SDA/SCL";
  } else if (relays_.scanCompleted() && relays_.scanOtherErrorCount() > 0) {
    root["diagnosis"] = "o controlador registrou erros I2C diferentes de NACK; confira fiacao e alimentacao";
  } else if (relays_.scanCompleted() && expectedFound) {
    root["diagnosis"] = "o endereco configurado foi encontrado no scan, mas a ultima escrita falhou";
  } else if (relays_.scanCompleted() && relays_.scanFoundCount() > 0) {
    root["diagnosis"] = "existem dispositivos I2C, mas o PCF8574 nao respondeu no endereco configurado; confira A0/A1/A2";
  } else if (relays_.scanCompleted()) {
    root["diagnosis"] = "nenhum dispositivo respondeu; confira VCC, GND, SDA, SCL e nivel de pull-up";
  } else {
    root["diagnosis"] = "PCF8574 sem ACK; execute POST /api/v1/i2c/scan";
  }
}

void NetworkServices::handleI2cGet() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["note"] = "leitura instantanea do cache; este GET nao acessa nem bloqueia o I2C";
  fillI2cDiagnostics(doc["i2c"].to<JsonObject>());
  sendJson(200, doc);
}

void NetworkServices::handleI2cScan() {
  const bool started = relays_.requestI2cScan();
  JsonDocument doc;
  doc["ok"] = true;
  doc["started"] = started;
  doc["message"] = started ? "scan iniciado; consulte GET /api/v1/i2c"
                            : "scan ja estava em andamento";
  fillI2cDiagnostics(doc["i2c"].to<JsonObject>());
  sendJson(202, doc);
}

bool NetworkServices::applyRelayDocument(JsonVariantConst root, String &error) {
  if (!root.is<JsonObjectConst>()) {
    error = "payload deve ser um objeto JSON";
    return false;
  }

  if (!root["mask"].isNull()) {
    const uint32_t mask = root["mask"].as<uint32_t>();
    if (!root["mask"].is<uint32_t>() || mask > RelayController::OUTPUT_MASK) {
      error = "mask deve estar entre 0 e 31"; return false;
    }
    relays_.setMask(static_cast<uint16_t>(mask));
    syncModbusCoils();
    return true;
  }

  JsonArrayConst states = root["states"];
  if (!states.isNull()) {
    if (states.size() != RelayController::CHANNEL_COUNT) {
      error = "states deve conter exatamente 5 valores booleanos";
      return false;
    }
    uint16_t mask = 0;
    for (uint8_t i = 0; i < RelayController::CHANNEL_COUNT; ++i) {
      bool on = false;
      if (!parseOnValue(states[i], on)) { error = "states contem valor invalido"; return false; }
      if (on) mask |= static_cast<uint16_t>(1U << i);
    }
    relays_.setMask(mask);
    syncModbusCoils();
    return true;
  }

  error = "informe mask ou states";
  return false;
}

void NetworkServices::handleRelaysPut() {
  JsonDocument input;
  const DeserializationError jsonError = deserializeJson(input, web_.arg("plain"));
  if (jsonError) { sendError(400, String("JSON invalido: ") + jsonError.c_str()); return; }
  String error;
  if (!applyRelayDocument(input.as<JsonVariantConst>(), error)) { sendError(422, error); return; }

  publishRelayState();
  JsonDocument response;
  response["ok"] = relays_.pcfHealthy();
  fillRelays(response["relays"].to<JsonObject>());
  if (!relays_.pcfHealthy()) response["warning"] = "estado solicitado, mas PCF8574 nao confirmou escrita I2C";
  sendJson(relays_.pcfHealthy() ? 200 : 503, response);
}

void NetworkServices::handleConfigGet() {
  JsonDocument doc;
  doc["ok"] = true;
  config_.toJson(doc["config"].to<JsonObject>(), false);
  sendJson(200, doc);
}

bool NetworkServices::applyConfigText(const String &body, String &error, bool &rebootRequired) {
  JsonDocument input;
  const DeserializationError jsonError = deserializeJson(input, body);
  if (jsonError) { error = String("JSON invalido: ") + jsonError.c_str(); return false; }

  const DeviceConfig previous = config_.data();
  if (!config_.update(input.as<JsonVariantConst>(), error, rebootRequired)) return false;
  if (!config_.save()) { config_.restore(previous); error = "falha ao gravar configuracao na NVS"; return false; }

  const DeviceConfig &current = config_.data();
  const bool mqttChanged = previous.mqttEnabled != current.mqttEnabled ||
                           previous.mqttPort != current.mqttPort ||
                           previous.mqttKeepAlive != current.mqttKeepAlive ||
                           strcmp(previous.mqttHost, current.mqttHost) != 0 ||
                           strcmp(previous.mqttUser, current.mqttUser) != 0 ||
                           strcmp(previous.mqttPassword, current.mqttPassword) != 0 ||
                           strcmp(previous.mqttBaseTopic, current.mqttBaseTopic) != 0;
  // Pode ser chamado de dentro do callback MQTT. Reconectar no proximo loop
  // evita destruir o socket enquanto PubSubClient ainda processa a mensagem.
  if (mqttChanged) mqttReconfigurePending_ = true;
  if (!previous.modbusEnabled && current.modbusEnabled) configureModbus();
  return true;
}

void NetworkServices::handleConfigPut() {
  String error;
  bool rebootRequired = false;
  if (!applyConfigText(web_.arg("plain"), error, rebootRequired)) { sendError(422, error); return; }

  publishConfigStatus(rebootRequired);
  JsonDocument doc;
  doc["ok"] = true;
  doc["saved"] = true;
  doc["reboot_required"] = rebootRequired;
  config_.toJson(doc["config"].to<JsonObject>(), false);
  sendJson(200, doc);
}

void NetworkServices::handleReboot() {
  relays_.shutdown();
  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = "reiniciando em 500 ms";
  sendJson(202, doc);
  delay(500);
  ESP.restart();
}

void NetworkServices::handleNotFound() {
  const String prefix = "/api/v1/relays/";
  if (web_.method() == HTTP_PUT && web_.uri().startsWith(prefix)) {
    const String channelText = web_.uri().substring(prefix.length());
    for (size_t i = 0; i < channelText.length(); ++i) {
      if (!isDigit(channelText[i])) { sendError(404, "rota nao encontrada"); return; }
    }
    const int channel = channelText.toInt();
    if (channel < 1 || channel > RelayController::CHANNEL_COUNT) {
      sendError(422, "canal deve estar entre 1 e 5");
      return;
    }
    JsonDocument input;
    const DeserializationError jsonError = deserializeJson(input, web_.arg("plain"));
    if (jsonError) { sendError(400, String("JSON invalido: ") + jsonError.c_str()); return; }
    bool on = false;
    if (!parseOnValue(input["on"], on)) { sendError(422, "campo on deve ser booleano"); return; }
    relays_.set(static_cast<uint8_t>(channel), on);
    syncModbusCoils();
    publishRelayState();

    JsonDocument response;
    response["ok"] = relays_.pcfHealthy();
    response["channel"] = channel;
    response["on"] = on;
    response["mask"] = relays_.mask();
    if (!relays_.pcfHealthy()) response["warning"] = "PCF8574 sem resposta I2C";
    sendJson(relays_.pcfHealthy() ? 200 : 503, response);
    return;
  }
  sendError(404, "rota nao encontrada");
}

void NetworkServices::configureMqtt() {
  if (mqtt_.connected()) mqtt_.disconnect();
  const DeviceConfig &cfg = config_.data();
  mqttSocket_.setConnectionTimeout(1000);
  mqttSocket_.setTimeout(1000);
  mqtt_.setServer(cfg.mqttHost, cfg.mqttPort);
  mqtt_.setCallback(mqttCallback);
  mqtt_.setKeepAlive(cfg.mqttKeepAlive);
  mqtt_.setSocketTimeout(1);
  mqtt_.setBufferSize(MQTT_BUFFER_SIZE);
  lastMqttAttemptMs_ = 0;
}

String NetworkServices::mqttPrefix() const {
  String prefix = config_.data().mqttBaseTopic;
  while (prefix.endsWith("/")) prefix.remove(prefix.length() - 1);
  prefix += "/";
  prefix += config_.data().deviceName;
  return prefix;
}

void NetworkServices::serviceMqtt(bool ethernetHasIp) {
  if (mqttReconfigurePending_) {
    mqttReconfigurePending_ = false;
    configureMqtt();
  }
  if (!config_.data().mqttEnabled || !ethernetHasIp) return;
  if (!mqtt_.connected()) {
    if (millis() - lastMqttAttemptMs_ >= MQTT_RECONNECT_INTERVAL_MS || lastMqttAttemptMs_ == 0) connectMqtt();
    return;
  }
  mqtt_.loop();
  if (millis() - lastHealthPublishMs_ >= HEALTH_PUBLISH_INTERVAL_MS) publishHealth();
}

void NetworkServices::connectMqtt() {
  lastMqttAttemptMs_ = millis();
  const DeviceConfig &cfg = config_.data();
  const String availability = mqttPrefix() + "/availability";
  bool connected;
  if (cfg.mqttUser[0]) {
    connected = mqtt_.connect(cfg.deviceName, cfg.mqttUser, cfg.mqttPassword,
                              availability.c_str(), 1, true, "offline");
  } else {
    connected = mqtt_.connect(cfg.deviceName, availability.c_str(), 1, true, "offline");
  }
  if (!connected) {
    Serial.printf("[MQTT] conexao falhou, estado %d\n", mqtt_.state());
    return;
  }

  ++mqttReconnects_;
  const String prefix = mqttPrefix();
  mqtt_.subscribe((prefix + "/relay/+/set").c_str(), 1);
  mqtt_.subscribe((prefix + "/relays/set").c_str(), 1);
  mqtt_.subscribe((prefix + "/config/set").c_str(), 1);
  mqtt_.subscribe((prefix + "/config/get").c_str(), 0);
  publishAvailability(true);
  publishRelayState();
  publishInputs();
  publishHealth();
  publishConfigStatus();
  Serial.println("[MQTT] conectado");
}

void NetworkServices::mqttCallback(char *topic, byte *payload, unsigned int length) {
  if (instance_) instance_->handleMqttMessage(String(topic), payload, length);
}

void NetworkServices::handleMqttMessage(const String &topic, const uint8_t *payload, size_t length) {
  ++mqttMessagesReceived_;
  String body;
  body.reserve(length + 1);
  for (size_t i = 0; i < length; ++i) body += static_cast<char>(payload[i]);
  const String prefix = mqttPrefix();

  if (topic == prefix + "/relays/set") {
    JsonDocument input;
    String error;
    if (deserializeJson(input, body) || !applyRelayDocument(input.as<JsonVariantConst>(), error)) {
      publishConfigStatus(false, error.c_str());
      return;
    }
    publishRelayState();
    return;
  }

  const String relayPrefix = prefix + "/relay/";
  if (topic.startsWith(relayPrefix) && topic.endsWith("/set")) {
    const int suffixStart = relayPrefix.length();
    const int suffixEnd = topic.length() - 4;
    const String channelText = topic.substring(suffixStart, suffixEnd);
    if (channelText.length() != 1 || channelText[0] < '1' || channelText[0] > '5') return;
    const int channel = channelText.toInt();
    bool on = false;
    JsonDocument input;
    const DeserializationError parseError = deserializeJson(input, body);
    bool valid = false;
    if (!parseError) {
      valid = input.is<JsonObject>() ? parseOnValue(input["on"], on)
                                    : parseOnValue(input.as<JsonVariantConst>(), on);
    } else {
      String text = body;
      text.trim();
      JsonDocument scalar;
      scalar.set(text);
      valid = parseOnValue(scalar.as<JsonVariantConst>(), on);
    }
    if (!valid) return;
    if (channel >= 1 && channel <= RelayController::CHANNEL_COUNT) {
      relays_.set(static_cast<uint8_t>(channel), on);
      syncModbusCoils();
      publishRelayState();
    }
    return;
  }

  if (topic == prefix + "/config/set") {
    String error;
    bool rebootRequired = false;
    if (!applyConfigText(body, error, rebootRequired)) publishConfigStatus(false, error.c_str());
    else publishConfigStatus(rebootRequired);
    return;
  }
  if (topic == prefix + "/config/get") publishConfigStatus();
}

void NetworkServices::publishAvailability(bool online) {
  if (!mqtt_.connected()) return;
  mqtt_.publish((mqttPrefix() + "/availability").c_str(), online ? "online" : "offline", true);
}

void NetworkServices::publishRelayState(bool retained) {
  if (!mqtt_.connected()) return;
  JsonDocument doc;
  fillRelays(doc.to<JsonObject>());
  String payload;
  serializeJson(doc, payload);
  const String prefix = mqttPrefix();
  mqtt_.publish((prefix + "/relays/state").c_str(), payload.c_str(), retained);
  for (uint8_t channel = 1; channel <= RelayController::CHANNEL_COUNT; ++channel) {
    mqtt_.publish((prefix + "/relay/" + channel + "/state").c_str(),
                  relays_.get(channel) ? "ON" : "OFF", retained);
  }
}

void NetworkServices::publishHealth() {
  if (!mqtt_.connected()) return;
  JsonDocument doc;
  fillHealth(doc.to<JsonObject>());
  String payload;
  serializeJson(doc, payload);
  mqtt_.publish((mqttPrefix() + "/health").c_str(), payload.c_str(), false);
  lastHealthPublishMs_ = millis();
}

void NetworkServices::publishConfigStatus(bool rebootRequired, const char *error) {
  if (!mqtt_.connected()) return;
  JsonDocument doc;
  doc["ok"] = error == nullptr;
  doc["reboot_required"] = rebootRequired;
  if (error) doc["error"] = error;
  config_.toJson(doc["config"].to<JsonObject>(), false);
  String payload;
  serializeJson(doc, payload);
  mqtt_.publish((mqttPrefix() + "/config/status").c_str(), payload.c_str(), true);
}

void NetworkServices::configureModbus() {
  if (!config_.data().modbusEnabled || modbusServerStarted_) return;
  activeModbusPort_ = config_.data().modbusPort;
  modbus_.server(activeModbusPort_);
  modbus_.addCoil(0, false, RelayController::CHANNEL_COUNT);
  modbus_.onSetCoil(0, modbusSetCoil, RelayController::CHANNEL_COUNT);
  syncModbusCoils();
  modbusServerStarted_ = true;
  Serial.printf("[MODBUS] servidor TCP iniciado na porta %u\n", activeModbusPort_);
}

void NetworkServices::serviceModbus(bool ethernetHasIp) {
  if (config_.data().modbusEnabled && !modbusServerStarted_) configureModbus();
  if (ethernetHasIp && config_.data().modbusEnabled && modbusServerStarted_) {
    modbus_.task();
    if (relayStateDirty_) {
      relayStateDirty_ = false;
      publishRelayState();
    }
  }
}

uint16_t NetworkServices::modbusSetCoil(TRegister *reg, uint16_t value) {
  if (!instance_ || !reg) return value;
  const uint16_t address = reg->address.address;
  if (address < RelayController::CHANNEL_COUNT) {
    instance_->relays_.set(static_cast<uint8_t>(address + 1), COIL_BOOL(value));
    instance_->relayStateDirty_ = true;
  }
  return value;
}

void NetworkServices::syncModbusCoils() {
  if (!modbusServerStarted_) return;
  for (uint8_t i = 0; i < RelayController::CHANNEL_COUNT; ++i) {
    modbus_.Coil(i, relays_.get(i + 1));
  }
}
