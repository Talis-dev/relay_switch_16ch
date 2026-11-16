#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ETH.h>
#include <Network.h>

#include "AppConfig.h"
#include "NetworkServices.h"
#include "RelayController.h"

SET_LOOP_TASK_STACK_SIZE(24576);

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
      Serial.printf("[ETH] IP: %s, link: %u Mbps, %s duplex\n",
                    ETH.localIP().toString().c_str(), ETH.linkSpeed(),
                    ETH.fullDuplex() ? "full" : "half");
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
    relays.shutdown();
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
    relays.setMask(0);
    Serial.printf("[OTA] erro %u\n", error);
  });
  ArduinoOTA.begin();
  Serial.printf("[OTA] pronto: %s.local\n", appConfig.data().deviceName);
}

static void beginEthernet() {
  Network.onEvent(onNetworkEvent);

  // WT32-ETH01 V1.4 / LAN8720: clock externo em GPIO0.
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
  Serial.println("\nRelay Switch Industrial 5CH + 2DI iniciando...");

  appConfig.begin();
  relays.begin(appConfig.data().pcfAddress, appConfig.data().inputActiveLow,
               appConfig.data().inputDebounceMs);
  beginEthernet();
  beginOta();
  services.begin();
  configTime(0, 0, "pool.ntp.org", "time.google.com");
}

void loop() {
  // Todos os servicos de rede ficam neste unico contexto cooperativo.
  ArduinoOTA.handle();
  relays.loop();
  services.loop(ethernetHasIp);
  delay(1);
}
