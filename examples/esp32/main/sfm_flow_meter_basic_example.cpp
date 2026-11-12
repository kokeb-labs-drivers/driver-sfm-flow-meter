/**
 * @file sfm_flow_meter_basic_example.cpp
 * @brief Identify an SFM3003-300-CL, start an air measurement and log the flow twice a second.
 * @copyright Copyright (c) 2026 The driver-sfm-flow-meter authors. SPDX-License-Identifier: MIT
 *
 * Wiring: examples/esp32/README.md. The blocking calls are used: `EnsureInitialized()` and the
 * calls that return a wait finish through the port's `DelayUs()`. Every `Result` is checked and
 * logged by name.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp32_sfm_flow_meter_port.hpp"
#include "sfm_flow_meter.hpp"
#include "sfm_flow_meter_kconfig.hpp"

namespace {
constexpr const char* kTag = "sfm_flow_meter_example";

static_assert(Esp32SfmFlowMeterPins{}.clock_hz <=
                  sfm::FindPart(sfm::Variant::SFM3003_300_CL)->max_clock_hz,
              "SCL above the part's limit");

/// The menuconfig options, on top of the part's own variant and address.
sfm::Config BoardConfig() noexcept {
  sfm::Config cfg = sfm::KconfigDefaults();
  const sfm::Config part = sfm::PresetFor(sfm::Variant::SFM3003_300_CL);
  cfg.variant = part.variant;
  cfg.family = part.family;
  cfg.address = part.address;
  return cfg;
}
} // namespace

extern "C" void app_main(void) {
  static Esp32SfmFlowMeterPort port; // static: the driver keeps a reference
  static sfm::SfmFlowMeter<Esp32SfmFlowMeterPort, sfm::kFamilySf06> meter(port,
                                                                          BoardConfig()); // no I/O

  ESP_LOGI(kTag, "SFM flow meter driver %s", sfm::GetDriverVersion());
  const auto init = meter.EnsureInitialized();
  if (!init) {
    ESP_LOGE(kTag, "EnsureInitialized failed: %s", sfm::ToString(init.error));
    return;
  }
  ESP_LOGI(kTag, "product 0x%08X, serial 0x%08X",
           static_cast<unsigned>(meter.GetProductInfo().product_id),
           static_cast<unsigned>(meter.GetProductInfo().serial));

  const auto start = meter.StartMeasurement(sfm::Gas::Air);
  if (!start) {
    ESP_LOGE(kTag, "StartMeasurement failed: %s", sfm::ToString(start.error));
    return;
  }
  if (const auto first = meter.AwaitPending();
      !first) { // the first sample is ready after the start wait
    ESP_LOGE(kTag, "first sample wait failed: %s", sfm::ToString(first.error));
    return;
  }

  while (true) {
    const auto s = meter.ReadSample();
    if (s) {
      ESP_LOGI(kTag, "%d ml/min, %d mdegC", static_cast<int>(s.value.flow_ml_per_min),
               static_cast<int>(s.value.temperature_mdegc));
    } else if (s.error == sfm::Error::NoData) {
      // No new sample yet: ask again on the next round.
    } else if (s.error == sfm::Error::DeviceReset) {
      ESP_LOGW(kTag, "the sensor reset; starting again");
      (void)meter.StartMeasurement(sfm::Gas::Air);
      (void)meter.AwaitPending();
    } else {
      ESP_LOGW(kTag, "read failed: %s", sfm::ToString(s.error));
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
