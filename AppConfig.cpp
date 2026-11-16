#include "AppConfig.h"

namespace {
constexpr const char *NVS_NAMESPACE = "relay16";
constexpr const char *NVS_KEY = "config";
constexpr uint16_t CONFIG_VERSION = 1;

void addIp(JsonObject object, const char *key, const IPAddress &ip) {
  object[key] = ip.toString();
}
}  // namespace

void AppConfig::copyText(char *destination, size_t capacity, const char *source) {
  if (!capacity) return;
  strlcpy(destination, source ? source : "", capacity);
}

void AppConfig::setDefaults() {
  memset(&config_, 0, sizeof(config_));
  copyText(config_.deviceName, sizeof(config_.deviceName), "relay-switch-16ch");
  config_.dhcp = true;
  config_.ip = IPAddress(192, 168, 1, 50);
  config_.gateway = IPAddress(192, 168, 1, 1);
  config_.subnet = IPAddress(255, 255, 255, 0);
  config_.dns1 = IPAddress(192, 168, 1, 1);
  config_.dns2 = IPAddress(8, 8, 8, 8);
  config_.mqttEnabled = false;
  config_.mqttPort = 1883;
  copyText(config_.mqttBaseTopic, sizeof(config_.mqttBaseTopic), "industrial/relay16");
  config_.mqttKeepAlive = 30;
  config_.modbusEnabled = false;
  config_.modbusPort = 502;
  config_.modbusUnitId = 1;
}

void AppConfig::begin() {
  setDefaults();
  preferences_.begin(NVS_NAMESPACE, false);
  if (!load()) {
    Serial.println("[CFG] usando configuracao padrao");
    save();
  } else {
    Serial.println("[CFG] configuracao persistente carregada");
  }
}

bool AppConfig::load() {
  const String stored = preferences_.getString(NVS_KEY, "");
  if (stored.isEmpty()) return false;

  JsonDocument doc;
  if (deserializeJson(doc, stored) != DeserializationError::Ok) return false;
  if ((doc["version"] | 0U) != CONFIG_VERSION) return false;

  String error;
  bool rebootRequired = false;
  return update(doc.as<JsonVariantConst>(), error, rebootRequired);
}

bool AppConfig::save() {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["version"] = CONFIG_VERSION;
  toJson(root, true);
  String encoded;
  serializeJson(doc, encoded);
  return preferences_.putString(NVS_KEY, encoded) == encoded.length();
}

bool AppConfig::parseIp(JsonVariantConst value, IPAddress &result) {
  if (!value.is<const char *>()) return false;
  IPAddress parsed;
  if (!parsed.fromString(value.as<const char *>())) return false;
  result = parsed;
  return true;
}

bool AppConfig::validName(const char *value) {
  if (!value || !value[0] || strlen(value) > 32) return false;
  for (const char *p = value; *p; ++p) {
    if (!(isalnum(static_cast<unsigned char>(*p)) || *p == '-')) return false;
  }
  return true;
}

bool AppConfig::update(JsonVariantConst root, String &error, bool &rebootRequired) {
  rebootRequired = false;
  if (!root.is<JsonObjectConst>()) {
    error = "payload deve ser um objeto JSON";
    return false;
  }

  DeviceConfig candidate = config_;

  if (!root["device_name"].isNull()) {
    const char *name = root["device_name"] | "";
    if (!validName(name)) {
      error = "device_name deve ter 1-32 caracteres: letras, numeros ou hifen";
      return false;
    }
    if (strcmp(candidate.deviceName, name) != 0) rebootRequired = true;
    copyText(candidate.deviceName, sizeof(candidate.deviceName), name);
  }

  JsonObjectConst network = root["network"];
  if (!network.isNull()) {
    if (!network["dhcp"].isNull()) {
      const bool nextDhcp = network["dhcp"].as<bool>();
      rebootRequired |= nextDhcp != candidate.dhcp;
      candidate.dhcp = nextDhcp;
    }
    struct IpField { const char *name; IPAddress *target; } fields[] = {
      {"ip", &candidate.ip}, {"gateway", &candidate.gateway},
      {"subnet", &candidate.subnet}, {"dns1", &candidate.dns1},
      {"dns2", &candidate.dns2}
    };
    for (auto &field : fields) {
      if (!network[field.name].isNull()) {
        if (!parseIp(network[field.name], *field.target)) {
          error = String("endereco invalido em network.") + field.name;
          return false;
        }
        rebootRequired = true;
      }
    }
  }

  JsonObjectConst mqtt = root["mqtt"];
  if (!mqtt.isNull()) {
    if (!mqtt["enabled"].isNull()) candidate.mqttEnabled = mqtt["enabled"].as<bool>();
    if (!mqtt["host"].isNull()) copyText(candidate.mqttHost, sizeof(candidate.mqttHost), mqtt["host"] | "");
    if (!mqtt["port"].isNull()) {
      const uint32_t port = mqtt["port"].as<uint32_t>();
      if (port == 0 || port > 65535) { error = "mqtt.port invalida"; return false; }
      candidate.mqttPort = static_cast<uint16_t>(port);
    }
    if (!mqtt["user"].isNull()) copyText(candidate.mqttUser, sizeof(candidate.mqttUser), mqtt["user"] | "");
    if (!mqtt["password"].isNull() && strcmp(mqtt["password"] | "", "********") != 0) {
      copyText(candidate.mqttPassword, sizeof(candidate.mqttPassword), mqtt["password"] | "");
    }
    if (!mqtt["base_topic"].isNull()) copyText(candidate.mqttBaseTopic, sizeof(candidate.mqttBaseTopic), mqtt["base_topic"] | "");
    if (!mqtt["keep_alive"].isNull()) {
      const uint32_t keepAlive = mqtt["keep_alive"].as<uint32_t>();
      if (keepAlive < 10 || keepAlive > 300) { error = "mqtt.keep_alive deve estar entre 10 e 300"; return false; }
      candidate.mqttKeepAlive = static_cast<uint16_t>(keepAlive);
    }
    if (candidate.mqttEnabled && candidate.mqttHost[0] == '\0') {
      error = "mqtt.host e obrigatorio quando MQTT esta habilitado";
      return false;
    }
  }

  JsonObjectConst modbus = root["modbus"];
  if (!modbus.isNull()) {
    if (!modbus["enabled"].isNull()) candidate.modbusEnabled = modbus["enabled"].as<bool>();
    if (!modbus["port"].isNull()) {
      const uint32_t port = modbus["port"].as<uint32_t>();
      if (port == 0 || port > 65535) { error = "modbus.port invalida"; return false; }
      if (port != candidate.modbusPort) rebootRequired = true;
      candidate.modbusPort = static_cast<uint16_t>(port);
    }
    if (!modbus["unit_id"].isNull()) {
      const uint32_t unit = modbus["unit_id"].as<uint32_t>();
      if (unit == 0 || unit > 247) { error = "modbus.unit_id deve estar entre 1 e 247"; return false; }
      candidate.modbusUnitId = static_cast<uint8_t>(unit);
    }
  }

  config_ = candidate;
  return true;
}

void AppConfig::toJson(JsonObject root, bool includeSecret) const {
  root["device_name"] = config_.deviceName;
  JsonObject network = root["network"].to<JsonObject>();
  network["dhcp"] = config_.dhcp;
  addIp(network, "ip", config_.ip);
  addIp(network, "gateway", config_.gateway);
  addIp(network, "subnet", config_.subnet);
  addIp(network, "dns1", config_.dns1);
  addIp(network, "dns2", config_.dns2);

  JsonObject mqtt = root["mqtt"].to<JsonObject>();
  mqtt["enabled"] = config_.mqttEnabled;
  mqtt["host"] = config_.mqttHost;
  mqtt["port"] = config_.mqttPort;
  mqtt["user"] = config_.mqttUser;
  mqtt["password"] = includeSecret ? config_.mqttPassword : (config_.mqttPassword[0] ? "********" : "");
  mqtt["base_topic"] = config_.mqttBaseTopic;
  mqtt["keep_alive"] = config_.mqttKeepAlive;

  JsonObject modbus = root["modbus"].to<JsonObject>();
  modbus["enabled"] = config_.modbusEnabled;
  modbus["port"] = config_.modbusPort;
  modbus["unit_id"] = config_.modbusUnitId;
}
