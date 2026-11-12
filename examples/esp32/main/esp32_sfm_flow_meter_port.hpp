/**
 * @file esp32_sfm_flow_meter_port.hpp
 * @brief ESP-IDF port for the SFM flow meter driver: i2c_master for the bus, GPIO for the optional
 * reset and power pins, esp_rom delays, esp_timer for the clock and ESP_LOG for the log hook.
 * @copyright Copyright (c) 2026 The driver-sfm-flow-meter authors. SPDX-License-Identifier: MIT
 *
 * Start from this file for any ESP-IDF target. The pins are in Esp32SfmFlowMeterPins; the defaults
 * match the wiring table in examples/esp32/README.md. The port implements every optional hook, so
 * it works with every family, including the clock-stretching read of the SFM4100. It has been
 * compiled only; it has not run against a sensor (see docs/contract.md).
 */
#ifndef SFM_FLOW_METER_ESP32_SFM_FLOW_METER_PORT_HPP
#define SFM_FLOW_METER_ESP32_SFM_FLOW_METER_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "sfm_flow_meter.hpp"

/// The port's default SCL clock: the lowest "maximum" of the supported parts (the SFM4200 and
/// SFM4100).
inline constexpr uint32_t kEsp32SfmFlowMeterClockHz = sfm::kSafeClockHz;
static_assert(kEsp32SfmFlowMeterClockHz <= sfm::kMaxClockHz, "SCL above the family's maximum");

/// Pins and bus settings of the ESP-IDF port.
struct Esp32SfmFlowMeterPins {
  i2c_port_num_t port = I2C_NUM_0;
  int sda = 6;
  int scl = 7;
  int reset_n = -1;      ///< /RES of the SFM4200; -1 when the pin is not wired.
  int power_enable = -1; ///< A load switch on the sensor supply; -1 when not wired.
  uint32_t clock_hz =
      kEsp32SfmFlowMeterClockHz;   ///< Up to `sfm::kMaxClockHz`; 100 kHz for the legacy parts.
  int timeout_ms = 10;             ///< Per transaction.
  int stretch_timeout_ms = 50;     ///< Per transaction of the clock-stretching read.
  uint32_t power_off_us = 100'000; ///< How long `PowerCycle()` keeps the supply off.
  bool internal_pullup =
      false; ///< The weak internal pull-ups: a bench aid only, see docs/porting.md.
};

/// The SFM flow meter I2C port on ESP-IDF, with every optional hook.
class Esp32SfmFlowMeterPort : public sfm::I2cInterface<Esp32SfmFlowMeterPort> {
public:
  /// Ask the SFM4100 hook of the driver for the stretching read.
  static constexpr bool kClockStretch = true;

  explicit Esp32SfmFlowMeterPort(const Esp32SfmFlowMeterPins& pins = {}) noexcept : pins_(pins) {}

  /// Whether the configured SCL clock is within the limit of `part`; check it before the first
  /// call.
  [[nodiscard]] bool ClockAllowed(const sfm::PartInfo& part) const noexcept {
    return pins_.clock_hz <= part.max_clock_hz;
  }

  bool EnsureInitialized() noexcept {
    if (bus_ != nullptr) {
      if (needs_clear_) {
        // A transfer timed out: free a slave that may hold SDA low before the next start-up.
        needs_clear_ = false;
        return i2c_master_bus_reset(bus_) == ESP_OK;
      }
      return true;
    }
    // Also the way out of a held bus: a reset that failed between its two steps leaves no bus
    // handle, and creating the bus takes SDA and SCL back from the pins that held them low.
    i2c_master_bus_config_t bus{};
    bus.i2c_port = pins_.port;
    bus.sda_io_num = static_cast<gpio_num_t>(pins_.sda);
    bus.scl_io_num = static_cast<gpio_num_t>(pins_.scl);
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = pins_.internal_pullup;
    if (pins_.clock_hz > sfm::kMaxClockHz) {
      return false; // above what any part takes; the part's own limit is checked by ClockAllowed()
    }
    if (i2c_new_master_bus(&bus, &bus_) != ESP_OK) {
      bus_ = nullptr;
      return false;
    }
    // A pin is driven to its inactive level before its output is enabled, so configuring it never
    // pulses it.
    return outputOk(pins_.reset_n, 1) && outputOk(pins_.power_enable, 1);
  }

  /// A write of `len` bytes (at least 1: the driver never sends an empty transfer). A NACK is
  /// `false`; while a sensor sleeps or resets that is expected.
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    i2c_master_dev_handle_t dev = device(addr);
    return (len != 0U) && (dev != nullptr) &&
           done(i2c_master_transmit(dev, data, len, pins_.timeout_ms));
  }

  /// A read of `len` bytes: every byte ACKed but the last. A NACKed address is "no data yet".
  bool Read(uint8_t addr, uint8_t* data, std::size_t len) noexcept {
    i2c_master_dev_handle_t dev = device(addr);
    return (dev != nullptr) && done(i2c_master_receive(dev, data, len, pins_.timeout_ms));
  }

  /// The read of the SFM4100 ("hold master"): the sensor may hold SCL low, so the timeout is
  /// longer.
  bool ReadStretch(uint8_t addr, uint8_t* data, std::size_t len) noexcept {
    i2c_master_dev_handle_t dev = device(addr);
    return (dev != nullptr) && done(i2c_master_receive(dev, data, len, pins_.stretch_timeout_ms));
  }

  /// START, address 0x00 + W, one byte, STOP. Every device on the bus that listens obeys it.
  bool WriteGeneralCall(uint8_t byte) noexcept {
    i2c_master_dev_handle_t dev = device(0x00U);
    return (dev != nullptr) && done(i2c_master_transmit(dev, &byte, 1U, pins_.timeout_ms));
  }

  /// Remove and restore the sensor supply through `power_enable`. SDA and SCL are held low while
  /// the supply is off, so the sensor is not powered through the bus lines (SFM4200 2.3, SFM3505
  /// 3.3.5); the bus is set up again afterwards.
  bool PowerCycle() noexcept {
    if (pins_.power_enable < 0) {
      return false;
    }
    bool ok = holdBus();
    ok = ok && (gpio_set_level(static_cast<gpio_num_t>(pins_.power_enable), 0) == ESP_OK);
    esp_rom_delay_us(pins_.power_off_us);
    ok = ok && (gpio_set_level(static_cast<gpio_num_t>(pins_.power_enable), 1) == ESP_OK);
    return releaseBus() && ok;
  }

  /// Drive the /RES pin of the SFM4200 (active low). SDA and SCL are held low for as long as it is
  /// asserted (SFM4200 2.3).
  bool SetResetPin(bool asserted) noexcept {
    if (pins_.reset_n < 0) {
      return false;
    }
    if (asserted) {
      return holdBus() && (gpio_set_level(static_cast<gpio_num_t>(pins_.reset_n), 0) == ESP_OK);
    }
    const bool released = gpio_set_level(static_cast<gpio_num_t>(pins_.reset_n), 1) == ESP_OK;
    return releaseBus() && released;
  }

  void DelayUs(uint32_t us) noexcept {
    esp_rom_delay_us(us);
  }

  uint32_t NowUs() noexcept {
    return static_cast<uint32_t>(esp_timer_get_time());
  }

  void Log(sfm::LogLevel level, const char* message) noexcept {
    esp_log_level_t esp_level = ESP_LOG_DEBUG;
    if (level == sfm::LogLevel::Error) {
      esp_level = ESP_LOG_ERROR;
    } else if (level == sfm::LogLevel::Warning) {
      esp_level = ESP_LOG_WARN;
    } else if (level == sfm::LogLevel::Info) {
      esp_level = ESP_LOG_INFO;
    } else {
      // Debug: the default.
    }
    esp_log_write(esp_level, kTag, "%s\n", message); // plain text, never a format
  }

private:
  // A transfer result: a timeout asks for a bus clear before the next start-up.
  bool done(esp_err_t result) noexcept {
    needs_clear_ = needs_clear_ || (result == ESP_ERR_TIMEOUT);
    return result == ESP_OK;
  }

  // Give the bus pins up and drive SDA and SCL low (level first, then the output), for a reset or a
  // supply cycle.
  bool holdBus() noexcept {
    if (device_ != nullptr) {
      (void)i2c_master_bus_rm_device(device_);
      device_ = nullptr;
    }
    if (bus_ != nullptr) {
      (void)i2c_del_master_bus(bus_);
      bus_ = nullptr;
    }
    return lowOutput(pins_.sda) && lowOutput(pins_.scl);
  }

  // Hand the pins back and set the bus up again.
  bool releaseBus() noexcept {
    (void)gpio_reset_pin(static_cast<gpio_num_t>(pins_.sda));
    (void)gpio_reset_pin(static_cast<gpio_num_t>(pins_.scl));
    return EnsureInitialized();
  }

  static bool lowOutput(int pin) noexcept {
    gpio_config_t io{};
    io.pin_bit_mask = 1ULL << static_cast<unsigned>(pin);
    io.mode = GPIO_MODE_OUTPUT;
    return (gpio_set_level(static_cast<gpio_num_t>(pin), 0U) == ESP_OK) &&
           (gpio_config(&io) == ESP_OK);
  }

  // Drive an optional output pin to `level` and make it an output; true when the pin is not wired.
  static bool outputOk(int pin, uint32_t level) noexcept {
    if (pin < 0) {
      return true;
    }
    gpio_config_t io{};
    io.pin_bit_mask = 1ULL << static_cast<unsigned>(pin);
    io.mode = GPIO_MODE_OUTPUT;
    return (gpio_set_level(static_cast<gpio_num_t>(pin), level) == ESP_OK) &&
           (gpio_config(&io) == ESP_OK);
  }

  // The device handle for one 7-bit address, created on first use and replaced when the address
  // changes.
  i2c_master_dev_handle_t device(uint8_t addr) noexcept {
    if (bus_ == nullptr) {
      return nullptr;
    }
    if ((device_ != nullptr) && (device_addr_ == addr)) {
      return device_;
    }
    if (device_ != nullptr) {
      (void)i2c_master_bus_rm_device(device_);
      device_ = nullptr;
    }
    i2c_device_config_t cfg{};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = pins_.clock_hz;
    if (i2c_master_bus_add_device(bus_, &cfg, &device_) != ESP_OK) {
      device_ = nullptr;
      return nullptr;
    }
    device_addr_ = addr;
    return device_;
  }

  static constexpr const char* kTag = "sfm_flow_meter";
  Esp32SfmFlowMeterPins pins_;
  i2c_master_bus_handle_t bus_ = nullptr;
  i2c_master_dev_handle_t device_ = nullptr;
  uint8_t device_addr_ = 0;
  bool needs_clear_ = false;
};

#endif // SFM_FLOW_METER_ESP32_SFM_FLOW_METER_PORT_HPP
