#include "RelayController.h"

namespace {
constexpr uint8_t I2C_SDA = 33;
constexpr uint8_t I2C_SCL = 32;
constexpr uint32_t I2C_FREQUENCY = 100000;
constexpr uint32_t RETRY_INTERVAL_MS = 1000;
}

void RelayController::begin() {
  Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQUENCY);
  Wire.setTimeOut(20);
  logicalMask_ = 0;
  pcfHealthy_ = writeHardware();
  Serial.printf("[PCF8575] endereco 0x%02X, reles OFF, status: %s\n",
                PCF8575_ADDRESS, pcfHealthy_ ? "OK" : "FALHA");
}

void RelayController::loop() {
  if (pcfHealthy_ || millis() - lastProbeMs_ < RETRY_INTERVAL_MS) return;
  lastProbeMs_ = millis();
  pcfHealthy_ = writeHardware();
  if (pcfHealthy_) Serial.println("[PCF8575] comunicacao restabelecida");
}

bool RelayController::writeHardware() {
  // Placa de rele ativa em LOW: inverte o estado logico antes de enviar.
  const uint16_t electricalLevels = static_cast<uint16_t>(~logicalMask_);
  Wire.beginTransmission(PCF8575_ADDRESS);
  Wire.write(static_cast<uint8_t>(electricalLevels & 0xFF));
  Wire.write(static_cast<uint8_t>((electricalLevels >> 8) & 0xFF));
  const bool ok = Wire.endTransmission() == 0;
  pcfHealthy_ = ok;
  if (!ok) ++writeErrors_;
  return ok;
}

bool RelayController::set(uint8_t channel, bool on) {
  if (channel < 1 || channel > CHANNEL_COUNT) return false;
  const uint16_t bit = static_cast<uint16_t>(1U << (channel - 1));
  if (on) logicalMask_ |= bit;
  else logicalMask_ &= static_cast<uint16_t>(~bit);
  return writeHardware();
}

bool RelayController::setMask(uint16_t logicalMask) {
  logicalMask_ = logicalMask;
  return writeHardware();
}

bool RelayController::get(uint8_t channel) const {
  if (channel < 1 || channel > CHANNEL_COUNT) return false;
  return (logicalMask_ & (1U << (channel - 1))) != 0;
}
