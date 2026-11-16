#pragma once

#include <Arduino.h>
#include <Wire.h>

class RelayController {
 public:
  static constexpr uint8_t CHANNEL_COUNT = 16;
  static constexpr uint8_t PCF8575_ADDRESS = 0x20;
  static constexpr uint8_t I2C_SDA = 33;
  static constexpr uint8_t I2C_SCL = 32;
  static constexpr uint32_t I2C_FREQUENCY = 100000;
  static constexpr uint16_t I2C_TIMEOUT_MS = 5;
  static constexpr uint8_t MAX_SCAN_RESULTS = 24;

  void begin();
  void loop();
  bool set(uint8_t channel, bool on);
  bool setMask(uint16_t logicalMask);
  bool get(uint8_t channel) const;
  uint16_t mask() const { return logicalMask_; }
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
