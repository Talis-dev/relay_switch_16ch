#include "InputWebhook.h"

#include <ETH.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_system.h>
#include <time.h>

void InputWebhook::begin() {
  boot_ = esp_random();
  queue_ = xQueueCreate(8, sizeof(Job));
  if (!queue_) return;
  if (xTaskCreate(task, "input-webhook", 10240, this, 1, nullptr) != pdPASS) {
    vQueueDelete(queue_);
    queue_ = nullptr;
  }
}

void InputWebhook::enqueue(const DeviceConfig &config, uint8_t channel, bool active,
                           uint8_t mask, uint32_t sequence, uint32_t changedMs) {
  if (!config.webhookEnabled) return;
  Job job{};
  strlcpy(job.url, config.webhookUrl, sizeof(job.url));
  strlcpy(job.token, config.webhookToken, sizeof(job.token));
  strlcpy(job.ca, config.webhookCa, sizeof(job.ca));
  strlcpy(job.device, config.deviceName, sizeof(job.device));
  job.boot = boot_;
  job.sequence = sequence;
  job.changedMs = changedMs;
  job.channel = channel;
  job.mask = mask;
  job.active = active;
  if (!queue_ || xQueueSend(queue_, &job, 0) != pdTRUE) {
    portENTER_CRITICAL(&mux_);
    ++dropped_;
    portEXIT_CRITICAL(&mux_);
  }
}

void InputWebhook::task(void *context) { static_cast<InputWebhook *>(context)->run(); }

void InputWebhook::run() {
  Job job;
  for (;;) {
    if (xQueueReceive(queue_, &job, portMAX_DELAY) != pdTRUE) continue;
    JsonDocument doc;
    doc["event"] = "input.changed";
    doc["device_name"] = job.device;
    const String eventId = String(job.device) + "-" + String(job.boot, HEX) + "-" +
                           job.sequence + "-" + job.channel;
    doc["event_id"] = eventId;
    doc["sequence"] = job.sequence;
    doc["channel"] = job.channel;
    doc["active"] = job.active;
    doc["mask"] = job.mask;
    doc["changed_ms"] = job.changedMs;
    String payload;
    serializeJson(doc, payload);
    int status = 0;
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
      status = -1000; // Ethernet indisponivel.
      if (ETH.hasIP()) {
        HTTPClient http;
        NetworkClient plain;
        NetworkClientSecure secure;
        const bool tls = String(job.url).startsWith("https://");
        // A validacao TLS exige CA e horario sincronizado. Nunca setInsecure().
        if (tls) {
          secure.setCACert(job.ca);
          secure.setHandshakeTimeout(3);
        }
        NetworkClient &client = tls ? static_cast<NetworkClient &>(secure) : plain;
        http.setConnectTimeout(1500);
        http.setTimeout(2000);
        http.setReuse(false);
        if (tls && time(nullptr) < 1700000000) status = -1001;
        else if (http.begin(client, job.url)) {
          http.addHeader("Content-Type", "application/json");
          http.addHeader("X-Event-ID", eventId);
          if (job.token[0]) http.addHeader("Authorization", String("Bearer ") + job.token);
          status = http.POST(payload);
          http.end();
        } else status = -1002;
      }
      if (status >= 200 && status < 300) break;
      // 4xx sao terminais, salvo timeout/rate limit.
      if (status >= 400 && status < 500 && status != 408 && status != 429) break;
      if (attempt < 2) vTaskDelay(pdMS_TO_TICKS(1000U << attempt));
    }
    portENTER_CRITICAL(&mux_);
    lastStatus_ = status;
    if (status >= 200 && status < 300) ++delivered_;
    else ++failed_;
    portEXIT_CRITICAL(&mux_);
  }
}

void InputWebhook::toJson(JsonObject root) {
  uint32_t delivered, failed, dropped;
  int status;
  portENTER_CRITICAL(&mux_);
  delivered = delivered_; failed = failed_; dropped = dropped_; status = lastStatus_;
  portEXIT_CRITICAL(&mux_);
  root["worker_ready"] = queue_ != nullptr;
  root["pending"] = queue_ ? uxQueueMessagesWaiting(queue_) : 0;
  root["delivered"] = delivered;
  root["failed"] = failed;
  root["dropped"] = dropped;
  root["last_http_status"] = status;
}
