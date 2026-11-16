#include "WifiGateway.h"

void WifiGateway::begin(const DeviceConfig &cfg) {
  if (!cfg.apEnabled) return;
  IPAddress lease = cfg.apIp;
  lease[3] = 2;
  started_ = WiFi.AP.begin() &&
    WiFi.AP.config(cfg.apIp, cfg.apIp, IPAddress(255, 255, 255, 0), lease, cfg.apDns) &&
    WiFi.AP.create(cfg.apSsid, cfg.apPassword, cfg.apChannel, 0, 2);
  if (!started_) WiFi.AP.end();
  Serial.printf("[WiFi AP] %s\n", started_ ? "iniciado" : "FALHA");
}

void WifiGateway::loop(bool ethernetHasIp) {
  if (!started_) return;
  const uint32_t ethMask = uint32_t(ETH.subnetMask());
  const uint32_t apMask = uint32_t(IPAddress(255, 255, 255, 0));
  const uint32_t ethIp = uint32_t(ETH.localIP());
  const uint32_t apIp = uint32_t(WiFi.AP.localIP());
  conflict_ = ethernetHasIp && (((ethIp & ethMask) == (apIp & ethMask)) ||
                                ((ethIp & apMask) == (apIp & apMask)));
  const bool desired = ethernetHasIp && !conflict_;
  if (desired == nat_) return;
  if (desired && millis() - lastAttemptMs_ < 1000) return;
  lastAttemptMs_ = millis();
  if (desired) ETH.setDefault();
  if (WiFi.AP.enableNAPT(desired)) nat_ = desired;
}

void WifiGateway::toJson(JsonObject root) {
  root["started"] = started_;
  root["nat_enabled"] = nat_;
  root["subnet_conflict"] = conflict_;
  root["ip"] = started_ ? WiFi.AP.localIP().toString() : String("");
  root["clients"] = started_ ? WiFi.AP.stationCount() : 0;
}
