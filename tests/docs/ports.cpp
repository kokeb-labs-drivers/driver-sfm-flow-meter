/**
 * @file ports.cpp
 * @brief Port snippets of the Porting page: a port with every optional hook, and the reset hooks.
 */
#include <cstddef>
#include <cstdint>

#include "my_port.hpp"

namespace board {
bool I2cReadStretching(uint8_t addr, uint8_t* data, std::size_t len) noexcept;
bool I2cGeneralCall(uint8_t byte) noexcept;
void SupplyOff() noexcept;
void SupplyOn() noexcept;
void ResetLine(bool low) noexcept;
void BusLow() noexcept;     // drive SDA and SCL low (open drain to ground)
void BusRelease() noexcept; // let the pull-ups raise SDA and SCL again
void Print(const char* text) noexcept;
} // namespace board

// --8<-- [start:full-port]
class FullPort : public sfm::I2cInterface<FullPort> {
public:
  static constexpr bool kClockStretch = true; // marks ReadStretch() for the SFM4100

  bool EnsureInitialized() noexcept {
    return board::I2cBegin();
  }
  bool Write(uint8_t a, const uint8_t* d, std::size_t n) noexcept {
    return board::I2cWrite(a, d, n);
  }
  bool Read(uint8_t a, uint8_t* d, std::size_t n) noexcept {
    return board::I2cRead(a, d, n);
  }
  bool ReadStretch(uint8_t a, uint8_t* d, std::size_t n) noexcept {
    return board::I2cReadStretching(a, d, n);
  }
  bool WriteGeneralCall(uint8_t byte) noexcept {
    return board::I2cGeneralCall(byte);
  }
  void DelayUs(uint32_t us) noexcept {
    board::SleepUs(us);
  }
  uint32_t NowUs() noexcept {
    return board::MicrosNow();
  }
  void Log(sfm::LogLevel /*level*/, const char* text) noexcept {
    board::Print(text);
  }
};
// --8<-- [end:full-port]

// --8<-- [start:hardware-reset]
class ResettablePort : public sfm::I2cInterface<ResettablePort> {
public:
  bool EnsureInitialized() noexcept {
    return board::I2cBegin();
  }
  bool Write(uint8_t a, const uint8_t* d, std::size_t n) noexcept {
    return board::I2cWrite(a, d, n);
  }
  bool Read(uint8_t a, uint8_t* d, std::size_t n) noexcept {
    return board::I2cRead(a, d, n);
  }
  void DelayUs(uint32_t us) noexcept {
    board::SleepUs(us);
  }

  // Both hooks hold SDA and SCL low while the sensor is without supply or in reset, so it is not
  // powered through the bus pins and really resets (SFM4200 2.3, SFM3505 3.3.5).
  bool PowerCycle() noexcept { // the legacy parts' last resort: remove and restore the supply
    board::BusLow();
    board::SupplyOff();
    board::SleepUs(100'000U);
    board::SupplyOn();
    board::BusRelease();
    return true;
  }
  bool SetResetPin(bool asserted) noexcept { // the /RES pin of the SFM4200, active low
    if (asserted) {
      board::BusLow(); // at the same time as /RES, not after it
    }
    board::ResetLine(asserted);
    if (!asserted) {
      board::BusRelease();
    }
    return true;
  }
};
// --8<-- [end:hardware-reset]

// The ports are checked at build time: a wrong signature fails here, naming the hook.
static_assert(sfm::I2cPort<MyPort>);
static_assert(sfm::I2cPort<FullPort>);
static_assert(sfm::I2cPort<ResettablePort>);

bool FullPortCompiles(FullPort& port) {
  sfm::SfmFlowMeter<FullPort, sfm::kFamilySf04> meter(port,
                                                      sfm::PresetFor(sfm::Variant::SFM4100_AIR));
  return meter.GetState() == sfm::State::Uninitialized;
}

bool ResettablePortCompiles(ResettablePort& port) {
  sfm::SfmFlowMeter<ResettablePort, sfm::kFamilySf05> meter(port,
                                                            sfm::PresetFor(sfm::Variant::SFM4200));
  return meter.HardReset().ok();
}
