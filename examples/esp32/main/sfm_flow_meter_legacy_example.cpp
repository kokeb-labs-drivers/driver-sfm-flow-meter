/**
 * @file sfm_flow_meter_legacy_example.cpp
 * @brief An SFM3400-AW (5 V, legacy command set): flow and temperature, and the recovery the family
 * needs.
 * @copyright Copyright (c) 2026 The driver-sfm-flow-meter authors. SPDX-License-Identifier: MIT
 *
 * The legacy parts have no status word and no soft reset command you can rely on. The driver counts
 * errors in a row and, at the limit, reports `StateUnknown`; this loop then calls `HardReset()`
 * (the port's `PowerCycle()` through the `power_enable` pin, with SDA and SCL held low while the
 * supply is off) and initialises again. A read never power cycles on its own.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp32_sfm_flow_meter_port.hpp"
#include "sfm_flow_meter.hpp"

namespace {
constexpr const char* kTag = "sfm_flow_meter_legacy";

using Meter = sfm::SfmFlowMeter<Esp32SfmFlowMeterPort, sfm::kFamilySf05>;

/// The legacy parts run on 5 V and 100 kHz; the pins of the supply switch are in the port.
constexpr Esp32SfmFlowMeterPins Pins() noexcept {
  Esp32SfmFlowMeterPins pins;
  pins.power_enable = 10;
  return pins;
}
static_assert(Pins().clock_hz <= sfm::FindPart(sfm::Variant::SFM3400_AW)->max_clock_hz,
              "SCL above the part's limit");
} // namespace

extern "C" void app_main(void) {
  static Esp32SfmFlowMeterPort port(Pins());
  static Meter meter(port, sfm::PresetFor(sfm::Variant::SFM3400_AW));

  while (true) {
    if (meter.GetState() != sfm::State::Idle && meter.GetState() != sfm::State::Measuring) {
      if (meter.GetState() == sfm::State::StateUnknown) {
        const auto reset = meter.HardReset();
        if (!reset || !meter.AwaitPending()) {
          ESP_LOGE(kTag, "hard reset failed");
        }
      }
      const auto init = meter.EnsureInitialized();
      if (!init) {
        ESP_LOGE(kTag, "initialise failed: %s", sfm::ToString(init.error));
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }
      if (const auto start = meter.StartMeasurement(sfm::Gas::Air);
          !start || !meter.AwaitPending()) {
        ESP_LOGE(kTag, "start failed");
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }
    }
    const auto s = meter.ReadSample();
    if (s) {
      ESP_LOGI(kTag, "%d ml/min", static_cast<int>(s.value.flow_ml_per_min));
      if (meter.ReadTemperature() && meter.AwaitPending()) {
        ESP_LOGI(kTag, "%d mdegC", static_cast<int>(meter.GetTemperature()));
      }
    } else {
      ESP_LOGW(kTag, "read failed: %s (power cycles so far: %u)", sfm::ToString(s.error),
               static_cast<unsigned>(meter.GetStats().power_cycles));
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
