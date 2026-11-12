/**
 * @file sfm_flow_meter_parts.hpp
 * @brief The table of supported parts: one row of data per variant, the
 * lookups, `Validate()` and the unit conversions.
 * @copyright Copyright (c) 2026 The driver-sfm-flow-meter authors.
 * SPDX-License-Identifier: MIT
 *
 * @details
 * A part is a row of data (`PartInfo`): family, addresses, scale and offset per
 * gas, temperature scale, timings, feature flags, connector. The driver's code
 * never names a part: it dispatches on `PartInfo::family` and on the feature
 * flags of the row. `static_assert`s walk the table at build time, and pin one
 * row per family against hand-typed datasheet values, so a mistyped digit fails
 * the build and not a bench.
 *
 * Rows of the `Sfx6000` family come from an interface reference the vendor
 * marks "preliminary": `PartInfo::preliminary` is true and every fact of those
 * rows is tagged `[P]` in the docs.
 */
#ifndef SFM_FLOW_METER_PARTS_HPP
#define SFM_FLOW_METER_PARTS_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "sfm_flow_meter_detail.hpp"
#include "sfm_flow_meter_registers.hpp"
#include "sfm_flow_meter_types.hpp"

namespace sfm {

/** @brief Feature bits of a part (`PartInfo::features`); a clear bit makes the
 * call `Unsupported`.
 */
namespace feature {
inline constexpr uint32_t kScaleRead = 1U << 0;        ///< `ReadScaling()`.
inline constexpr uint32_t kAveraging = 1U << 1;        ///< `SetAveraging()`.
inline constexpr uint32_t kAveragingZero = 1U << 2;    ///< N = 0 (average until read) is legal.
inline constexpr uint32_t kConcentration = 1U << 3;    ///< `UpdateConcentration()`.
inline constexpr uint32_t kSleep = 1U << 4;            ///< `Sleep()` and `WakeUp()`.
inline constexpr uint32_t kStatusWord = 1U << 5;       ///< The frame carries a status word.
inline constexpr uint32_t kTemperatureFrame = 1U << 6; ///< The frame carries the temperature.
inline constexpr uint32_t kTemperatureRead = 1U
                                             << 7; ///< `ReadTemperature()` by a pointer sequence.
inline constexpr uint32_t kGeneralCallReset = 1U << 8; ///< `SoftReset()` is the general call.
inline constexpr uint32_t kSoftResetCommand = 1U
                                              << 9; ///< `SoftReset()` is a command to the device.
inline constexpr uint32_t kResetPin = 1U << 10;     ///< `HardReset()` pulses /RES (SFM4200).
inline constexpr uint32_t kClockStretch = 1U << 11; ///< Reads need `ReadStretch()` (SFM4100).
inline constexpr uint32_t kFilterArg = 1U << 12;    ///< The start command takes a filter argument.
inline constexpr uint32_t kFirstByteAck = 1U << 13; ///< SF05: the first data byte must be ACKed.
inline constexpr uint32_t kTwoFlows = 1U << 14;     ///< The frame carries two flows (SFM3505).
inline constexpr uint32_t kRestartBeforeRead = 1U << 15; ///< SF05: start again around each read.
inline constexpr uint32_t kPowerCycle = 1U
                                        << 16;  ///< `HardReset()` is a supply cycle (needs a hook).
inline constexpr uint32_t kWakeByte = 1U << 17; ///< Wake is address plus one dummy byte.
inline constexpr uint32_t kIrqPin = 1U << 18;   ///< The module has an IRQn or DRDY output.
} // namespace feature

/** @brief Counts per slm as the rational `num / den`, the offset and the unit
 * word of one gas. */
struct Scaling {
  uint32_t num{1};   ///< Numerator of the scale factor in counts per slm.
  uint32_t den{1};   ///< Denominator (10 for the SFM3000's 142.8).
  int32_t offset{0}; ///< Offset in counts.
  uint16_t unit{0};  ///< The 16-bit flow-unit word; 0 for SF05.
};

/** @brief One calibrated gas of a part: its start code, its status index and
 * its scaling. */
struct GasRow {
  Gas gas{Gas::Air};          ///< The gas.
  uint16_t code{0};           ///< The start command (or pointer, or trigger byte).
  uint8_t status_index{0};    ///< The status-word nibble that identifies the running command.
  bool takes_argument{false}; ///< A mixture: the start command carries the oxygen fraction.
  Scaling scaling{};          ///< Scale, offset and unit.
};

/** @brief The temperature scaling of a part. */
struct TempRow {
  int32_t scale{0};  ///< Counts per degree Celsius; 0 when the part has no temperature.
  int32_t offset{0}; ///< Counts.
};

/** @brief Timings of a part in microseconds: the datasheet maxima. */
struct Timing {
  uint32_t power_up_us{0};     ///< Power-up until the sensor answers.
  uint32_t soft_reset_us{0};   ///< Soft reset until the sensor answers.
  uint32_t first_sample_us{0}; ///< Start command until the first result.
  uint32_t warm_up_us{0};      ///< Start command until the result is accurate.
  uint32_t stop_us{0};         ///< Stop command until the sensor accepts a command.
  uint32_t wake_us{0};         ///< Wake from sleep until the sensor answers.
  uint32_t id_read_us{0};      ///< Identifier command until the data can be read.
  uint32_t hard_reset_us{0}; ///< /RES low time (SFM4200), or the supply-off time of a power cycle.
};

/** @brief The addresses a part may use; `list[0]` is the default. */
struct AddressSet {
  std::array<uint8_t, 7> list{}; ///< The 7-bit addresses.
  uint8_t count{0};              ///< Entries in use.
};

/** @brief The gases of a part. */
struct GasList {
  std::array<GasRow, 7> rows{}; ///< The calibrated gases.
  uint8_t count{0};             ///< Entries in use.
};

/** @brief One row of the part table. */
struct PartInfo {
  Variant variant{Variant::Auto}; ///< The variant this row describes.
  Family family{Family::Sf06};    ///< The command-set family.
  const char* name{""};           ///< The vendor's part name.
  uint32_t id_value{0};           ///< Product identifier to match (SF06, SFM3505, SFx6000); 0 else.
  uint32_t id_mask{0};            ///< Bits of the identifier that are compared; 0 when not used.
  AddressSet addresses{};         ///< The permitted 7-bit addresses.
  uint8_t crc_init{0xFF};         ///< CRC-8 initial value: 0xFF or 0x00.
  GasList gases{};                ///< The calibrated gases.
  int32_t span_min_mlpm{0};       ///< Lowest specified flow in ml/min.
  int32_t span_max_mlpm{0};       ///< Highest specified flow in ml/min.
  TempRow temperature{};          ///< Temperature scaling.
  uint8_t raw_bits{16};           ///< 16, or 24 for the SFM3505.
  bool raw_signed{true};          ///< The flow word is two's complement.
  Timing timing{};                ///< Datasheet timings.
  uint32_t features{0};           ///< `feature::` bits.
  uint32_t max_clock_hz{0};       ///< Highest SCL frequency of the part.
  uint16_t supply_mv_min{0};      ///< Lowest supply voltage of the sensor.
  uint16_t supply_mv_max{0};      ///< Highest supply voltage of the sensor.
  uint8_t connector{0};           ///< Connector id of the hardware page (informational).
  bool preliminary{false};        ///< The protocol document of the part is preliminary.
};

/** @brief The part table: what the code, the docs generators and the tests
 * read. */
namespace parts {

/// Rows in the table: one per variant but `Auto`.
inline constexpr std::size_t kCount = 32;

/// @cond SFM_INTERNAL
constexpr GasRow Row(Gas gas, uint16_t code, uint8_t idx, bool arg, Scaling s) noexcept {
  return GasRow{.gas = gas, .code = code, .status_index = idx, .takes_argument = arg, .scaling = s};
}

template <typename... R>
constexpr GasList Gases(R... rows) noexcept {
  static_assert(sizeof...(R) <= 7U);
  GasList list{};
  const std::array<GasRow, sizeof...(R)> given{rows...};
  for (std::size_t i = 0U; i < given.size(); ++i) {
    list.rows[i] = given[i];
  }
  list.count = static_cast<uint8_t>(sizeof...(R));
  return list;
}

template <typename... A>
constexpr AddressSet Addresses(A... addr) noexcept {
  static_assert(sizeof...(A) <= 7U);
  AddressSet set{};
  const std::array<uint8_t, sizeof...(A)> given{static_cast<uint8_t>(addr)...};
  for (std::size_t i = 0U; i < given.size(); ++i) {
    set.list[i] = given[i];
  }
  set.count = static_cast<uint8_t>(sizeof...(A));
  return set;
}

constexpr uint32_t kSf06Common = feature::kScaleRead | feature::kAveraging |
                                 feature::kAveragingZero | feature::kConcentration |
                                 feature::kSleep | feature::kStatusWord |
                                 feature::kTemperatureFrame | feature::kGeneralCallReset;
constexpr uint32_t kSf05Common = feature::kScaleRead | feature::kFirstByteAck |
                                 feature::kRestartBeforeRead | feature::kPowerCycle;

// Scalings: SFM4300-20 Table 15, SFM4300-50 Table 16, SFM3003 Table 15, SFM3304
// Table 15, SFM3505 3.5.
constexpr Scaling kS4300x20{.num = 2500, .den = 1, .offset = -28672, .unit = reg::kUnitSlm};
constexpr Scaling kS4300x50{.num = 1000, .den = 1, .offset = -28672, .unit = reg::kUnitSlm};
constexpr Scaling kS3xxx{.num = 170, .den = 1, .offset = -24576, .unit = reg::kUnitSlm};
constexpr Scaling kS3xxxCe{.num = 120, .den = 1, .offset = -12288, .unit = reg::kUnitSlm};
constexpr Scaling kS3304{.num = 120, .den = 1, .offset = 0, .unit = reg::kUnitSlm};
constexpr Scaling kS3505{.num = 25600, .den = 1, .offset = 8388608, .unit = 0};
// SF05: AN-05 and the datasheets (unit word not reported).
constexpr Scaling kS3000{.num = 140, .den = 1, .offset = 32000, .unit = 0};
constexpr Scaling kS3000O2{.num = 1428, .den = 10, .offset = 32000, .unit = 0};
constexpr Scaling kS3200{.num = 120, .den = 1, .offset = 32768, .unit = 0};
constexpr Scaling kS3400{.num = 800, .den = 1, .offset = 32768, .unit = 0};
constexpr Scaling kS4200{.num = 256, .den = 1, .offset = 0, .unit = 0};
constexpr Scaling kS4100{.num = 1000, .den = 1, .offset = 0, .unit = reg::kUnitSccm};
// SFx6000 [P]: interface reference section 3.5.1.
constexpr Scaling kS6000x50a{.num = 1024, .den = 1, .offset = -28672, .unit = reg::kUnitSlm};
constexpr Scaling kS6000x50b{.num = 2560, .den = 1, .offset = -28672, .unit = reg::kUnitSlm};
constexpr Scaling kS6000x20a{.num = 2560, .den = 1, .offset = -28672, .unit = reg::kUnitSlm};
constexpr Scaling kS6000x20b{.num = 5120, .den = 1, .offset = -28672, .unit = reg::kUnitSlm};
constexpr Scaling kS6000x5a{.num = 10240, .den = 1, .offset = -28672, .unit = reg::kUnitSlm};
constexpr Scaling kS6000x5b{.num = 25600, .den = 1, .offset = -28672, .unit = reg::kUnitSlm};

constexpr GasList Sf06Basic(Scaling s) noexcept {
  return Gases(Row(Gas::O2, reg::kCmdStartGas0, 0, false, s),
               Row(Gas::Air, reg::kCmdStartGas1, 1, false, s),
               Row(Gas::AirO2, reg::kCmdStartMixture0, 6, true, s));
}

constexpr GasList Sf06Full(Scaling s) noexcept {
  return Gases(Row(Gas::O2, reg::kCmdStartGas0, 0, false, s),
               Row(Gas::Air, reg::kCmdStartGas1, 1, false, s),
               Row(Gas::N2O, reg::kCmdStartGas2, 2, false, s),
               Row(Gas::CO2, reg::kCmdStartGas3, 3, false, s),
               Row(Gas::AirO2, reg::kCmdStartMixture0, 6, true, s),
               Row(Gas::N2OO2, reg::kCmdStartMixture1, 7, true, s),
               Row(Gas::CO2O2, reg::kCmdStartMixture2, 8, true, s));
}

constexpr GasList Sf06Heliox(Scaling s) noexcept {
  return Gases(Row(Gas::O2, reg::kCmdStartGas0, 0, false, s),
               Row(Gas::Air, reg::kCmdStartGas1, 1, false, s),
               Row(Gas::HeOx, reg::kCmdStartGas2, 2, false, s),
               Row(Gas::AirO2, reg::kCmdStartMixture0, 6, true, s),
               Row(Gas::HeOxO2, reg::kCmdStartMixture1, 7, true, s));
}

constexpr GasList Sf05Gases(Scaling air, Scaling o2) noexcept {
  return Gases(Row(Gas::Air, reg::kSf05Flow, 0, false, air),
               Row(Gas::N2, reg::kSf05Flow, 0, false, air),
               Row(Gas::O2, reg::kSf05Flow, 0, false, o2));
}

/// Air calibration, nitrogen at the air scale: the media tables of the SFM3200
/// (1.3), SFM3300, SFM3400 and SFM4200 (1.3) list N2, the scale rows of the
/// SFM3300 (120) and SFM3400 (800) read "Air, N2", and the SFM3000 states one
/// scale for both (2.1). Oxygen is not offered.
constexpr GasList Sf05AirN2(Scaling air) noexcept {
  return Gases(Row(Gas::Air, reg::kSf05Flow, 0, false, air),
               Row(Gas::N2, reg::kSf05Flow, 0, false, air));
}

constexpr GasList Sf04Gas(Gas gas) noexcept {
  return Gases(Row(gas, reg::kSf04TriggerFlow, 0, false, kS4100));
}

constexpr GasList Sfx6000Gases(Scaling a, Scaling b) noexcept {
  return Gases(
      Row(Gas::O2, reg::kCmdStartGas0, 0, false, a), Row(Gas::Air, reg::kCmdStartGas1, 1, false, a),
      Row(Gas::CO2, reg::kCmdStartGas2, 2, false, b),
      Row(Gas::N2O, reg::kCmdStartGas3, 3, false, b), Row(Gas::Ar, reg::kCmdStartGas4, 4, false, b),
      Row(Gas::AirO2, reg::kCmdStartSfxMixture0, 10, true, a));
}

// Timings in microseconds (datasheet maxima; see docs/hardware.md for the
// section of each).
constexpr Timing kT4300{.power_up_us = 20000,
                        .soft_reset_us = 20000,
                        .first_sample_us = 12000,
                        .warm_up_us = 30000,
                        .stop_us = 500,
                        .wake_us = 20000,
                        .id_read_us = 0,
                        .hard_reset_us = 0};
constexpr Timing kT3xxx{.power_up_us = 2000,
                        .soft_reset_us = 2000,
                        .first_sample_us = 12000,
                        .warm_up_us = 30000,
                        .stop_us = 500,
                        .wake_us = 16000,
                        .id_read_us = 0,
                        .hard_reset_us = 0};
constexpr Timing kT3304{.power_up_us = 2000,
                        .soft_reset_us = 2000,
                        .first_sample_us = 4000,
                        .warm_up_us = 50000,
                        .stop_us = 500,
                        .wake_us = 2000,
                        .id_read_us = 0,
                        .hard_reset_us = 0};
constexpr Timing kT3505{.power_up_us = 2000,
                        .soft_reset_us = 2000,
                        .first_sample_us = 4000,
                        .warm_up_us = 50000,
                        .stop_us = 500,
                        .wake_us = 0,
                        .id_read_us = 60,
                        .hard_reset_us = 0};
constexpr Timing kT3000{.power_up_us = 100000,
                        .soft_reset_us = 80000,
                        .first_sample_us = 30000,
                        .warm_up_us = 30000,
                        .stop_us = 0,
                        .wake_us = 0,
                        .id_read_us = 0,
                        .hard_reset_us = 0};
constexpr Timing kT3x00{.power_up_us = 100000,
                        .soft_reset_us = 80000,
                        .first_sample_us = 100000,
                        .warm_up_us = 100000,
                        .stop_us = 0,
                        .wake_us = 0,
                        .id_read_us = 0,
                        .hard_reset_us = 0};
constexpr Timing kT4200{.power_up_us = 2000,
                        .soft_reset_us = 0,
                        .first_sample_us = 100000,
                        .warm_up_us = 100000,
                        .stop_us = 0,
                        .wake_us = 0,
                        .id_read_us = 0,
                        .hard_reset_us = 1000};
constexpr Timing kT4100{.power_up_us = 20000,
                        .soft_reset_us = 20000,
                        .first_sample_us = 0,
                        .warm_up_us = 500000,
                        .stop_us = 0,
                        .wake_us = 0,
                        .id_read_us = 0,
                        .hard_reset_us = 0};
constexpr Timing kT6000{.power_up_us = 30000,
                        .soft_reset_us = 30000,
                        .first_sample_us = 12000,
                        .warm_up_us = 1000000,
                        .stop_us = 1000,
                        .wake_us = 0,
                        .id_read_us = 0,
                        .hard_reset_us = 0};

/// @endcond

// clang-format off: one field per line keeps each row readable against its datasheet table.
/// The table, in `Variant` order after `Auto`.
inline constexpr std::array<PartInfo, kCount> kTable{{
    {.variant = Variant::SFM4300_20_B, .family = Family::Sf06, .name = "SFM4300-20-B",
     .id_value = 0x04030100, .id_mask = 0xFFFFFF00, .addresses = Addresses(0x2A, 0x2B, 0x2C, 0x2D),
     .crc_init = 0xFF, .gases = Sf06Full(kS4300x20), .span_min_mlpm = 0, .span_max_mlpm = 20000,
     .temperature = {.scale = 200, .offset = 0}, .timing = kT4300, .features = kSf06Common | feature::kIrqPin,
     .max_clock_hz = 1000000, .supply_mv_min = 3000, .supply_mv_max = 5500, .connector = 1},
    {.variant = Variant::SFM4300_20_O, .family = Family::Sf06, .name = "SFM4300-20-O",
     .id_value = 0x04030200, .id_mask = 0xFFFFFF00, .addresses = Addresses(0x2A, 0x2B, 0x2C, 0x2D),
     .crc_init = 0xFF, .gases = Sf06Full(kS4300x20), .span_min_mlpm = 0, .span_max_mlpm = 20000,
     .temperature = {.scale = 200, .offset = 0}, .timing = kT4300, .features = kSf06Common | feature::kIrqPin,
     .max_clock_hz = 1000000, .supply_mv_min = 3000, .supply_mv_max = 5500, .connector = 1},
    {.variant = Variant::SFM4300_20_P, .family = Family::Sf06, .name = "SFM4300-20-P",
     .id_value = 0x04030300, .id_mask = 0xFFFFFF00, .addresses = Addresses(0x2A, 0x2B, 0x2C, 0x2D),
     .crc_init = 0xFF, .gases = Sf06Full(kS4300x20), .span_min_mlpm = 0, .span_max_mlpm = 20000,
     .temperature = {.scale = 200, .offset = 0}, .timing = kT4300, .features = kSf06Common | feature::kIrqPin,
     .max_clock_hz = 1000000, .supply_mv_min = 3000, .supply_mv_max = 5500, .connector = 1},
    {.variant = Variant::SFM4300_50_B, .family = Family::Sf06, .name = "SFM4300-50-B",
     .id_value = 0x04030900, .id_mask = 0xFFFFFF00, .addresses = Addresses(0x2A, 0x2B, 0x2C, 0x2D),
     .crc_init = 0xFF, .gases = Sf06Basic(kS4300x50), .span_min_mlpm = 0, .span_max_mlpm = 50000,
     .temperature = {.scale = 200, .offset = 0}, .timing = kT4300, .features = kSf06Common | feature::kIrqPin,
     .max_clock_hz = 1000000, .supply_mv_min = 3000, .supply_mv_max = 5500, .connector = 1},
    {.variant = Variant::SFM4300_50_O, .family = Family::Sf06, .name = "SFM4300-50-O",
     .id_value = 0x04030700, .id_mask = 0xFFFFFF00, .addresses = Addresses(0x2A, 0x2B, 0x2C, 0x2D),
     .crc_init = 0xFF, .gases = Sf06Basic(kS4300x50), .span_min_mlpm = 0, .span_max_mlpm = 50000,
     .temperature = {.scale = 200, .offset = 0}, .timing = kT4300, .features = kSf06Common | feature::kIrqPin,
     .max_clock_hz = 1000000, .supply_mv_min = 3000, .supply_mv_max = 5500, .connector = 1},
    {.variant = Variant::SFM4300_50_P, .family = Family::Sf06, .name = "SFM4300-50-P",
     .id_value = 0x04030600, .id_mask = 0xFFFFFF00, .addresses = Addresses(0x2A, 0x2B, 0x2C, 0x2D),
     .crc_init = 0xFF, .gases = Sf06Basic(kS4300x50), .span_min_mlpm = 0, .span_max_mlpm = 50000,
     .temperature = {.scale = 200, .offset = 0}, .timing = kT4300, .features = kSf06Common | feature::kIrqPin,
     .max_clock_hz = 1000000, .supply_mv_min = 3000, .supply_mv_max = 5500, .connector = 1},
    {.variant = Variant::SFM3003_300_CL, .family = Family::Sf06, .name = "SFM3003-300-CL",
     .id_value = 0x04020100, .id_mask = 0xFFFFFF00, .addresses = Addresses(0x28),
     .crc_init = 0xFF, .gases = Sf06Basic(kS3xxx), .span_min_mlpm = -30000, .span_max_mlpm = 300000,
     .temperature = {.scale = 200, .offset = 0}, .timing = kT3xxx, .features = kSf06Common,
     .max_clock_hz = 1000000, .supply_mv_min = 2700, .supply_mv_max = 5500, .connector = 2},
    {.variant = Variant::SFM3003_300_CE, .family = Family::Sf06, .name = "SFM3003-300-CE",
     .id_value = 0x04020700, .id_mask = 0xFFFFFF00, .addresses = Addresses(0x2D),
     .crc_init = 0xFF, .gases = Sf06Basic(kS3xxxCe), .span_min_mlpm = -150000, .span_max_mlpm = 300000,
     .temperature = {.scale = 200, .offset = 0}, .timing = kT3xxx, .features = kSf06Common,
     .max_clock_hz = 1000000, .supply_mv_min = 2700, .supply_mv_max = 5500, .connector = 2},
    {.variant = Variant::SFM3003_300_CET, .family = Family::Sf06, .name = "SFM3003-300-CET",
     .id_value = 0x04020800, .id_mask = 0xFFFFFF00, .addresses = Addresses(0x2A),
     .crc_init = 0xFF, .gases = Sf06Basic(kS3xxxCe), .span_min_mlpm = -150000, .span_max_mlpm = 300000,
     .temperature = {.scale = 200, .offset = 0}, .timing = kT3xxx, .features = kSf06Common,
     .max_clock_hz = 1000000, .supply_mv_min = 2700, .supply_mv_max = 5500, .connector = 2},
    {.variant = Variant::SFM3013_300_CL, .family = Family::Sf06, .name = "SFM3013-300-CL",
     .id_value = 0x04020500, .id_mask = 0xFFFFFF00, .addresses = Addresses(0x2F),
     .crc_init = 0xFF, .gases = Sf06Basic(kS3xxx), .span_min_mlpm = -30000, .span_max_mlpm = 300000,
     .temperature = {.scale = 200, .offset = 0}, .timing = kT3xxx, .features = kSf06Common,
     .max_clock_hz = 1000000, .supply_mv_min = 2700, .supply_mv_max = 5500, .connector = 2},
    {.variant = Variant::SFM3013_300_CLM, .family = Family::Sf06, .name = "SFM3013-300-CLM",
     .id_value = 0x04020200, .id_mask = 0xFFFFFF00, .addresses = Addresses(0x2F),
     .crc_init = 0xFF, .gases = Sf06Heliox(kS3xxx), .span_min_mlpm = -30000, .span_max_mlpm = 300000,
     .temperature = {.scale = 200, .offset = 0}, .timing = kT3xxx, .features = kSf06Common,
     .max_clock_hz = 1000000, .supply_mv_min = 2700, .supply_mv_max = 5500, .connector = 2},
    {.variant = Variant::SFM3019_200_CL, .family = Family::Sf06, .name = "SFM3019-200-CL",
     .id_value = 0x04020600, .id_mask = 0xFFFFFF00, .addresses = Addresses(0x2E),
     .crc_init = 0xFF, .gases = Sf06Basic(kS3xxx), .span_min_mlpm = -10000, .span_max_mlpm = 240000,
     .temperature = {.scale = 200, .offset = 0}, .timing = kT3xxx, .features = kSf06Common,
     .max_clock_hz = 1000000, .supply_mv_min = 2700, .supply_mv_max = 5500, .connector = 2},
    {.variant = Variant::SFM3119_240_CL, .family = Family::Sf06, .name = "SFM3119-240-CL",
     .id_value = 0x04040100, .id_mask = 0xFFFFFF00, .addresses = Addresses(0x29),
     .crc_init = 0xFF, .gases = Sf06Basic(kS3xxx), .span_min_mlpm = -10000, .span_max_mlpm = 240000,
     .temperature = {.scale = 200, .offset = 0}, .timing = kT3xxx, .features = kSf06Common,
     .max_clock_hz = 1000000, .supply_mv_min = 2700, .supply_mv_max = 5500, .connector = 2},
    {.variant = Variant::SFM3304_250_D, .family = Family::Sf06Gen2, .name = "SFM3304-250-D",
     .id_value = 0x04050100, .id_mask = 0xFFFFFF00, .addresses = Addresses(0x2E),
     .crc_init = 0xFF, .gases = Gases(Row(Gas::Air, reg::kCmdStartGas0, 0, false, kS3304)),
     .span_min_mlpm = -250000, .span_max_mlpm = 250000, .temperature = {.scale = 200, .offset = 0}, .timing = kT3304,
     .features = feature::kScaleRead | feature::kAveraging | feature::kAveragingZero |
                 feature::kSleep | feature::kStatusWord | feature::kTemperatureFrame |
                 feature::kGeneralCallReset | feature::kFilterArg | feature::kWakeByte,
     .max_clock_hz = 1000000, .supply_mv_min = 3150, .supply_mv_max = 3450, .connector = 7},
    {.variant = Variant::SFM3505_300, .family = Family::Sf06Dual, .name = "SFM3505-300",
     .id_value = 0x04060200, .id_mask = 0xFFFFFF00, .addresses = Addresses(0x2E),
     .crc_init = 0xFF, .gases = Gases(Row(Gas::Air, reg::kCmdStartGas0, 0, false, kS3505)),
     .span_min_mlpm = -150000, .span_max_mlpm = 300000, .temperature = {.scale = 0, .offset = 0}, .raw_bits = 24,
     .raw_signed = false, .timing = kT3505,
     .features = feature::kAveraging | feature::kGeneralCallReset | feature::kFilterArg |
                 feature::kTwoFlows | feature::kPowerCycle | feature::kIrqPin,
     .max_clock_hz = 1000000, .supply_mv_min = 3200, .supply_mv_max = 3400, .connector = 5},
    {.variant = Variant::SFM3505_300_X, .family = Family::Sf06Dual, .name = "SFM3505-300-X",
     .id_value = 0x04060100, .id_mask = 0xFFFFFF00, .addresses = Addresses(0x2E),
     .crc_init = 0xFF, .gases = Gases(Row(Gas::Air, reg::kCmdStartGas0, 0, false, kS3505)),
     .span_min_mlpm = -150000, .span_max_mlpm = 300000, .temperature = {.scale = 0, .offset = 0}, .raw_bits = 24,
     .raw_signed = false, .timing = kT3505,
     .features = feature::kAveraging | feature::kGeneralCallReset | feature::kFilterArg |
                 feature::kTwoFlows | feature::kPowerCycle | feature::kIrqPin,
     .max_clock_hz = 1000000, .supply_mv_min = 3200, .supply_mv_max = 3400, .connector = 5},
    {.variant = Variant::SFM3000_200_C, .family = Family::Sf05, .name = "SFM3000-200-C",
     .addresses = Addresses(0x40), .crc_init = 0x00, .gases = Sf05Gases(kS3000, kS3000O2),
     .span_min_mlpm = -200000, .span_max_mlpm = 200000, .temperature = {.scale = 100, .offset = 20000},
     .raw_signed = false, .timing = kT3000,
     .features = kSf05Common | feature::kSoftResetCommand | feature::kTemperatureRead,
     .max_clock_hz = 400000, .supply_mv_min = 4750, .supply_mv_max = 5250, .connector = 2},
    {.variant = Variant::SFM3200, .family = Family::Sf05, .name = "SFM3200",
     .addresses = Addresses(0x40), .crc_init = 0x00, .gases = Sf05AirN2(kS3200),
     .span_min_mlpm = -100000, .span_max_mlpm = 250000, .temperature = {.scale = 0, .offset = 0},
     .raw_signed = false, .timing = kT3x00, .features = kSf05Common | feature::kSoftResetCommand,
     .max_clock_hz = 400000, .supply_mv_min = 4750, .supply_mv_max = 5250, .connector = 3},
    {.variant = Variant::SFM3200_AW, .family = Family::Sf05, .name = "SFM3200-AW",
     .addresses = Addresses(0x40), .crc_init = 0x00, .gases = Sf05AirN2(kS3200),
     .span_min_mlpm = -100000, .span_max_mlpm = 250000, .temperature = {.scale = 100, .offset = 20000},
     .raw_signed = false, .timing = kT3x00,
     .features = kSf05Common | feature::kSoftResetCommand | feature::kTemperatureRead,
     .max_clock_hz = 400000, .supply_mv_min = 4750, .supply_mv_max = 5250, .connector = 6},
    {.variant = Variant::SFM3300_D, .family = Family::Sf05, .name = "SFM3300-D",
     .addresses = Addresses(0x40), .crc_init = 0x00, .gases = Sf05AirN2(kS3200),
     .span_min_mlpm = -250000, .span_max_mlpm = 250000, .temperature = {.scale = 0, .offset = 0},
     .raw_signed = false, .timing = kT3x00, .features = kSf05Common | feature::kSoftResetCommand,
     .max_clock_hz = 400000, .supply_mv_min = 4750, .supply_mv_max = 5250, .connector = 6},
    {.variant = Variant::SFM3300_AW, .family = Family::Sf05, .name = "SFM3300-AW",
     .addresses = Addresses(0x40), .crc_init = 0x00, .gases = Sf05AirN2(kS3200),
     .span_min_mlpm = -250000, .span_max_mlpm = 250000, .temperature = {.scale = 100, .offset = 20000},
     .raw_signed = false, .timing = kT3x00,
     .features = kSf05Common | feature::kSoftResetCommand | feature::kTemperatureRead,
     .max_clock_hz = 400000, .supply_mv_min = 4750, .supply_mv_max = 5250, .connector = 6},
    {.variant = Variant::SFM3400_D, .family = Family::Sf05, .name = "SFM3400-D",
     .addresses = Addresses(0x40), .crc_init = 0x00, .gases = Sf05AirN2(kS3400),
     .span_min_mlpm = -33000, .span_max_mlpm = 33000, .temperature = {.scale = 0, .offset = 0},
     .raw_signed = false, .timing = kT3x00, .features = kSf05Common | feature::kSoftResetCommand,
     .max_clock_hz = 400000, .supply_mv_min = 4750, .supply_mv_max = 5250, .connector = 6},
    {.variant = Variant::SFM3400_AW, .family = Family::Sf05, .name = "SFM3400-AW",
     .addresses = Addresses(0x40), .crc_init = 0x00, .gases = Sf05AirN2(kS3400),
     .span_min_mlpm = -33000, .span_max_mlpm = 33000, .temperature = {.scale = 100, .offset = 20000},
     .raw_signed = false, .timing = kT3x00,
     .features = kSf05Common | feature::kSoftResetCommand | feature::kTemperatureRead,
     .max_clock_hz = 400000, .supply_mv_min = 4750, .supply_mv_max = 5250, .connector = 6},
    {.variant = Variant::SFM4200, .family = Family::Sf05, .name = "SFM4200",
     .addresses = Addresses(0x40), .crc_init = 0x00, .gases = Sf05AirN2(kS4200),
     .span_min_mlpm = 0, .span_max_mlpm = 160000, .temperature = {.scale = 100, .offset = 20000},
     .raw_signed = false, .timing = kT4200,
     .features = kSf05Common | feature::kResetPin | feature::kTemperatureRead,
     .max_clock_hz = 100000, .supply_mv_min = 4750, .supply_mv_max = 5250, .connector = 4},
    {.variant = Variant::SFM4100_AIR, .family = Family::Sf04, .name = "SFM4100-Air",
     .addresses = Addresses(1), .crc_init = 0x00, .gases = Sf04Gas(Gas::Air),
     .span_min_mlpm = 0, .span_max_mlpm = 20000, .temperature = {.scale = 0, .offset = 0}, .timing = kT4100,
     .features = feature::kScaleRead | feature::kClockStretch | feature::kSoftResetCommand,
     .max_clock_hz = 120000, .supply_mv_min = 3500, .supply_mv_max = 9000, .connector = 8},
    {.variant = Variant::SFM4100_O2, .family = Family::Sf04, .name = "SFM4100-O2",
     .addresses = Addresses(2), .crc_init = 0x00, .gases = Sf04Gas(Gas::O2),
     .span_min_mlpm = 0, .span_max_mlpm = 20000, .temperature = {.scale = 0, .offset = 0}, .timing = kT4100,
     .features = feature::kScaleRead | feature::kClockStretch | feature::kSoftResetCommand,
     .max_clock_hz = 120000, .supply_mv_min = 3500, .supply_mv_max = 9000, .connector = 8},
    {.variant = Variant::SFM4100_CO2, .family = Family::Sf04, .name = "SFM4100-CO2",
     .addresses = Addresses(3), .crc_init = 0x00, .gases = Sf04Gas(Gas::CO2),
     .span_min_mlpm = 0, .span_max_mlpm = 20000, .temperature = {.scale = 0, .offset = 0}, .timing = kT4100,
     .features = feature::kScaleRead | feature::kClockStretch | feature::kSoftResetCommand,
     .max_clock_hz = 120000, .supply_mv_min = 3500, .supply_mv_max = 9000, .connector = 8},
    {.variant = Variant::SFM4100_N2O, .family = Family::Sf04, .name = "SFM4100-N2O",
     .addresses = Addresses(4), .crc_init = 0x00, .gases = Sf04Gas(Gas::N2O),
     .span_min_mlpm = 0, .span_max_mlpm = 20000, .temperature = {.scale = 0, .offset = 0}, .timing = kT4100,
     .features = feature::kScaleRead | feature::kClockStretch | feature::kSoftResetCommand,
     .max_clock_hz = 120000, .supply_mv_min = 3500, .supply_mv_max = 9000, .connector = 8},
    {.variant = Variant::SFM4100_AR, .family = Family::Sf04, .name = "SFM4100-Ar",
     .addresses = Addresses(5), .crc_init = 0x00, .gases = Sf04Gas(Gas::Ar),
     .span_min_mlpm = 0, .span_max_mlpm = 20000, .temperature = {.scale = 0, .offset = 0}, .timing = kT4100,
     .features = feature::kScaleRead | feature::kClockStretch | feature::kSoftResetCommand,
     .max_clock_hz = 120000, .supply_mv_min = 3500, .supply_mv_max = 9000, .connector = 8},
    {.variant = Variant::SFM6000D_50, .family = Family::Sfx6000, .name = "SFM6000D-50",
     .id_value = 0x06021184, .id_mask = 0xFFFFFFFF,
     .addresses = Addresses(0x24, 0x23, 0x22, 0x21, 0x20, 0x42, 0x41), .crc_init = 0xFF,
     .gases = Sfx6000Gases(kS6000x50a, kS6000x50b), .span_min_mlpm = 0, .span_max_mlpm = 50000,
     .temperature = {.scale = 200, .offset = 0}, .timing = kT6000,
     .features = feature::kScaleRead | feature::kConcentration | feature::kStatusWord |
                 feature::kTemperatureRead | feature::kGeneralCallReset,
     .max_clock_hz = 1000000, .supply_mv_min = 22800, .supply_mv_max = 25200, .connector = 9,
     .preliminary = true},
    {.variant = Variant::SFM6000D_20, .family = Family::Sfx6000, .name = "SFM6000D-20",
     .id_value = 0x06021284, .id_mask = 0xFFFFFFFF,
     .addresses = Addresses(0x24, 0x23, 0x22, 0x21, 0x20, 0x42, 0x41), .crc_init = 0xFF,
     .gases = Sfx6000Gases(kS6000x20a, kS6000x20b), .span_min_mlpm = 0, .span_max_mlpm = 20000,
     .temperature = {.scale = 200, .offset = 0}, .timing = kT6000,
     .features = feature::kScaleRead | feature::kConcentration | feature::kStatusWord |
                 feature::kTemperatureRead | feature::kGeneralCallReset,
     .max_clock_hz = 1000000, .supply_mv_min = 22800, .supply_mv_max = 25200, .connector = 9,
     .preliminary = true},
    {.variant = Variant::SFM6000D_5, .family = Family::Sfx6000, .name = "SFM6000D-5",
     .id_value = 0x06021484, .id_mask = 0xFFFFFFFF,
     .addresses = Addresses(0x24, 0x23, 0x22, 0x21, 0x20, 0x42, 0x41), .crc_init = 0xFF,
     .gases = Sfx6000Gases(kS6000x5a, kS6000x5b), .span_min_mlpm = 0, .span_max_mlpm = 5000,
     .temperature = {.scale = 200, .offset = 0}, .timing = kT6000,
     .features = feature::kScaleRead | feature::kConcentration | feature::kStatusWord |
                 feature::kTemperatureRead | feature::kGeneralCallReset,
     .max_clock_hz = 1000000, .supply_mv_min = 22800, .supply_mv_max = 25200, .connector = 9,
     .preliminary = true},
}};
// clang-format on

} // namespace parts

/**
 * @brief The table row of a variant.
 * @param variant A variant other than `Auto`.
 * @return The row, or `nullptr` for `Auto` or a value that is not an
 * enumerator.
 */
constexpr const PartInfo* FindPart(Variant variant) noexcept {
  const auto index = static_cast<uint32_t>(detail::ToUnderlying(variant));
  if ((index == 0U) || (index > parts::kCount)) {
    return nullptr;
  }
  return &parts::kTable[index - 1U];
}

/**
 * @brief The table row that a product identifier and address select inside one
 * family.
 * @param family The configured family; a row of another family never matches.
 * @param id The 32-bit product identifier read from the sensor.
 * @param address The 7-bit address the sensor answered at.
 * @return The row, or `nullptr` when no row of `family` matches (for example a
 * controller product).
 */
constexpr const PartInfo* FindById(Family family, uint32_t id, uint8_t address) noexcept {
  for (const PartInfo& row : parts::kTable) {
    if ((row.family != family) || (row.id_mask == 0U) || ((id & row.id_mask) != row.id_value)) {
      continue;
    }
    for (uint8_t i = 0U; i < row.addresses.count; ++i) {
      if (row.addresses.list[i] == address) {
        return &row;
      }
    }
  }
  return nullptr;
}

/**
 * @brief The first row of a family: what `Variant::Auto` uses for waits before
 * the part is known.
 * @param family The family.
 * @return The row, or `nullptr` when the family has none.
 */
constexpr const PartInfo* FamilyRow(Family family) noexcept {
  for (const PartInfo& row : parts::kTable) {
    if (row.family == family) {
      return &row;
    }
  }
  return nullptr;
}

/**
 * @brief The gas row of a part.
 * @param part The part.
 * @param gas The gas.
 * @return The row, or `nullptr` when the part does not calibrate that gas.
 */
constexpr const GasRow* FindGas(const PartInfo& part, Gas gas) noexcept {
  for (uint8_t i = 0U; i < part.gases.count; ++i) {
    if (part.gases.rows[i].gas == gas) {
      return &part.gases.rows[i];
    }
  }
  return nullptr;
}

/**
 * @param part The part.
 * @param bits `feature::` bits.
 * @return Whether the part has every bit of `bits` set in its features.
 */
constexpr bool Has(const PartInfo& part, uint32_t bits) noexcept {
  return (part.features & bits) == bits;
}

/**
 * @param part The part.
 * @return The part's capabilities, from its feature bits.
 */
constexpr Capabilities CapabilitiesOf(const PartInfo& part) noexcept {
  Capabilities c{};
  c.scale_read = Has(part, feature::kScaleRead);
  c.averaging = Has(part, feature::kAveraging);
  c.concentration = Has(part, feature::kConcentration);
  c.sleep = Has(part, feature::kSleep);
  c.temperature_in_frame = Has(part, feature::kTemperatureFrame);
  c.temperature_read = Has(part, feature::kTemperatureRead);
  c.status_word = Has(part, feature::kStatusWord);
  c.general_call_reset = Has(part, feature::kGeneralCallReset);
  c.soft_reset_command = Has(part, feature::kSoftResetCommand);
  c.hard_reset = Has(part, feature::kResetPin) || Has(part, feature::kPowerCycle);
  c.filter = Has(part, feature::kFilterArg);
  c.two_flows = Has(part, feature::kTwoFlows);
  c.clock_stretch = Has(part, feature::kClockStretch);
  c.preliminary = part.preliminary;
  return c;
}

/**
 * @brief Divide `value` by `divisor` rounding to nearest, ties away from zero.
 * @param value The dividend.
 * @param divisor The divisor; positive.
 * @return The rounded quotient.
 */
constexpr int64_t DivideRounded(int64_t value, int64_t divisor) noexcept {
  const int64_t half = divisor / 2;
  return (value >= 0) ? ((value + half) / divisor) : ((value - half) / divisor);
}

/**
 * @brief A raw flow in millilitres per minute: `(raw - offset) * 1000 * den /
 * num`, rounded.
 * @param raw The received flow word (16-bit signed or unsigned, or 24-bit
 * unsigned).
 * @param scaling The gas's scaling.
 * @return The flow in ml/min; the intermediate is 64-bit, so no input
 * overflows.
 */
constexpr int32_t FlowMlPerMin(int32_t raw, const Scaling& scaling) noexcept {
  const int64_t diff = static_cast<int64_t>(raw) - static_cast<int64_t>(scaling.offset);
  const int64_t scaled = diff * int64_t{1000} * static_cast<int64_t>(scaling.den);
  return static_cast<int32_t>(DivideRounded(scaled, static_cast<int64_t>(scaling.num)));
}

/**
 * @brief A raw temperature in milli-degrees Celsius: `(raw - offset) * 1000 /
 * scale`, rounded.
 * @param raw The received temperature word.
 * @param row The part's temperature scaling; `scale` must not be 0.
 * @return The temperature in 1/1000 degC; 0 when the part has no temperature
 * scaling.
 */
constexpr int32_t TemperatureMdegC(int32_t raw, const TempRow& row) noexcept {
  if (row.scale == 0) {
    return 0;
  }
  const int64_t diff = static_cast<int64_t>(raw) - static_cast<int64_t>(row.offset);
  return static_cast<int32_t>(DivideRounded(diff * int64_t{1000}, static_cast<int64_t>(row.scale)));
}

/**
 * @brief Check a configuration against the part it names, without touching the
 * chip.
 *
 * Fails when an enumeration holds a value that is not an enumerator, the family
 * is not the part's, the address is not in the part's set, `Auto` is combined
 * with `TimingPolicy::Datasheet`, averaging or the filter is used on a part
 * without that feature, averaging is out of range for the part, or the error
 * limit is 0.
 *
 * @param config The configuration.
 * @return `Error::Ok` or `Error::InvalidArgument`.
 */
constexpr Error Validate(const Config& config) noexcept;

/**
 * @brief A part's datasheet defaults as a configuration.
 * @param variant The part.
 * @return `Config{}` with the variant, family and default address of the row;
 * `Config{}` for `Auto`.
 */
constexpr Config PresetFor(Variant variant) noexcept {
  Config c{};
  const PartInfo* row = FindPart(variant);
  if (row != nullptr) {
    c.variant = variant;
    c.family = row->family;
    c.address = row->addresses.list[0];
  }
  return c;
}

namespace detail {
/// Averaging value limits of the family: the lowest N and whether N = 0 is
/// allowed.
constexpr bool AveragingInRange(const PartInfo& part, uint16_t n) noexcept {
  if (n == kAveragingDefault) {
    return true;
  }
  if (!Has(part, feature::kAveraging)) {
    return false;
  }
  if (n == 0U) {
    return Has(part, feature::kAveragingZero);
  }
  return n <= 128U;
}

/// Whether an address is in the part's set.
constexpr bool AddressAllowed(const PartInfo& part, uint8_t address) noexcept {
  for (uint8_t i = 0U; i < part.addresses.count; ++i) {
    if (part.addresses.list[i] == address) {
      return true;
    }
  }
  return false;
}

/// Whether some row of the family uses the address (what `Variant::Auto`
/// accepts).
constexpr bool FamilyAddressAllowed(Family family, uint8_t address) noexcept {
  return std::ranges::any_of(parts::kTable, [family, address](const PartInfo& row) {
    return (row.family == family) && AddressAllowed(row, address);
  });
}

/// The enumeration and scalar checks that need no table row.
constexpr bool ConfigScalarsValid(const Config& c) noexcept {
  return IsEnumerator(c.variant) && IsEnumerator(c.family) && IsEnumerator(c.scaling_check) &&
         IsEnumerator(c.range_policy) && IsEnumerator(c.timing) &&
         (c.consecutive_error_limit != 0U) && (c.range_margin_permille <= 1000U);
}
} // namespace detail

namespace detail {
/// `Validate()` for `Variant::Auto`: the family's first row stands in for the
/// part; its waits are the conservative ones, and averaging and the filter are
/// checked again after identification.
constexpr Error ValidateAuto(const Config& config) noexcept {
  const PartInfo* part = FamilyRow(config.family);
  if ((part == nullptr) || (config.timing == TimingPolicy::Datasheet) ||
      ((config.address != 0U) && !FamilyAddressAllowed(config.family, config.address))) {
    return Error::InvalidArgument;
  }
  return ((config.family == Family::Sf05) || (config.family == Family::Sf04))
             ? Error::InvalidArgument
             : Error::Ok;
}
} // namespace detail

constexpr Error Validate(const Config& config) noexcept {
  if (!detail::ConfigScalarsValid(config)) {
    return Error::InvalidArgument;
  }
  const PartInfo* part = FindPart(config.variant);
  if (part == nullptr) {
    return detail::ValidateAuto(config);
  }
  // A variant names its own family; the default family field is ignored.
  const bool family_ok = (part->family == config.family) || (config.family == Family::Sf06);
  const bool address_ok = (config.address == 0U) || detail::AddressAllowed(*part, config.address);
  const bool options_ok = detail::AveragingInRange(*part, config.averaging) &&
                          (!config.filter_enabled || Has(*part, feature::kFilterArg));
  return (family_ok && address_ok && options_ok) ? Error::Ok : Error::InvalidArgument;
}

// Config{} and every preset are valid, proved at build time (KB-CPP20-012).
static_assert(Validate(Config{}) == Error::Ok);

/**
 * @brief Reached only when a `CheckedConfig` is built from an invalid
 * configuration: it is never defined, so the build fails and names it
 * (KB-CPP20-013).
 */
void ConfigIsInvalid() noexcept;

/**
 * @brief A configuration proven valid at build time.
 *
 * `constexpr CheckedConfig kBoard{PresetFor(Variant::SFM3003_300_CL)};` fails
 * to compile, naming `ConfigIsInvalid`, when `Validate()` rejects the
 * configuration. Pass `kBoard.Get()` to the driver; the driver still validates
 * at start-up, for configurations built at run time.
 */
class CheckedConfig {
public:
  /**
   * @brief Check `config` at build time.
   * @param config The configuration; it must pass `Validate()`.
   */
  consteval explicit CheckedConfig(const Config& config) noexcept : config_(config) {
    if (Validate(config) != Error::Ok) {
      ConfigIsInvalid();
    }
  }
  /** @return The checked configuration. */
  [[nodiscard]] constexpr const Config& Get() const& noexcept {
    return config_;
  }
  /** @brief Not on a temporary: the reference would dangle (KB-CPP20-044). */
  void Get() const&& = delete;

private:
  Config config_;
};

namespace detail {

/// One row is consistent: its default address is allowed, its scales and codes
/// are non-zero and its feature flags fit its family.
constexpr bool RowIsConsistent(const PartInfo& row) noexcept {
  bool ok = (row.addresses.count > 0U) && (row.gases.count > 0U) && (row.timing.power_up_us > 0U) &&
            ((row.crc_init == 0xFFU) || (row.crc_init == 0x00U)) &&
            (row.span_min_mlpm < row.span_max_mlpm);
  for (uint8_t g = 0U; g < row.gases.count; ++g) {
    const GasRow& gas = row.gases.rows[g];
    ok = ok && (gas.code != 0U) && (gas.scaling.num != 0U) && (gas.scaling.den != 0U);
  }
  // A clock-stretching read exists only on the SF04 family, the first-byte rule
  // only on SF05.
  ok = ok && (Has(row, feature::kClockStretch) == (row.family == Family::Sf04));
  ok = ok && (Has(row, feature::kFirstByteAck) == (row.family == Family::Sf05));
  return ok && (row.preliminary == (row.family == Family::Sfx6000));
}

/// Every row's variant is its position in the table, each row is consistent,
/// and no two rows of a family share an identifier (walked at build time).
constexpr bool TableIsConsistent() noexcept {
  for (std::size_t i = 0U; i < parts::kCount; ++i) {
    const PartInfo& row = parts::kTable[i];
    bool ok =
        (static_cast<std::size_t>(ToUnderlying(row.variant)) == (i + 1U)) && RowIsConsistent(row);
    for (std::size_t j = i + 1U; j < parts::kCount; ++j) {
      const PartInfo& other = parts::kTable[j];
      ok = ok && ((row.family != other.family) || (row.id_mask == 0U) ||
                  (row.id_value != other.id_value));
    }
    if (!ok) {
      return false;
    }
  }
  return true;
}
} // namespace detail

static_assert(detail::TableIsConsistent());

// One row per family pinned against hand-typed datasheet values: a mistyped
// digit fails the build.
static_assert(FindPart(Variant::SFM4300_20_B)->gases.rows[0].scaling.num == 2500U);
static_assert(FindPart(Variant::SFM4300_20_B)->addresses.list[0] == 0x2AU);
static_assert(FindPart(Variant::SFM3003_300_CE)->gases.rows[1].scaling.offset == -12288);
static_assert(FindPart(Variant::SFM3304_250_D)->addresses.list[0] == 0x2EU);
static_assert(FindPart(Variant::SFM3505_300)->gases.rows[0].scaling.offset == 0x800000);
static_assert(FindPart(Variant::SFM3000_200_C)->gases.rows[2].scaling.num == 1428U);
static_assert(FindPart(Variant::SFM4100_AR)->addresses.list[0] == 5U);
static_assert(FindPart(Variant::SFM6000D_5)->gases.rows[2].scaling.num == 25600U);
// The start code is data: 0x3615 is nitrous oxide, heliox or carbon dioxide
// depending on the part.
static_assert(FindGas(*FindPart(Variant::SFM4300_20_P), Gas::N2O)->code == 0x3615U);
static_assert(FindGas(*FindPart(Variant::SFM3013_300_CLM), Gas::HeOx)->code == 0x3615U);
static_assert(FindGas(*FindPart(Variant::SFM6000D_50), Gas::CO2)->code == 0x3615U);
static_assert(FindGas(*FindPart(Variant::SFM4300_50_B), Gas::CO2) == nullptr);
// The conversions at the documented extremes (SFM4300-20: -1.64 to +24.58 slm
// raw span).
static_assert(FlowMlPerMin(-32768, FindPart(Variant::SFM4300_20_B)->gases.rows[1].scaling) ==
              -1638);
static_assert(FlowMlPerMin(32767, FindPart(Variant::SFM4300_20_B)->gases.rows[1].scaling) == 24576);
static_assert(FlowMlPerMin(32000, FindPart(Variant::SFM3000_200_C)->gases.rows[2].scaling) == 0);
static_assert(TemperatureMdegC(20000, FindPart(Variant::SFM3200_AW)->temperature) == 0);
namespace detail {
static_assert(FindById(Family::Sf06, 0x04030100_u32, 0x2A_u8) == FindPart(Variant::SFM4300_20_B));
// Another family's identifier never matches (a controller product is refused
// the same way).
static_assert(FindById(Family::Sf06, 0x06021184_u32, 0x24_u8) == nullptr);
} // namespace detail

} // namespace sfm

#endif // SFM_FLOW_METER_PARTS_HPP
