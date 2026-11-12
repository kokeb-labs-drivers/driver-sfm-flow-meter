/**
 * @file sfm_flow_meter_task_example.cpp
 * @brief An SFM4300-20-B mixture measurement with the waits owned by a FreeRTOS task, and a live
 * update of the oxygen fraction.
 * @copyright Copyright (c) 2026 The driver-sfm-flow-meter authors. SPDX-License-Identifier: MIT
 *
 * The calls return the wait the driver owes the chip; this task sleeps for it (`vTaskDelay`) and
 * lets other tasks run, then calls `Poll()`. Nothing in the driver blocks.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp32_sfm_flow_meter_port.hpp"
#include "sfm_flow_meter.hpp"

namespace {
constexpr const char* kTag = "sfm_flow_meter_task";

using Meter = sfm::SfmFlowMeter<Esp32SfmFlowMeterPort, sfm::kFamilySf06>;
static_assert(Esp32SfmFlowMeterPins{}.clock_hz <=
                  sfm::FindPart(sfm::Variant::SFM4300_20_B)->max_clock_hz,
              "SCL above the part's limit");

/// Sleep for each wait the driver asks for until the operation ends; false on an error.
bool Drive(Meter& meter, sfm::Result<sfm::Wait> step, const char* what) noexcept {
  while (step && (step.value.us != 0U)) {
    vTaskDelay(pdMS_TO_TICKS((step.value.us + 999U) / 1000U)); // round up: never poll early
    step = meter.Poll();
  }
  if (!step) {
    ESP_LOGE(kTag, "%s failed: %s", what, sfm::ToString(step.error));
    return false;
  }
  return true;
}
} // namespace

extern "C" void app_main(void) {
  static Esp32SfmFlowMeterPort port;
  static Meter meter(port, sfm::PresetFor(sfm::Variant::SFM4300_20_B));

  if (!Drive(meter, meter.BeginInitialize(), "initialise")) {
    return;
  }
  uint16_t o2_permille = 300; // 30 percent oxygen in air
  if (!Drive(meter, meter.StartMeasurement(sfm::Gas::AirO2, o2_permille), "start")) {
    return;
  }

  uint32_t round = 0;
  while (true) {
    const auto s = meter.ReadSample();
    if (s) {
      ESP_LOGI(kTag, "%d ml/min, oxygen %u permille", static_cast<int>(s.value.flow_ml_per_min),
               static_cast<unsigned>(s.value.o2_permille));
    } else if (s.error != sfm::Error::NoData) {
      ESP_LOGW(kTag, "read failed: %s", sfm::ToString(s.error));
    } else {
      // No new sample yet.
    }
    if ((++round % 20U) == 0U) { // every ten seconds the gas mixer reports a new fraction
      o2_permille = (o2_permille >= 900U) ? 210U : static_cast<uint16_t>(o2_permille + 100U);
      (void)Drive(meter, meter.UpdateConcentration(o2_permille), "update concentration");
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
