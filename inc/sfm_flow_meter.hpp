/**
 * @file sfm_flow_meter.hpp
 * @brief SFM flow meter driver: the one header to include.
 * @copyright Copyright (c) 2026 The driver-sfm-flow-meter authors.
 * SPDX-License-Identifier: MIT
 *
 * @details
 * `sfm::SfmFlowMeter<Port>` drives one Sensirion SFM mass flow meter (SFM3000
 * to SFM6000D) through a platform port you write (see
 * `sfm_flow_meter_i2c_interface.hpp`). The driver has no platform code,
 * allocates nothing, throws nothing, takes no lock and is not thread-safe: give
 * each object one owner, or guard it with one mutex.
 *
 * Every operation is a bounded plan of bus steps. A plan that has to wait (a
 * reset, a first sample, a wake) returns the wait to you as `Result<Wait>`;
 * delay that long and call `Poll()`. With a `DelayUs` hook, `AwaitPending()`
 * does that for you. `ReadSample()` and `ReadTemperature()` never wait.
 */
#ifndef SFM_FLOW_METER_HPP
#define SFM_FLOW_METER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "sfm_flow_meter_detail.hpp"
#include "sfm_flow_meter_i2c_interface.hpp"
#include "sfm_flow_meter_parts.hpp"
#include "sfm_flow_meter_protocols.hpp"
#include "sfm_flow_meter_registers.hpp"
#include "sfm_flow_meter_types.hpp"
#include "sfm_flow_meter_version.h"

namespace sfm {

namespace detail {

// Optional hooks: a `Declares*` concept only tests the name, the `Has*` concept
// the exact signature, `noexcept` and the result type; the driver rejects a
// hook that is declared but does not match (KB-CPP20-007).

/// The port declares something called `DelayUs`.
template <typename Port>
concept DeclaresDelayUs = requires(Port& port) { port.DelayUs(uint32_t{0}); };
/// The port waits: `void DelayUs(uint32_t us) noexcept`.
template <typename Port>
concept HasDelayUs = requires { ExactHook<Port, void(uint32_t) noexcept>::Take(&Port::DelayUs); };

/// The port declares something called `NowUs`.
template <typename Port>
concept DeclaresNowUs = requires(Port& port) { port.NowUs(); };
/// The port has a free-running clock: `uint32_t NowUs() noexcept`.
template <typename Port>
concept HasNowUs = requires { ExactHook<Port, uint32_t() noexcept>::Take(&Port::NowUs); };

/// The port declares something called `Log`.
template <typename Port>
concept DeclaresLog = requires(Port& port) { port.Log(LogLevel::Info, ""); };
/// The port takes log lines: `void Log(LogLevel, const char*) noexcept`.
template <typename Port>
concept HasLog =
    requires { ExactHook<Port, void(LogLevel, const char*) noexcept>::Take(&Port::Log); };

/// The port declares something called `ReadStretch`.
template <typename Port>
concept DeclaresReadStretch =
    requires(Port& port, uint8_t* data) { port.ReadStretch(uint8_t{0}, data, std::size_t{0}); };
/// The port reads with clock stretching: `bool ReadStretch(uint8_t, uint8_t*,
/// size_t) noexcept` and `static constexpr bool kClockStretch = true`.
template <typename Port>
concept HasReadStretch = requires {
  ExactHook<Port, bool(uint8_t, uint8_t*, std::size_t) noexcept>::Take(&Port::ReadStretch);
} && requires { requires Port::kClockStretch; };

/// The port declares something called `WriteGeneralCall`.
template <typename Port>
concept DeclaresGeneralCall = requires(Port& port) { port.WriteGeneralCall(uint8_t{0}); };
/// The port sends general calls: `bool WriteGeneralCall(uint8_t) noexcept`.
template <typename Port>
concept HasGeneralCall =
    requires { ExactHook<Port, bool(uint8_t) noexcept>::Take(&Port::WriteGeneralCall); };

/// The port declares something called `PowerCycle`.
template <typename Port>
concept DeclaresPowerCycle = requires(Port& port) { port.PowerCycle(); };
/// The port cycles the sensor supply: `bool PowerCycle() noexcept`.
template <typename Port>
concept HasPowerCycle = requires { ExactHook<Port, bool() noexcept>::Take(&Port::PowerCycle); };

/// The port declares something called `SetResetPin`.
template <typename Port>
concept DeclaresResetPin = requires(Port& port) { port.SetResetPin(true); };
/// The port drives /RES: `bool SetResetPin(bool asserted) noexcept`.
template <typename Port>
concept HasResetPin = requires { ExactHook<Port, bool(bool) noexcept>::Take(&Port::SetResetPin); };

/// Wait through the port. A port without `DelayUs()` fails to compile at the
/// first call that needs a wait: the chip's reset and conversion times are
/// requirements, never skipped.
template <typename Port>
void DelayUs(Port& port, uint32_t us) noexcept {
  if constexpr (HasDelayUs<Port>) {
    port.DelayUs(us);
  } else {
    static_assert(kAlwaysFalse<Port>, "sfm_flow_meter: the port must implement "
                                      "`void DelayUs(uint32_t us) noexcept`");
  }
}

/// The port's clock, or 0 when it has none.
template <typename Port>
uint32_t NowUs(Port& port) noexcept {
  if constexpr (HasNowUs<Port>) {
    return port.NowUs();
  } else {
    (void)port;
    return 0U;
  }
}

/// Log through the port, or nothing when the port has no `Log()`. The driver
/// never formats text.
template <typename Port>
void Log(Port& port, [[maybe_unused]] LogLevel level,
         [[maybe_unused]] const char* message) noexcept {
  if constexpr (HasLog<Port>) {
    port.Log(level, message);
  } else {
    (void)port;
  }
}

/// The last phase of an operation (phases are plans whose decoded result
/// decides the next one).
constexpr uint8_t LastPhase(Op op) noexcept {
  switch (op) {
  case Op::Init:
  case Op::ReadScaling:
  case Op::Wake:
    return 1U;
  case Op::Start:
    return 2U;
  case Op::None:
  case Op::Stop:
  case Op::SetAveraging:
  case Op::Sleep:
  case Op::SoftReset:
  case Op::HardReset:
  case Op::ReadSample:
  case Op::ReadTemperature:
  case Op::Concentration:
  default:
    break;
  }
  return 0U;
}

/// Plans take at most this many `AwaitPending()` rounds (10 steps x 255 polls x
/// 3 phases, rounded up).
inline constexpr uint32_t kMaxAwaitRounds = 10000U;

} // namespace detail

/**
 * @brief The driver.
 *
 * @tparam Port Your port class, derived from `I2cInterface<Port>`.
 * @tparam FamilyMask The protocol families the object may drive (`kFamilySf06 |
 * ...`); a configuration outside the mask is `Unsupported`. The default is
 * every family.
 */
template <I2cPort Port, uint32_t FamilyMask = kAllFamilies>
class SfmFlowMeter {
  static_assert((FamilyMask != 0U) && ((FamilyMask & ~kAllFamilies) == 0U),
                "sfm_flow_meter: bad family mask");
  static_assert(!detail::DeclaresDelayUs<Port> || detail::HasDelayUs<Port>,
                "sfm_flow_meter: `DelayUs` must be `void DelayUs(uint32_t) noexcept`");
  static_assert(!detail::DeclaresNowUs<Port> || detail::HasNowUs<Port>,
                "sfm_flow_meter: `NowUs` must be `uint32_t NowUs() noexcept`");
  static_assert(!detail::DeclaresLog<Port> || detail::HasLog<Port>,
                "sfm_flow_meter: `Log` must be `void Log(LogLevel, const "
                "char*) noexcept`");
  static_assert(!detail::DeclaresReadStretch<Port> || detail::HasReadStretch<Port>,
                "sfm_flow_meter: `ReadStretch` must be `bool "
                "ReadStretch(uint8_t, uint8_t*, "
                "size_t) noexcept` "
                "with `static constexpr bool kClockStretch = true`");
  static_assert(!detail::DeclaresGeneralCall<Port> || detail::HasGeneralCall<Port>,
                "sfm_flow_meter: `WriteGeneralCall` must be `bool "
                "WriteGeneralCall(uint8_t) noexcept`");
  static_assert(!detail::DeclaresPowerCycle<Port> || detail::HasPowerCycle<Port>,
                "sfm_flow_meter: `PowerCycle` must be `bool PowerCycle() noexcept`");
  static_assert(!detail::DeclaresResetPin<Port> || detail::HasResetPin<Port>,
                "sfm_flow_meter: `SetResetPin` must be `bool SetResetPin(bool) "
                "noexcept`");
  static_assert(FamilyMask != kFamilySf04 || detail::HasReadStretch<Port>,
                "sfm_flow_meter: the SFM4100 family needs the port's "
                "`ReadStretch()` hook");

public:
  /**
   * @brief Bind a port and a configuration. No bus traffic.
   * @param port The platform port; it must outlive the driver.
   * @param config The configuration; checked by `EnsureInitialized()` and
   * `Validate()`.
   */
  explicit SfmFlowMeter(Port& port, const Config& config = Config{}) noexcept
      : port_(port), config_(config),
        part_((FindPart(config.variant) != nullptr) ? FindPart(config.variant)
                                                    : FamilyRow(config.family)),
        address_(((config.address != 0U) || (part_ == nullptr)) ? config.address
                                                                : part_->addresses.list[0]),
        auto_(config.variant == Variant::Auto) {}
  /** @brief The driver is not copied: it holds a reference to the port. */
  SfmFlowMeter(const SfmFlowMeter&) = delete;
  /** @brief The driver is not copied. */
  SfmFlowMeter& operator=(const SfmFlowMeter&) = delete;
  ~SfmFlowMeter() = default;

  /// @name Lifecycle
  /// @{

  /**
   * @brief Start identification: wake the sensor, check the product identifier
   * (or serial number), set the averaging. Returns the first wait; finish with
   * `Poll()` or `AwaitPending()`.
   * @return The wait (0 when done), `InvalidArgument`, `Unsupported`, `Bus`
   * (the port could not bring the bus up), `Busy`, or a plan error: `NoDevice`,
   * `Crc`, `DeviceIdMismatch`, `InvalidReply`.
   */
  Result<Wait> BeginInitialize() noexcept;

  /**
   * @brief `BeginInitialize()` and `AwaitPending()`: blocking, needs the port's
   * `DelayUs()`.
   * @return `Ok`, or the error of either call. Returns at once when already
   * initialised.
   */
  Result<void> EnsureInitialized() noexcept;

  /**
   * @brief Forget the initialisation. No bus traffic: call `Stop()` first if
   * the sensor measures.
   * @return `Ok`.
   */
  Result<void> Deinitialize() noexcept;

  /// @}
  /// @name Operations that may wait
  /// @{

  /**
   * @brief Start a measurement of a gas or gas mixture; reads and checks the
   * part's scaling first.
   * @param gas A gas of the part's table row.
   * @param o2_permille Mixtures: the oxygen fraction, 0 to 1000; 0 otherwise.
   * @return The wait until the first sample (0 when none), `NotInitialized`,
   * `Busy`, `StateUnknown`, `InvalidArgument`, `DeviceFault` (scaling differs),
   * or a plan error.
   */
  Result<Wait> StartMeasurement(Gas gas, uint16_t o2_permille = 0) noexcept;

  /**
   * @brief Stop the measurement. A no-op when idle. Failure leaves
   * `StateUnknown`.
   * @return The wait (0 when done), `NotInitialized`, `Busy`, `StateUnknown`,
   * or a plan error.
   */
  Result<Wait> Stop() noexcept;

  /**
   * @brief Set the averaging while idle.
   * @param n 0 for average-until-read where offered, 1 to 128, or
   * `kAveragingDefault`.
   * @return The wait (0 when done), `Unsupported`, `InvalidArgument`, `Busy`,
   * or a plan error.
   */
  Result<Wait> SetAveraging(uint16_t n) noexcept;

  /**
   * @brief Read and check the sensor's scaling of a gas while idle; see
   * `GetScaling()`.
   * @param gas A gas of the part's table row.
   * @return The wait (0 when done), `Unsupported`, `InvalidArgument`, `Crc`,
   * `DeviceFault`, or a plan error.
   */
  Result<Wait> ReadScaling(Gas gas) noexcept;

  /**
   * @brief Enter sleep mode (idle only). Failure leaves `StateUnknown`.
   * @return The wait (0 when done), `Unsupported`, `Busy`, or a plan error.
   */
  Result<Wait> Sleep() noexcept;

  /**
   * @brief Wake from sleep and write the averaging again.
   * @return The wait (0 when done), `Busy`, `Timeout` (the sensor stays
   * asleep), or a plan error.
   */
  Result<Wait> WakeUp() noexcept;

  /**
   * @brief Reset by the part's soft-reset command (the general call needs
   * `allow_general_call_reset`). Afterwards the driver is `Uninitialized`: call
   * `EnsureInitialized()` again.
   * @return The wait (0 when done), `Unsupported`, `Busy`, or a plan error.
   */
  Result<Wait> SoftReset() noexcept;

  /**
   * @brief Reset by the /RES pin (SFM4200) or by cycling the supply; needs the
   * port hook.
   * @return The wait (0 when done), `Unsupported`, `Busy`, or a plan error.
   */
  Result<Wait> HardReset() noexcept;

  /**
   * @brief Change the oxygen fraction of a running mixture (no read in
   * between).
   * @param o2_permille The new fraction, 0 to 1000.
   * @return The wait (0 when done), `Unsupported`, `InvalidArgument`, `Busy`,
   * or a plan error.
   */
  Result<Wait> UpdateConcentration(uint16_t o2_permille) noexcept;

  /**
   * @brief Continue a pending operation after the wait it returned.
   * @param elapsed_us How long you waited; less than the wait returns the
   * remainder.
   * @return The next wait (0 when done) or the operation's error.
   */
  Result<Wait> Poll(uint32_t elapsed_us) noexcept;

  /**
   * @brief `Poll()` using the port's `NowUs()`; without a clock the whole wait
   * counts as elapsed.
   * @return As `Poll(uint32_t)`.
   */
  Result<Wait> Poll() noexcept;

  /**
   * @brief Wait with `DelayUs()` until no operation is pending. Needs the
   * port's `DelayUs()`.
   * @return `Ok`, the operation's error, or `Timeout` after an implausible
   * number of rounds.
   */
  Result<void> AwaitPending() noexcept;

  /// @}
  /// @name Measurement
  /// @{

  /**
   * @brief Read one sample of the running measurement.
   * @return The sample, or `NotInitialized`, `Busy`, `StateUnknown`, `NoData`
   * (nothing new yet), `Crc`, `InvalidReply`, `StatusMismatch`, `OutOfRange`,
   * `Stale`, `DeviceReset`, `NoDevice`, `Bus`, `Timeout` (SFM4100 stretch).
   */
  Result<Sample> ReadSample() noexcept;

  /**
   * @brief One raw read of the running measurement: the CRC-checked words, with
   * no decoding, no status check and no range check. The single-shot raw access
   * for bring-up and diagnostics; `GetLastReply()` returns it again without
   * I/O. A failed read does not count towards the SF05 error limit, and a
   * silent sensor is never reported here (`NoData` for ever): only
   * `ReadSample()` reports `DeviceReset`.
   * @return The words, or `NotInitialized`, `Busy`, `StateUnknown`, `NoData`,
   * `Crc`, `InvalidReply`, `Bus`, `Timeout`.
   */
  Result<RawReply> ReadRawReply() noexcept;

  /**
   * @brief Read the sensor temperature by a pointer sequence while measuring
   * (parts that offer it); the SF05 family needs 5 ms between the pointer and
   * the read, so this returns a wait. The value is `GetTemperature()` once the
   * operation ended without an error.
   * @return The wait (0 when done), `Unsupported`, `Busy`, `NotInitialized`,
   * `StateUnknown`, `Bus`, `Crc`. A failure after the pointer moved leaves
   * `StateUnknown`.
   */
  Result<Wait> ReadTemperature() noexcept;

  /// @}
  /// @name State
  /// @{

  /** @return The driver's shadow of the chip's mode. No I/O. */
  [[nodiscard]] State GetState() const noexcept {
    return state_;
  }
  /** @return Whether an operation waits for `Poll()`. */
  [[nodiscard]] bool HasPending() const noexcept {
    return op_ != detail::Op::None;
  }
  /** @return What identification read; zeros before. No I/O. */
  [[nodiscard]] const ProductInfo& GetProductInfo() const noexcept {
    return info_;
  }
  /** @return The last temperature `ReadTemperature()` decoded, in milli-degrees
   * C; 0 before and after a failed call. No I/O. */
  [[nodiscard]] int32_t GetTemperature() const noexcept {
    return temperature_mdegc_;
  }
  /** @return The CRC-valid words of the last frame read by `ReadSample()` or
   * `ReadRawReply()`; empty before. No I/O. */
  [[nodiscard]] const RawReply& GetLastReply() const noexcept {
    return last_reply_;
  }
  /** @return What the last scale read returned. No I/O. */
  [[nodiscard]] const ScalingInfo& GetScaling() const noexcept {
    return scaling_;
  }
  /** @return The counters. No I/O. */
  [[nodiscard]] const Stats& GetStats() const noexcept {
    return stats_;
  }
  /** @return The configuration. */
  [[nodiscard]] const Config& GetConfig() const noexcept {
    return config_;
  }
  /** @return The part's table row; the family's first row for `Auto` before
   * identification; may be null. */
  [[nodiscard]] const PartInfo* GetPart() const noexcept {
    return part_;
  }
  /** @return The part's capabilities (all false without a part). */
  [[nodiscard]] Capabilities GetCapabilities() const noexcept {
    return (part_ != nullptr) ? CapabilitiesOf(*part_) : Capabilities{};
  }
  /** @return The 7-bit address in use. */
  [[nodiscard]] uint8_t GetAddress() const noexcept {
    return address_;
  }

  /// @}

private:
  Result<Wait> begin(const detail::Request& rq) noexcept;
  Result<Wait> run() noexcept;
  Result<Wait> advance() noexcept;
  Result<Wait> failOp(Error error) noexcept;
  Error execStep(const detail::Step& step, uint32_t& wait_us) noexcept;
  Error writeStep(const detail::Step& step) noexcept;
  Error doWrite(const detail::Step& step) noexcept;
  Error doPoll(const detail::Step& step, uint32_t& wait_us) noexcept;
  Error doRead(const detail::Step& step) noexcept;
  Error finishPhase() noexcept;
  Error finishInit() noexcept;
  Error finishScale() noexcept;
  Error finishTemperature() noexcept;
  Error adoptIdentity(const ProductInfo& info) noexcept;
  void loadPhase() noexcept;
  bool nextPhase() noexcept;
  void complete() noexcept;
  [[nodiscard]] detail::Request makeRequest(detail::Op op) const noexcept;
  Error gate(bool allowed) const noexcept;
  Error decodeSample(Sample& sample) noexcept;
  Error captureReply() noexcept;
  Error fillSample(const detail::Frame& frame, Sample& sample) noexcept;
  Error checkRange(Sample& sample) const noexcept;
  Error checkFresh(Sample& sample) noexcept;
  Result<Sample> sampleFailure(Error error) noexcept;
  void noteFrame() noexcept;
  [[nodiscard]] bool silenceExceeded() const noexcept;
  Error escalateSilence() noexcept;
  Error resetSupport() const noexcept;
  [[nodiscard]] uint16_t resolvedAveraging() const noexcept;
  Error driverConfigOk() const noexcept;
  [[nodiscard]] std::span<const uint8_t> rx() const noexcept {
    return {rx_.data(), rx_used_};
  }

  Port& port_;
  Config config_;
  const PartInfo* part_{nullptr};
  const GasRow* running_{nullptr};
  detail::Op op_{detail::Op::None};
  detail::Request req_{};
  detail::Plan plan_{};
  std::array<uint8_t, kMaxRead> rx_{};
  ProductInfo info_{};
  int32_t temperature_mdegc_{0};
  ScalingInfo scaling_{};
  RawReply last_reply_{};
  Stats stats_{};
  State state_{State::Uninitialized};
  uint32_t wait_us_{0};
  uint32_t wait_start_us_{0};
  uint32_t started_us_{0};
  uint32_t last_good_us_{0};
  uint32_t last_frame_us_{0};
  uint32_t quiet_start_us_{0};
  uint32_t quiet_us_{0};
  uint16_t o2_permille_{0};
  uint16_t averaging_{0};
  std::size_t rx_used_{0};
  uint8_t address_{0};
  uint8_t step_{0};
  uint8_t polls_{0};
  uint8_t phase_{0};
  bool waiting_{false};
  bool skip_optional_read_{false};
  bool auto_{false};
};

} // namespace sfm

// codeql::misra_deviation_next_line(MD-CPP-009)
#include "../src/sfm_flow_meter.ipp" // IWYU pragma: export

#endif // SFM_FLOW_METER_HPP
