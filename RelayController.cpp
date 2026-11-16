#include "RelayController.h"

namespace {
constexpr uint32_t RETRY_INTERVAL_MS = 1000;
}

void RelayController::begin(uint8_t address, bool inputActiveLow, uint16_t debounceMs) {
  // A PCB mantem EN baixo por pull-down mesmo antes de setup()/durante reset.
  digitalWrite(RELAY_ENABLE_GPIO, LOW);
  pinMode(RELAY_ENABLE_GPIO, OUTPUT);
  enableOutputs(false);
  address_ = address;
  inputActiveLow_ = inputActiveLow;
  debounceMs_ = debounceMs;
  Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQUENCY);
  // Um timeout curto impede que
  // curto/falha no barramento prejudique OTA e Ethernet.
  Wire.setTimeOut(I2C_TIMEOUT_MS);
  logicalMask_ = 0;
  pcfHealthy_ = writeHardware();
  Serial.printf("[PCF8574] endereco 0x%02X, reles OFF, status: %s\n",
                address_, pcfHealthy_ ? "OK" : "FALHA");
}

void RelayController::loop() {
  if (scanRunning_) {
    // Um unico endereco por passagem: o scan nunca monopoliza o loop de rede.
    scanOneAddress();
  }
  if (!pcfHealthy_) {
    if (millis() - lastProbeMs_ < RETRY_INTERVAL_MS) return;
    lastProbeMs_ = millis();
    if (!writeHardware()) return;
    Serial.println("[PCF8574] comunicacao restabelecida");
  }
  if (millis() - inputSampleMs_ >= 10) readInputs();
}

bool RelayController::writeHardware() {
  // Placa de rele ativa em LOW: inverte o estado logico antes de enviar.
  // P5/P6 entradas e P7 livre SEMPRE em HIGH (modo quase bidirecional).
  const uint8_t electricalLevels = static_cast<uint8_t>(~logicalMask_) | 0xE0;
  Wire.beginTransmission(address_);
  Wire.write(electricalLevels);
  const uint32_t startedUs = micros();
  lastI2cError_ = Wire.endTransmission();
  lastTransactionDurationUs_ = micros() - startedUs;
  const bool ok = lastI2cError_ == 0;
  pcfHealthy_ = ok;
  // Somente libera a etapa de potencia apos escrita confirmada.
  enableOutputs(ok);
  if (ok) lastSuccessMs_ = millis();
  else {
    sampleSeeded_ = false;
    ++writeErrors_;
    lastFailureMs_ = millis();
  }
  return ok;
}

void RelayController::enableOutputs(bool enabled) {
  digitalWrite(RELAY_ENABLE_GPIO, enabled ? HIGH : LOW);
  outputsEnabled_ = enabled;
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
  if (logicalMask > OUTPUT_MASK) return false;
  logicalMask_ = logicalMask;
  return writeHardware();
}

void RelayController::readInputs() {
  const uint32_t now = millis();
  inputSampleMs_ = now;
  const size_t received = Wire.requestFrom(address_, static_cast<uint8_t>(1));
  if (received != 1) {
    enableOutputs(false);
    pcfHealthy_ = false;
    sampleSeeded_ = false;
    lastI2cError_ = 4;
    lastFailureMs_ = now;
    return;
  }
  const uint8_t electrical = (static_cast<uint8_t>(Wire.read()) >> 5) & 3;
  const uint8_t logical = inputActiveLow_ ? (electrical ^ 3) : electrical;
  lastI2cError_ = 0;
  lastSuccessMs_ = now;
  if (!sampleSeeded_) {
    rawMask_ = logical;
    for (uint8_t i = 0; i < INPUT_COUNT; ++i) rawSinceMs_[i] = now;
    sampleSeeded_ = true;
    // O primeiro estado valido e uma base, nao uma borda de entrada.
    if (!inputsValid_) { inputMask_ = logical; inputsValid_ = true; }
    return;
  }
  for (uint8_t i = 0; i < INPUT_COUNT; ++i) {
    const uint8_t bit = 1U << i;
    if ((logical & bit) != (rawMask_ & bit)) {
      rawMask_ ^= bit;
      rawSinceMs_[i] = now;
    }
    if ((logical & bit) != (inputMask_ & bit) && now - rawSinceMs_[i] >= debounceMs_) {
      inputMask_ ^= bit;
      pendingChanges_ |= bit;
      changedMs_[i] = now;
      ++inputSequence_;
    }
  }
}

bool RelayController::get(uint8_t channel) const {
  if (channel < 1 || channel > CHANNEL_COUNT) return false;
  return (logicalMask_ & (1U << (channel - 1))) != 0;
}
