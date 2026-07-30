#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ETH.h>
#include <Network.h>

#include "AppConfig.h"
#include "NetworkServices.h"
#include "RelayController.h"

AppConfig appConfig;
RelayController relays;
NetworkServices services(appConfig, relays);

static volatile bool ethernetHasIp = false;

// Executado pela tarefa interna de eventos do ESP32: mantenha esta funcao curta.
void onNetworkEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      ETH.setHostname(appConfig.data().deviceName);
      Serial.println("[ETH] interface iniciada");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("[ETH] cabo conectado");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      ethernetHasIp = true;
      Serial.printf("[ETH] IP: %s\n", ETH.localIP().toString().c_str());
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
      ethernetHasIp = false;
      Serial.println("[ETH] IP perdido");
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      ethernetHasIp = false;
      Serial.println("[ETH] cabo desconectado");
      break;
    case ARDUINO_EVENT_ETH_STOP:
      ethernetHasIp = false;
      Serial.println("[ETH] interface parada");
      break;
    default:
      break;
  }
}

static void beginOta() {
  ArduinoOTA.setHostname(appConfig.data().deviceName);
  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] iniciando atualizacao via Ethernet");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] atualizacao concluida");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    const unsigned int percent = total ? (progress * 100U) / total : 0;
    Serial.printf("[OTA] %u%%\r", percent);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] erro %u\n", error);
  });
  ArduinoOTA.begin();
  Serial.printf("[OTA] pronto: %s.local\n", appConfig.data().deviceName);
}

static void beginEthernet() {
  Network.onEvent(onNetworkEvent);

  // WT32-ETH01 V4.1 / LAN8720. Estes pinos vieram do sketch validado em bancada.
  const bool started = ETH.begin(
    ETH_PHY_LAN8720, 1, 23, 18, 16, ETH_CLOCK_GPIO0_IN
  );
  if (!started) {
    Serial.println("[ETH] falha ao iniciar PHY LAN8720");
    return;
  }

  const DeviceConfig &cfg = appConfig.data();
  if (!cfg.dhcp) {
    if (!ETH.config(cfg.ip, cfg.gateway, cfg.subnet, cfg.dns1, cfg.dns2)) {
      Serial.println("[ETH] falha ao aplicar IP estatico");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\nRelay Switch Industrial 16CH iniciando...");

  // Primeira acao de hardware: escreve HIGH nas 16 saidas (reles desligados).
  relays.begin();
  appConfig.begin();
  beginEthernet();
  beginOta();
  services.begin();
}

void loop() {
  // Todos os servicos de rede ficam neste unico contexto cooperativo.
  ArduinoOTA.handle();
  relays.loop();
  services.loop(ethernetHasIp);
  delay(1);
}
