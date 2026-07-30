#pragma once

#include <Arduino.h>
#include <Wire.h>

class RelayController {
 public:
  static constexpr uint8_t CHANNEL_COUNT = 16;
  static constexpr uint8_t PCF8575_ADDRESS = 0x20;

  void begin();
  void loop();
  bool set(uint8_t channel, bool on);
  bool setMask(uint16_t logicalMask);
  bool get(uint8_t channel) const;
  uint16_t mask() const { return logicalMask_; }
  bool pcfHealthy() const { return pcfHealthy_; }
  uint32_t writeErrors() const { return writeErrors_; }

 private:
  uint16_t logicalMask_ = 0;  // bit 1 = rele energizado
  bool pcfHealthy_ = false;
  uint32_t writeErrors_ = 0;
  uint32_t lastProbeMs_ = 0;

  bool writeHardware();
};
