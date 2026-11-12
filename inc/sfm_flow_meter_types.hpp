/**
 * @file sfm_flow_meter_types.hpp
 * @brief Public types of the SFM flow meter driver: errors, `Result<T>`, the
 * families, variants and gases, the `Sample`, and `Config`.
 * @copyright Copyright (c) 2026 The driver-sfm-flow-meter authors.
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Everything here is plain data with no platform dependency, so handlers, tests
 * and tools can include it without the driver. `Config{}` selects
 * `Variant::Auto` in the SF06 family and the chip's reset behaviour. The table
 * of parts and `Validate()` are in `sfm_flow_meter_parts.hpp`.
 */
#ifndef SFM_FLOW_METER_TYPES_HPP
#define SFM_FLOW_METER_TYPES_HPP

#include <array>
#include <cstdint>

#include "sfm_flow_meter_detail.hpp"
#include "sfm_flow_meter_version.h"

/** @brief The SFM flow meter driver: everything it declares lives here;
 * internals in `detail`. */
namespace sfm {

/**
 * @defgroup sfm_flow_meter_types Driver types
 * @brief Errors, results, families, variants, samples and configuration.
 * @{
 */

/**
 * @brief Why a call failed. Every call of the driver returns one of these in
 * its `Result`.
 *
 * Values 0 to 9 are the shared base set; chip-specific values start at 16.
 * `[[nodiscard]]` on the type: a function that returns a bare `Error` cannot
 * have it ignored either (KB-CPP20-037).
 */
enum class [[nodiscard]] Error : uint8_t {
  // Base set: the same names and values in every driver of the org
  // (standards/driver.md).
  Ok = 0,               ///< The call succeeded.
  NotInitialized = 1,   ///< `EnsureInitialized()` has not succeeded, or
                        ///< `Deinitialize()` ran since.
  InvalidArgument = 2,  ///< An argument or a `Config` field is out of range; nothing was sent.
  Bus = 3,              ///< A write hook returned `false`.
  Timeout = 4,          ///< A bounded wait or poll ended: wake, reset, stretched read.
  DeviceIdMismatch = 5, ///< The product identifier is not the configured part
                        ///< (or a prototype).
  Crc = 6,              ///< A received word failed its CRC-8; the whole transfer was
                        ///< discarded.
  Busy = 7,             ///< The call is not allowed in the current mode, or a wait is pending.
  DeviceFault = 8,      ///< The scaling the sensor reports differs from the part's
                        ///< datasheet value.
  Unsupported = 9,      ///< The part, the port hook or the family mask does not
                        ///< offer this call.
  // Chip-specific values start at 16; each one is returned by some call
  // (tests/test_errors.cpp).
  NoData = 16,         ///< The read header was NACKed: no new sample yet (or the sensor
                       ///< is idle).
  NoDevice = 17,       ///< The address is not acknowledged on a write, a poll or
                       ///< initialisation.
  OutOfRange = 18,     ///< A flow or temperature outside the part's span plus margin.
  StatusMismatch = 19, ///< The status word does not describe the measurement
                       ///< the driver started.
  Stale = 20,          ///< The sample may not be fresh enough (settling, smoothing or age).
  DeviceReset = 21,    ///< The measurement is over (the sensor reset, or was
                       ///< silent for the whole
                       ///< bound): idle again.
  StateUnknown = 22,   ///< A failed command left the chip's mode unknown; initialise again.
  InvalidReply = 23,   ///< A CRC-valid reply the sensor cannot send (stuck line,
                       ///< reserved value).
};

/**
 * @brief The name of an error, for logs.
 * @param error The error.
 * @return A static string such as `"Timeout"`; `"Unknown"` for a value that is
 * not an enumerator.
 */
constexpr const char* ToString(Error error) noexcept {
  switch (error) {
  case Error::Ok:
    return "Ok";
  case Error::NotInitialized:
    return "NotInitialized";
  case Error::InvalidArgument:
    return "InvalidArgument";
  case Error::Bus:
    return "Bus";
  case Error::Timeout:
    return "Timeout";
  case Error::DeviceIdMismatch:
    return "DeviceIdMismatch";
  case Error::Crc:
    return "Crc";
  case Error::Busy:
    return "Busy";
  case Error::DeviceFault:
    return "DeviceFault";
  case Error::Unsupported:
    return "Unsupported";
  case Error::NoData:
    return "NoData";
  case Error::NoDevice:
    return "NoDevice";
  case Error::OutOfRange:
    return "OutOfRange";
  case Error::StatusMismatch:
    return "StatusMismatch";
  case Error::Stale:
    return "Stale";
  case Error::DeviceReset:
    return "DeviceReset";
  case Error::StateUnknown:
    return "StateUnknown";
  case Error::InvalidReply:
    return "InvalidReply";
  default:
    break;
  }
  return "Unknown";
}

/**
 * @brief The value of a call, or why there is none.
 *
 * Check `ok()` (or the `bool` conversion) before reading `value`; on failure
 * `value` is `T{}`.
 *
 * @tparam T The value type. `Result<void>` carries only the error.
 */
template <typename T>
struct [[nodiscard]] Result {
  T value{};              ///< The value; `T{}` when the call failed.
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /**
   * @brief A successful result.
   * @param v The value.
   * @return `{v, Error::Ok}`.
   */
  static constexpr Result Success(T v) noexcept {
    return {v, Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{T{}, e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {T{}, e};
  }
};

/** @brief The result of a call that returns no value. */
template <>
struct [[nodiscard]] Result<void> {
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /** @return A successful result. */
  static constexpr Result Success() noexcept {
    return {Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {e};
  }
};

/** @brief Severity passed to the optional `Log()` hook. */
enum class LogLevel : uint8_t {
  Error = 0, ///< A call failed.
  Warning,   ///< Something unexpected that the driver recovered from.
  Info,      ///< Lifecycle events: initialised, reset.
  Debug,     ///< Detail for bring-up.
};

/**
 * @brief The command-set family of a part. One protocol policy per family;
 * every part is a row of data that names its family.
 */
enum class Family : uint8_t {
  Sf06 = 0, ///< SFM4300, SFM3003, SFM3013, SFM3019, SFM3119: 16-bit commands,
            ///< 9-byte frame.
  Sf06Gen2, ///< SFM3304: SF06 framing, one gas, optional filter argument,
            ///< status with averaging.
  Sf06Dual, ///< SFM3505: two 24-bit flows (air and oxygen) in one frame.
  Sf05,     ///< SFM3000, SFM3200, SFM3300, SFM3400, SFM4200: pointer registers, 5
            ///< V.
  Sf04,     ///< SFM4100: 8-bit commands, clock-stretching read, one gas per
            ///< address.
  Sfx6000,  ///< SFM6000D: preliminary vendor protocol (every fact tagged `[P]`
            ///< in the docs).
};

/**
 * @brief A bit in a family mask: `SfmFlowMeter<Port, kFamilySf06 |
 * kFamilySf05>`.
 * @param f The family.
 * @return The mask bit of `f`.
 */
constexpr uint32_t FamilyBit(Family f) noexcept {
  return uint32_t{1} << (static_cast<uint32_t>(f) & 0x1FU);
}
/// Family mask bits.
inline constexpr uint32_t kFamilySf06 = 1U << 0;     ///< `Family::Sf06`.
inline constexpr uint32_t kFamilySf06Gen2 = 1U << 1; ///< `Family::Sf06Gen2`.
inline constexpr uint32_t kFamilySf06Dual = 1U << 2; ///< `Family::Sf06Dual`.
inline constexpr uint32_t kFamilySf05 = 1U << 3;     ///< `Family::Sf05`.
inline constexpr uint32_t kFamilySf04 = 1U << 4;     ///< `Family::Sf04`.
inline constexpr uint32_t kFamilySfx6000 = 1U << 5;  ///< `Family::Sfx6000`.
/// Every family: the default mask.
inline constexpr uint32_t kAllFamilies = 0x3FU;

/**
 * @brief A part (one row of the table in `sfm_flow_meter_parts.hpp`). The names
 * follow the vendor's ordering suffixes; `_B`, `_O` and `_P` are the SFM4300
 * fittings.
 */
enum class Variant : uint8_t {
  Auto = 0,        ///< Identify the part by its product identifier inside the
                   ///< configured family.
  SFM4300_20_B,    ///< SFM4300-20-B, 0 to 20 slm, base-mount flanges.
  SFM4300_20_O,    ///< SFM4300-20-O, O-ring connection.
  SFM4300_20_P,    ///< SFM4300-20-P, push-in fittings.
  SFM4300_50_B,    ///< SFM4300-50-B, 0 to 50 slm.
  SFM4300_50_O,    ///< SFM4300-50-O.
  SFM4300_50_P,    ///< SFM4300-50-P.
  SFM3003_300_CL,  ///< SFM3003-300-CL, -30 to 300 slm, address 0x28.
  SFM3003_300_CE,  ///< SFM3003-300-CE, -150 to 300 slm, address 0x2D.
  SFM3003_300_CET, ///< SFM3003-300-CET, with a gas temperature sensor, address
                   ///< 0x2A.
  SFM3013_300_CL,  ///< SFM3013-300-CL, address 0x2F.
  SFM3013_300_CLM, ///< SFM3013-300-CLM, adds heliox, address 0x2F.
  SFM3019_200_CL,  ///< SFM3019-200-CL, address 0x2E.
  SFM3119_240_CL,  ///< SFM3119-240-CL, address 0x29.
  SFM3304_250_D,   ///< SFM3304-250-D, -250 to 250 slm, 3.3 V, address 0x2E.
  SFM3505_300,     ///< SFM3505-300, air and oxygen channels.
  SFM3505_300_X,   ///< SFM3505-300-X.
  SFM3000_200_C,   ///< SFM3000-200-C, 5 V, address 0x40, legacy.
  SFM3200,         ///< SFM3200, 5 V, legacy.
  SFM3200_AW,      ///< SFM3200-AW, with temperature output.
  SFM3300_D,       ///< SFM3300-D.
  SFM3300_AW,      ///< SFM3300-AW.
  SFM3400_D,       ///< SFM3400-D, -33 to 33 slm.
  SFM3400_AW,      ///< SFM3400-AW.
  SFM4200,         ///< SFM4200, 0 to 160 slm, /RES pin, 100 kHz, legacy.
  SFM4100_AIR,     ///< SFM4100-Air, address 1, clock stretching, legacy.
  SFM4100_O2,      ///< SFM4100-O2, address 2.
  SFM4100_CO2,     ///< SFM4100-CO2, address 3.
  SFM4100_N2O,     ///< SFM4100-N2O, address 4.
  SFM4100_AR,      ///< SFM4100-Ar, address 5.
  SFM6000D_50,     ///< SFM6000D-50, preliminary protocol.
  SFM6000D_20,     ///< SFM6000D-20, preliminary protocol.
  SFM6000D_5,      ///< SFM6000D-5, preliminary protocol.
};

/** @brief A calibrated gas or gas mixture. Which gases a part offers is in its
 * table row. */
enum class Gas : uint8_t {
  Air = 0, ///< Air.
  O2,      ///< Oxygen.
  N2,      ///< Nitrogen (every SF05 part, at the air scale).
  N2O,     ///< Nitrous oxide.
  CO2,     ///< Carbon dioxide.
  Ar,      ///< Argon.
  HeOx,    ///< Heliox 80/20 (SFM3013-300-CLM).
  AirO2,   ///< Mixture of air and oxygen; `o2_permille` is the oxygen fraction.
  N2OO2,   ///< Mixture of nitrous oxide and oxygen.
  CO2O2,   ///< Mixture of carbon dioxide and oxygen.
  HeOxO2,  ///< Mixture of heliox and oxygen.
};

/** @brief What `ReadScaling()` does when the sensor's scale or offset differs
 * from the datasheet.
 */
enum class ScalingCheck : uint8_t {
  Strict = 0, ///< Any difference or a wrong unit is `DeviceFault`.
  Lenient,    ///< A scale within 1 % of the table is accepted; offset and unit
              ///< still checked.
  Off,        ///< The sensor's values are used; only scale 0 and the unit are checked.
};

/** @brief What `ReadSample()` does with a flow or temperature outside the
 * part's span. */
enum class RangePolicy : uint8_t {
  Reject = 0, ///< `OutOfRange`, no value.
  Flag,       ///< The value is returned with `Sample::out_of_range` set; never
              ///< clamped.
};

/** @brief Which waits the driver uses after a reset or a wake. */
enum class TimingPolicy : uint8_t {
  Conservative = 0, ///< At least 20 ms for every SF06 reset and wake (the
                    ///< longest in the family).
  Datasheet,        ///< The part's own maxima; needs a known variant.
};

/** @brief The driver's shadow of the chip's mode. */
enum class State : uint8_t {
  Uninitialized = 0, ///< `EnsureInitialized()` has not succeeded.
  Idle,              ///< Initialised, not measuring.
  Measuring,         ///< A measurement is running; `ReadSample()` is legal.
  Asleep,            ///< In sleep mode; only `WakeUp()` is legal.
  StateUnknown,      ///< A failed command left the mode unknown; only initialisation
                     ///< is legal.
};

/** @brief A wait the caller owes the driver: delay `us`, then call `Poll()`. 0
 * means nothing to wait. */
struct Wait {
  uint32_t us{0}; ///< Microseconds to wait.
};

/** @brief The product identifier read at initialisation. */
struct ProductInfo {
  uint32_t product_id{0};         ///< 32-bit product and revision number; 0 for SF05 and SF04.
  uint32_t serial{0};             ///< Serial number (low 32 bits); SF05 and SF04 report all of it.
  uint8_t revision{0};            ///< Low byte of the product identifier.
  Variant variant{Variant::Auto}; ///< The part that matched.
  bool prototype{false};          ///< The revision byte marks a prototype (high nibble 8).
  bool preliminary{false};        ///< The part's protocol document is marked preliminary.
};

/** @brief What the sensor reports about the scaling of a gas, and whether it
 * matches the table. */
struct ScalingInfo {
  int32_t scale{0};          ///< Counts per slm (counts per unit for SF05).
  int32_t offset{0};         ///< Counts.
  uint16_t unit{0};          ///< The 16-bit flow-unit word (0 when the family does not
                             ///< report one).
  bool matches_table{false}; ///< The values equal the datasheet's (within `ScalingCheck`).
};

/** @brief Which calls a part offers, from its table row. */
struct Capabilities {
  bool scale_read{false};           ///< `ReadScaling()` works.
  bool averaging{false};            ///< `SetAveraging()` works.
  bool concentration{false};        ///< `UpdateConcentration()` works.
  bool sleep{false};                ///< `Sleep()` and `WakeUp()` work.
  bool temperature_in_frame{false}; ///< `Sample::temperature_mdegc` is filled.
  bool temperature_read{false};     ///< `ReadTemperature()` works.
  bool status_word{false};          ///< `Sample::status` is filled.
  bool general_call_reset{false};   ///< `SoftReset()` is a general-call reset.
  bool soft_reset_command{false};   ///< `SoftReset()` is a device command.
  bool hard_reset{false};           ///< `HardReset()` is offered (needs a port hook).
  bool filter{false};               ///< `Config::filter` applies.
  bool two_flows{false};            ///< `Sample::flow2_ml_per_min` is filled.
  bool clock_stretch{false};        ///< The port must provide `ReadStretch()`.
  bool preliminary{false};          ///< The part's protocol document is preliminary.
};

/** @brief One measurement. Flow is in millilitres per minute at the part's
 * reference conditions. */
struct Sample {
  int32_t flow_raw{0};          ///< As received, after the CRC: 16, 24 bits, or 14 bits in 16.
  int32_t flow_ml_per_min{0};   ///< Round-to-nearest of `(raw - offset) * 1000 / scale`.
  int32_t flow2_ml_per_min{0};  ///< `Sf06Dual`: the oxygen channel; 0 otherwise.
  int32_t temperature_mdegc{0}; ///< Milli-degrees Celsius of the temperature the frame carries; 0
                                ///< unless `has_temperature`.
  uint32_t timestamp_us{0};     ///< `NowUs()` at the read; 0 without a clock.
  uint16_t status{0};           ///< The raw status word; 0 unless `has_status`.
  uint16_t o2_permille{0};      ///< Oxygen fraction of a mixture, in per mille.
  Gas gas{Gas::Air};            ///< The running gas.
  bool pure_gas{true};          ///< The running measurement is a pure gas.
  bool settled{false};          ///< The warm-up time has passed (false without a clock).
  bool smoothed{false};         ///< Status bit 11: exponential smoothing took over.
  bool has_temperature{false};  ///< `temperature_mdegc` is valid.
  bool has_flow2{false};        ///< `flow2_ml_per_min` is valid.
  bool has_status{false};       ///< `status` is valid.
  bool out_of_range{false};     ///< `RangePolicy::Flag` only: the flow is outside
                                ///< the part's span.
};

/**
 * @brief The words of one reply, after their CRCs were checked and before
 * anything was decoded.
 *
 * For diagnostics and bring-up: it carries no scaling, status check or range
 * check. A reply is at most six words (the product identifier); a sample frame
 * has one to three.
 */
struct RawReply {
  std::array<uint16_t, 6> words{}; ///< The words in order; zero beyond `count`.
  uint8_t count{0};                ///< Words in use; 0 before the first reply.
};

/** @brief Counters the driver keeps; none of them is cleared by a successful
 * call. */
struct Stats {
  uint32_t crc_errors{0};         ///< Received words that failed their CRC.
  uint32_t no_data{0};            ///< Reads whose header was NACKed.
  uint32_t bus_errors{0};         ///< Writes that failed.
  uint32_t device_resets{0};      ///< `DeviceReset` results.
  uint32_t consecutive_errors{0}; ///< SF05 rule: errors since the last good sample.
  uint32_t power_cycles{0};       ///< Hard resets requested through the port.
};

/** @brief Marks `Config::averaging` as "the family's reset value". */
inline constexpr uint16_t kAveragingDefault = 0xFFFFU;
/** @brief The filter argument of the default 3 ms response (SFM3304, SFM3505).
 */
inline constexpr uint16_t kFilterDefault = 50961U;

/**
 * @brief Everything `EnsureInitialized()` applies.
 *
 * `Config{}` is valid: automatic identification in the SF06 family, the
 * datasheet reset behaviour, strict scaling checks and conservative timing. It
 * has no `operator==`: nothing here is compared as a whole.
 */
struct Config {
  Variant variant{Variant::Auto};        ///< The part; `Auto` identifies it inside `family`.
  Family family{Family::Sf06};           ///< Used with `Auto` only; never crosses families.
  uint8_t address{0};                    ///< 7-bit address; 0 is the part's default.
  uint16_t averaging{kAveragingDefault}; ///< N (0 average-until-read, 1..128) or the default.
  bool filter_enabled{false};            ///< Send the `filter` argument with the start command.
  uint16_t filter{kFilterDefault};       ///< The 16-bit signal-filter argument.
  ScalingCheck scaling_check{ScalingCheck::Strict}; ///< See `ScalingCheck`.
  RangePolicy range_policy{RangePolicy::Reject};    ///< See `RangePolicy`.
  uint16_t range_margin_permille{20}; ///< Of the span, added at both ends before `OutOfRange`.
  bool require_settled{false};  ///< A sample before the warm-up time is `Stale` (needs a clock).
  bool reject_smoothed{false};  ///< A smoothed sample is `Stale`.
  uint32_t max_age_us{0};       ///< 0 off; else a read later than this after the
                                ///< previous good one is `Stale`.
  bool accept_prototype{false}; ///< A prototype product identifier is accepted.
  bool allow_general_call_reset{false};            ///< Permit the general-call reset (it
                                                   ///< resets every device that listens).
  TimingPolicy timing{TimingPolicy::Conservative}; ///< See `TimingPolicy`.
  uint8_t consecutive_error_limit{
      5};                         ///< SF05: errors in a row before a hard reset or `StateUnknown`.
  bool restart_before_read{true}; ///< SF05: send the start command again after each read.
  bool poll_with_identifier{false}; ///< SF06: idle polls send the identifier command, not stop
                                    ///< (ambiguity K-STOPIDLE).
  bool sf05_serial_low_word{true};  ///< SF05: also read the low serial word through 0x31AF
                                    ///< (ambiguity K-SF05-SERIAL).
};

namespace detail {
/// Last enumerator of `Family` (detail::IsEnumerator).
template <>
struct EnumLast<Family> {
  static constexpr Family kValue = Family::Sfx6000; ///< The last enumerator.
};
/// Last enumerator of `Variant`.
template <>
struct EnumLast<Variant> {
  static constexpr Variant kValue = Variant::SFM6000D_5; ///< The last enumerator.
};
/// Last enumerator of `Gas`.
template <>
struct EnumLast<Gas> {
  static constexpr Gas kValue = Gas::HeOxO2; ///< The last enumerator.
};
/// Last enumerator of `ScalingCheck`.
template <>
struct EnumLast<ScalingCheck> {
  static constexpr ScalingCheck kValue = ScalingCheck::Off; ///< The last enumerator.
};
/// Last enumerator of `RangePolicy`.
template <>
struct EnumLast<RangePolicy> {
  static constexpr RangePolicy kValue = RangePolicy::Flag; ///< The last enumerator.
};
/// Last enumerator of `TimingPolicy`.
template <>
struct EnumLast<TimingPolicy> {
  static constexpr TimingPolicy kValue = TimingPolicy::Datasheet; ///< The last enumerator.
};
} // namespace detail

/** @return The driver version, e.g. `"0.1.0"`, from the generated
 * `sfm_flow_meter_version.h`. No I/O. */
constexpr const char* GetDriverVersion() noexcept {
  return SFM_FLOW_METER_VERSION_STRING;
}
/** @return The major version. No I/O. */
constexpr uint32_t GetDriverVersionMajor() noexcept {
  return SFM_FLOW_METER_VERSION_MAJOR;
}
/** @return The minor version. No I/O. */
constexpr uint32_t GetDriverVersionMinor() noexcept {
  return SFM_FLOW_METER_VERSION_MINOR;
}
/** @return The patch version. No I/O. */
constexpr uint32_t GetDriverVersionPatch() noexcept {
  return SFM_FLOW_METER_VERSION_PATCH;
}

/** @} */

} // namespace sfm

#endif // SFM_FLOW_METER_TYPES_HPP
