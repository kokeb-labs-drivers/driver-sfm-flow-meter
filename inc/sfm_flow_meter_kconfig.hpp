/**
 * @file sfm_flow_meter_kconfig.hpp
 * @brief Optional ESP-IDF layer: a `Config` built from the
 * `CONFIG_SFM_FLOW_METER_*` Kconfig options.
 * @copyright Copyright (c) 2026 The driver-sfm-flow-meter authors.
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Include this header only where you want menuconfig to choose the defaults;
 * the driver never includes it and no driver signature depends on it. Under
 * ESP-IDF the values come from `sdkconfig.h`. Without Kconfig every option
 * falls back to the datasheet default, so `KconfigDefaults()` equals
 * `Config{}`. The variant and the address are not options: they name the part,
 * so the application sets them (`PresetFor()`).
 *
 * Kconfig writes a bool option set to "n" as an absent macro, so every bool
 * option here is off by default and an absent macro means off in both cases.
 *
 * One configuration per program: the inline function below depends on the
 * `CONFIG_` macros, so every translation unit of one program must see the same
 * values (ESP-IDF has one `sdkconfig.h` per build; each host test is its own
 * program). No driver or library header includes this one (handbook
 * KB-CPP20-018).
 */
#ifndef SFM_FLOW_METER_KCONFIG_HPP
#define SFM_FLOW_METER_KCONFIG_HPP

#ifdef __has_include
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

#include "sfm_flow_meter_types.hpp"

#ifndef CONFIG_SFM_FLOW_METER_AVERAGING
/** Averaging when Kconfig does not set it: -1, the family's reset value. */
#define CONFIG_SFM_FLOW_METER_AVERAGING (-1)
#endif
#ifndef CONFIG_SFM_FLOW_METER_ERROR_LIMIT
/** Errors in a row before an SF05-family hard reset when Kconfig does not set
 * it. */
#define CONFIG_SFM_FLOW_METER_ERROR_LIMIT 5
#endif

namespace sfm {

/**
 * @brief The configuration menuconfig selected, on top of the datasheet
 * defaults.
 * @return `Config{}` with the averaging, scaling check, range policy, timing,
 * general-call permission, restart rule and error limit taken from Kconfig.
 */
constexpr Config KconfigDefaults() noexcept {
  Config c{};
  constexpr std::int32_t kAveraging = CONFIG_SFM_FLOW_METER_AVERAGING;
  if constexpr (kAveraging >= 0) {
    c.averaging = static_cast<std::uint16_t>(kAveraging);
  }
#ifdef CONFIG_SFM_FLOW_METER_SCALING_LENIENT
  c.scaling_check = ScalingCheck::Lenient;
#elif defined(CONFIG_SFM_FLOW_METER_SCALING_OFF)
  c.scaling_check = ScalingCheck::Off;
#else
  c.scaling_check = ScalingCheck::Strict;
#endif
#if defined(CONFIG_SFM_FLOW_METER_RANGE_FLAG) && CONFIG_SFM_FLOW_METER_RANGE_FLAG
  c.range_policy = RangePolicy::Flag;
#endif
#if defined(CONFIG_SFM_FLOW_METER_TIMING_DATASHEET) && CONFIG_SFM_FLOW_METER_TIMING_DATASHEET
  c.timing = TimingPolicy::Datasheet;
#endif
#if defined(CONFIG_SFM_FLOW_METER_ALLOW_GENERAL_CALL_RESET) &&                                     \
    CONFIG_SFM_FLOW_METER_ALLOW_GENERAL_CALL_RESET
  c.allow_general_call_reset = true;
#endif
#if defined(CONFIG_SFM_FLOW_METER_NO_RESTART_BEFORE_READ) &&                                       \
    CONFIG_SFM_FLOW_METER_NO_RESTART_BEFORE_READ
  c.restart_before_read = false;
#endif
  c.consecutive_error_limit = static_cast<std::uint8_t>(CONFIG_SFM_FLOW_METER_ERROR_LIMIT);
  return c;
}

} // namespace sfm

#endif // SFM_FLOW_METER_KCONFIG_HPP
