#pragma once

#include <Arduino.h>
#include <Wire.h>

class RelayController {
 public:
  static constexpr uint8_t CHANNEL_COUNT = 5;
  static constexpr uint8_t INPUT_COUNT = 2;
  static constexpr uint8_t OUTPUT_MASK = 0x1F;
  static constexpr uint8_t I2C_SDA = 33;
  static constexpr uint8_t I2C_SCL = 32;
  // EN fisico ativo em HIGH. Pull-down externo de 10k obrigatorio na PCB.
  static constexpr uint8_t RELAY_ENABLE_GPIO = 4;
  static constexpr uint32_t I2C_FREQUENCY = 100000;
  static constexpr uint16_t I2C_TIMEOUT_MS = 5;
  static constexpr uint8_t MAX_SCAN_RESULTS = 24;

  void begin(uint8_t address = 0x20, bool inputActiveLow = true, uint16_t debounceMs = 50);
  void loop();
  void shutdown() { enableOutputs(false); logicalMask_ = 0; }
  bool set(uint8_t channel, bool on);
  bool setMask(uint16_t logicalMask);
  bool get(uint8_t channel) const;
  uint16_t mask() const { return logicalMask_; }
  uint8_t address() const { return address_; }
  bool outputsEnabled() const { return outputsEnabled_; }
  bool inputsValid() const { return inputsValid_ && sampleSeeded_ && pcfHealthy_; }
  bool input(uint8_t channel) const { return channel >= 1 && channel <= INPUT_COUNT && (inputMask_ & (1U << (channel - 1))); }
  uint8_t inputMask() const { return inputMask_; }
  uint32_t inputSequence() const { return inputSequence_; }
  uint32_t inputSampleMs() const { return inputSampleMs_; }
  uint32_t inputChangedMs(uint8_t channel) const { return channel >= 1 && channel <= INPUT_COUNT ? changedMs_[channel - 1] : 0; }
  uint8_t takeInputChanges() { uint8_t changes = pendingChanges_; pendingChanges_ = 0; return changes; }
  bool pcfHealthy() const { return pcfHealthy_; }
  uint32_t writeErrors() const { return writeErrors_; }
  uint8_t lastI2cError() const { return lastI2cError_; }
  const char *lastI2cErrorText() const;
  uint32_t lastTransactionDurationUs() const { return lastTransactionDurationUs_; }
  uint32_t lastSuccessMs() const { return lastSuccessMs_; }
  uint32_t lastFailureMs() const { return lastFailureMs_; }
  uint8_t sdaLevel() const { return digitalRead(I2C_SDA); }
  uint8_t sclLevel() const { return digitalRead(I2C_SCL); }

  bool requestI2cScan();
  bool scanRunning() const { return scanRunning_; }
  bool scanCompleted() const { return scanCompleted_; }
  uint8_t scanCurrentAddress() const { return scanCurrentAddress_; }
  uint8_t scanFoundCount() const { return scanFoundCount_; }
  uint8_t scanFoundAddress(uint8_t index) const;
  bool scanResultsTruncated() const { return scanResultsTruncated_; }
  uint32_t scanDurationMs() const { return scanDurationMs_; }
  uint16_t scanNackCount() const { return scanNackCount_; }
  uint16_t scanTimeoutCount() const { return scanTimeoutCount_; }
  uint16_t scanOtherErrorCount() const { return scanOtherErrorCount_; }
  static const char *i2cErrorText(uint8_t error);

 private:
  uint8_t address_ = 0x20;
  bool outputsEnabled_ = false;
  void enableOutputs(bool enabled);
  bool inputActiveLow_ = true;
  uint16_t debounceMs_ = 50;
  uint8_t inputMask_ = 0;
  uint8_t rawMask_ = 0;
  uint8_t pendingChanges_ = 0;
  bool inputsValid_ = false;
  bool sampleSeeded_ = false;
  uint32_t inputSequence_ = 0;
  uint32_t inputSampleMs_ = 0;
  uint32_t rawSinceMs_[INPUT_COUNT] = {};
  uint32_t changedMs_[INPUT_COUNT] = {};
  void readInputs();
  uint16_t logicalMask_ = 0;  // bit 1 = rele energizado
  bool pcfHealthy_ = false;
  uint32_t writeErrors_ = 0;
  uint32_t lastProbeMs_ = 0;
  uint8_t lastI2cError_ = 4;
  uint32_t lastTransactionDurationUs_ = 0;
  uint32_t lastSuccessMs_ = 0;
  uint32_t lastFailureMs_ = 0;

  bool scanRunning_ = false;
  bool scanCompleted_ = false;
  bool scanResultsTruncated_ = false;
  uint8_t scanCurrentAddress_ = 0;
  uint8_t scanFoundCount_ = 0;
  uint8_t scanFoundAddresses_[MAX_SCAN_RESULTS] = {};
  uint32_t scanStartedMs_ = 0;
  uint32_t scanDurationMs_ = 0;
  uint16_t scanNackCount_ = 0;
  uint16_t scanTimeoutCount_ = 0;
  uint16_t scanOtherErrorCount_ = 0;

  bool writeHardware();
  void scanOneAddress();
};
