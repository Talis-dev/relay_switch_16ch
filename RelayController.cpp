#include "RelayController.h"

namespace {
constexpr uint32_t RETRY_INTERVAL_MS = 1000;
}

void RelayController::begin() {
  Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQUENCY);
  // O PCF8575 nao faz clock stretching demorado. Um timeout curto impede que
  // curto/falha no barramento prejudique OTA e Ethernet.
  Wire.setTimeOut(I2C_TIMEOUT_MS);
  logicalMask_ = 0;
  pcfHealthy_ = writeHardware();
  Serial.printf("[PCF8575] endereco 0x%02X, reles OFF, status: %s\n",
                PCF8575_ADDRESS, pcfHealthy_ ? "OK" : "FALHA");
}

void RelayController::loop() {
  if (scanRunning_) {
    // Um unico endereco por passagem: o scan nunca monopoliza o loop de rede.
    scanOneAddress();
    return;
  }
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
  const uint32_t startedUs = micros();
  lastI2cError_ = Wire.endTransmission();
  lastTransactionDurationUs_ = micros() - startedUs;
  const bool ok = lastI2cError_ == 0;
  pcfHealthy_ = ok;
  if (ok) lastSuccessMs_ = millis();
  else {
    ++writeErrors_;
    lastFailureMs_ = millis();
  }
  return ok;
}

const char *RelayController::i2cErrorText(uint8_t error) {
  switch (error) {
    case 0: return "ok";
    case 1: return "buffer de transmissao excedido";
    case 2: return "NACK no endereco";
    case 3: return "NACK nos dados";
    case 4: return "erro I2C nao especificado";
    case 5: return "timeout do barramento";
    default: return "codigo I2C desconhecido";
  }
}

const char *RelayController::lastI2cErrorText() const {
  return i2cErrorText(lastI2cError_);
}

bool RelayController::requestI2cScan() {
  if (scanRunning_) return false;
  scanRunning_ = true;
  scanCompleted_ = false;
  scanResultsTruncated_ = false;
  scanCurrentAddress_ = 1;
  scanFoundCount_ = 0;
  scanNackCount_ = 0;
  scanTimeoutCount_ = 0;
  scanOtherErrorCount_ = 0;
  scanStartedMs_ = millis();
  scanDurationMs_ = 0;
  return true;
}

uint8_t RelayController::scanFoundAddress(uint8_t index) const {
  return index < scanFoundCount_ ? scanFoundAddresses_[index] : 0;
}

void RelayController::scanOneAddress() {
  if (!scanRunning_) return;

  Wire.beginTransmission(scanCurrentAddress_);
  const uint8_t error = Wire.endTransmission();
  if (error == 0) {
    if (scanFoundCount_ < MAX_SCAN_RESULTS) {
      scanFoundAddresses_[scanFoundCount_++] = scanCurrentAddress_;
    } else {
      scanResultsTruncated_ = true;
    }
  } else if (error == 2) ++scanNackCount_;
  else if (error == 5) ++scanTimeoutCount_;
  else ++scanOtherErrorCount_;

  if (scanCurrentAddress_ < 126) {
    ++scanCurrentAddress_;
    return;
  }

  scanRunning_ = false;
  scanCompleted_ = true;
  scanDurationMs_ = millis() - scanStartedMs_;
  // Confirma o dispositivo esperado escrevendo novamente o estado seguro/atual.
  writeHardware();
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
