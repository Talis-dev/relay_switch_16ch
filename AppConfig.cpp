#include "AppConfig.h"

namespace {
constexpr const char *NVS_NAMESPACE = "relay5";
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
  copyText(config_.deviceName, sizeof(config_.deviceName), "relay-switch-5ch");
  config_.dhcp = true;
  config_.ip = IPAddress(192, 168, 1, 50);
  config_.gateway = IPAddress(192, 168, 1, 1);
  config_.subnet = IPAddress(255, 255, 255, 0);
  config_.dns1 = IPAddress(192, 168, 1, 1);
  config_.dns2 = IPAddress(8, 8, 8, 8);
  config_.mqttEnabled = false;
  config_.mqttPort = 1883;
  copyText(config_.mqttBaseTopic, sizeof(config_.mqttBaseTopic), "industrial/relay5");
  config_.mqttKeepAlive = 30;
  config_.modbusEnabled = false;
  config_.modbusPort = 502;
  config_.modbusUnitId = 1;
  config_.pcfAddress = 0x20;
  config_.inputActiveLow = true;
  config_.inputDebounceMs = 50;
  copyText(config_.apSsid, sizeof(config_.apSsid), "Relay-5CH");
  config_.apIp = IPAddress(192, 168, 4, 1);
  config_.apDns = IPAddress(8, 8, 8, 8);
  config_.apChannel = 6;
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
  for (const char *section : {"network", "mqtt", "modbus", "io", "webhook", "wifi_ap"}) {
    if (!root[section].isNull() && !root[section].is<JsonObjectConst>()) {
      error = String(section) + " deve ser um objeto JSON"; return false;
    }
  }
  struct BoolField { const char *section; const char *key; } booleans[] = {
    {"network", "dhcp"}, {"mqtt", "enabled"}, {"modbus", "enabled"},
    {"io", "input_active_low"}, {"webhook", "enabled"}, {"wifi_ap", "enabled"}
  };
  for (const auto &field : booleans) {
    JsonVariantConst value = root[field.section][field.key];
    if (!value.isNull() && !value.is<bool>()) {
      error = String(field.section) + "." + field.key + " deve ser booleano"; return false;
    }
  }
  struct TextLimit { const char *section; const char *key; size_t limit; } textLimits[] = {
    {"mqtt", "host", 64}, {"mqtt", "user", 64}, {"mqtt", "password", 96},
    {"mqtt", "base_topic", 64}, {"wifi_ap", "ssid", 32}, {"wifi_ap", "password", 63}
  };
  for (const auto &field : textLimits) {
    JsonVariantConst value = root[field.section][field.key];
    if (!value.isNull() && (!value.is<const char *>() || strlen(value.as<const char *>()) > field.limit)) {
      error = String(field.section) + "." + field.key + " invalido ou muito longo"; return false;
    }
  }

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
        const IPAddress previousIp = *field.target;
        if (!parseIp(network[field.name], *field.target)) {
          error = String("endereco invalido em network.") + field.name;
          return false;
        }
        rebootRequired |= previousIp != *field.target;
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

  JsonObjectConst io = root["io"];
  if (!io.isNull()) {
    if (!io["address"].isNull()) {
      const unsigned address = io["address"].as<unsigned>();
      if (!((address >= 0x20 && address <= 0x27) || (address >= 0x38 && address <= 0x3F))) {
        error = "io.address: use 32-39 (PCF8574) ou 56-63 (PCF8574A)"; return false;
      }
      candidate.pcfAddress = address;
    }
    if (!io["input_active_low"].isNull()) candidate.inputActiveLow = io["input_active_low"].as<bool>();
    if (!io["debounce_ms"].isNull()) {
      if (!io["debounce_ms"].is<unsigned>() || io["debounce_ms"].as<unsigned>() > 2000) {
        error = "io.debounce_ms deve estar entre 0 e 2000"; return false;
      }
      candidate.inputDebounceMs = io["debounce_ms"].as<unsigned>();
    }
    rebootRequired |= candidate.pcfAddress != config_.pcfAddress ||
                      candidate.inputActiveLow != config_.inputActiveLow ||
                      candidate.inputDebounceMs != config_.inputDebounceMs;
  }

  JsonObjectConst hook = root["webhook"];
  if (!hook.isNull()) {
    if (!hook["enabled"].isNull()) candidate.webhookEnabled = hook["enabled"].as<bool>();
    struct TextField { const char *key; char *target; size_t size; bool secret; } fields[] = {
      {"url", candidate.webhookUrl, sizeof(candidate.webhookUrl), false},
      {"token", candidate.webhookToken, sizeof(candidate.webhookToken), true},
      {"ca_cert", candidate.webhookCa, sizeof(candidate.webhookCa), true}
    };
    for (auto &field : fields) {
      if (hook[field.key].isNull()) continue;
      if (!hook[field.key].is<const char *>()) { error = "webhook: texto esperado"; return false; }
      const char *value = hook[field.key].as<const char *>();
      if (field.secret && strcmp(value, "********") == 0) continue;
      if (strlen(value) >= field.size) { error = String("webhook.") + field.key + " excede limite"; return false; }
      copyText(field.target, field.size, value);
    }
    String url(candidate.webhookUrl);
    if (candidate.webhookEnabled && !(url.startsWith("http://") || url.startsWith("https://"))) {
      error = "webhook.url deve iniciar com http:// ou https://"; return false;
    }
    if (candidate.webhookEnabled && url.startsWith("https://") && !candidate.webhookCa[0]) {
      error = "HTTPS exige webhook.ca_cert PEM da autoridade certificadora"; return false;
    }
    if (strchr(candidate.webhookToken, '\r') || strchr(candidate.webhookToken, '\n')) {
      error = "webhook.token contem quebra de linha"; return false;
    }
  }

  JsonObjectConst ap = root["wifi_ap"];
  if (!ap.isNull()) {
    if (!ap["enabled"].isNull()) candidate.apEnabled = ap["enabled"].as<bool>();
    if (!ap["ssid"].isNull()) {
      const char *ssid = ap["ssid"] | "";
      if (!strlen(ssid) || strlen(ssid) > 32) { error = "wifi_ap.ssid deve ter 1-32 bytes"; return false; }
      copyText(candidate.apSsid, sizeof(candidate.apSsid), ssid);
    }
    if (!ap["password"].isNull() && strcmp(ap["password"] | "", "********") != 0) {
      const char *password = ap["password"] | "";
      if (strlen(password) > 63) { error = "wifi_ap.password excede 63 bytes"; return false; }
      copyText(candidate.apPassword, sizeof(candidate.apPassword), password);
    }
    if (candidate.apEnabled && strlen(candidate.apPassword) < 8) {
      error = "Wi-Fi habilitado exige senha de 8-63 bytes"; return false;
    }
    if (!ap["ip"].isNull() && !parseIp(ap["ip"], candidate.apIp)) { error = "wifi_ap.ip invalido"; return false; }
    if (!ap["dns"].isNull() && !parseIp(ap["dns"], candidate.apDns)) { error = "wifi_ap.dns invalido"; return false; }
    if (candidate.apIp[3] != 1 || candidate.apIp[0] == 0 || candidate.apIp[0] >= 224) {
      error = "wifi_ap.ip deve ser endereco unicast terminado em .1 (rede /24)"; return false;
    }
    if (!ap["channel"].isNull()) {
      const unsigned channel = ap["channel"].as<unsigned>();
      if (channel < 1 || channel > 11) { error = "wifi_ap.channel deve ser 1-11"; return false; }
      candidate.apChannel = channel;
    }
    rebootRequired |= candidate.apEnabled != config_.apEnabled ||
      strcmp(candidate.apSsid, config_.apSsid) || strcmp(candidate.apPassword, config_.apPassword) ||
      candidate.apIp != config_.apIp || candidate.apDns != config_.apDns || candidate.apChannel != config_.apChannel;
  }
  if (candidate.mqttEnabled && (!candidate.mqttBaseTopic[0] || strchr(candidate.mqttBaseTopic, '#') ||
      strchr(candidate.mqttBaseTopic, '+'))) {
    error = "mqtt.base_topic deve ser preenchido e nao pode conter # ou +"; return false;
  }
  if (candidate.modbusPort == 80) { error = "modbus.port conflita com HTTP na porta 80"; return false; }
  if (candidate.apEnabled && !candidate.dhcp &&
      (((uint32_t(candidate.ip) & uint32_t(candidate.subnet)) ==
        (uint32_t(candidate.apIp) & uint32_t(candidate.subnet))) ||
       ((uint32_t(candidate.ip) & uint32_t(IPAddress(255,255,255,0))) ==
        (uint32_t(candidate.apIp) & uint32_t(IPAddress(255,255,255,0)))))) {
    error = "a sub-rede Wi-Fi deve ser diferente da Ethernet"; return false;
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

  JsonObject io = root["io"].to<JsonObject>();
  io["address"] = config_.pcfAddress;
  io["input_active_low"] = config_.inputActiveLow;
  io["debounce_ms"] = config_.inputDebounceMs;
  JsonObject hook = root["webhook"].to<JsonObject>();
  hook["enabled"] = config_.webhookEnabled;
  hook["url"] = config_.webhookUrl;
  hook["token"] = includeSecret ? config_.webhookToken : (config_.webhookToken[0] ? "********" : "");
  hook["ca_cert"] = includeSecret ? config_.webhookCa : (config_.webhookCa[0] ? "********" : "");
  JsonObject ap = root["wifi_ap"].to<JsonObject>();
  ap["enabled"] = config_.apEnabled;
  ap["ssid"] = config_.apSsid;
  ap["password"] = includeSecret ? config_.apPassword : (config_.apPassword[0] ? "********" : "");
  ap["ip"] = config_.apIp.toString();
  ap["dns"] = config_.apDns.toString();
  ap["channel"] = config_.apChannel;
}
