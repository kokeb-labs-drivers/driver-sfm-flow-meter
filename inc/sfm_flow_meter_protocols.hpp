/**
 * @file sfm_flow_meter_protocols.hpp
 * @brief The six protocol policies: pure `constexpr` functions that turn an
 * operation into a `Plan` of bus steps and decode what the sensor sent back. No
 * I/O, no state.
 * @copyright Copyright (c) 2026 The driver-sfm-flow-meter authors.
 * SPDX-License-Identifier: MIT
 *
 * @details
 * The driver core runs a `Plan` one step at a time (it never blocks) and hands
 * the bytes it received to a decoder. Every decoder reads the bytes through
 * `ParseWords()`, the only code that reads raw bytes: it checks the CRC-8 of
 * every word before any value is decoded (invariant I1). The dispatch is on
 * `PartInfo::family` and on feature bits, never on a variant.
 */
#ifndef SFM_FLOW_METER_PROTOCOLS_HPP
#define SFM_FLOW_METER_PROTOCOLS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "sfm_flow_meter_detail.hpp"
#include "sfm_flow_meter_i2c_interface.hpp"
#include "sfm_flow_meter_parts.hpp"
#include "sfm_flow_meter_registers.hpp"
#include "sfm_flow_meter_types.hpp"

namespace sfm::detail {

/// @cond SFM_INTERNAL

/** @brief One bus action of a plan. */
enum class StepKind : uint8_t {
  Write,       ///< `Write(addr, bytes, len)`; `len` is at least 1.
  Poll,        ///< A write repeated every `us` until it is acknowledged, at most
               ///< `retries` more times.
  Read,        ///< `Read(addr, rx, len)`.
  ReadStretch, ///< `ReadStretch(addr, rx, len)`, a read that tolerates clock
               ///< stretching.
  GeneralCall, ///< `WriteGeneralCall(bytes[0])`.
  Wait,        ///< Return a wait of `us` to the caller.
  ResetPin,    ///< `SetResetPin(level)`.
  PowerCycle,  ///< `PowerCycle()`.
};

/** @brief The operation a plan belongs to; the core's completion step depends
 * on it. */
enum class Op : uint8_t {
  None = 0,        ///< No operation pending.
  Init,            ///< `EnsureInitialized()`.
  Start,           ///< `StartMeasurement()`.
  Stop,            ///< `Stop()`.
  SetAveraging,    ///< `SetAveraging()`.
  ReadScaling,     ///< `ReadScaling()`.
  Sleep,           ///< `Sleep()`.
  Wake,            ///< `WakeUp()`.
  SoftReset,       ///< `SoftReset()`.
  HardReset,       ///< `HardReset()`.
  ReadSample,      ///< `ReadSample()`.
  ReadTemperature, ///< `ReadTemperature()`.
  Concentration,   ///< `UpdateConcentration()`.
};

/** @brief One step: what to do, what to send, and which error a failure means.
 */
struct Step {
  StepKind kind{StepKind::Wait};          ///< The action.
  uint8_t len{0};                         ///< Bytes to write or read.
  uint8_t retries{0};                     ///< `Poll`: attempts after the first.
  Error on_fail{Error::Bus};              ///< The error a failed transfer means.
  bool optional{false};                   ///< A failed write is ignored.
  bool level{false};                      ///< `ResetPin`: the level to drive.
  uint32_t us{0};                         ///< `Wait` and `Poll`: microseconds.
  std::array<uint8_t, kMaxWrite> bytes{}; ///< The bytes to write.
};

/// Most steps in one plan.
inline constexpr std::size_t kMaxSteps = 10;
/// Most phases of one operation (a phase is a plan whose decoded result decides
/// the next).
inline constexpr uint8_t kMaxPhases = 3;

/** @brief A bounded list of steps and the number of bytes its reads deliver in
 * all. */
struct Plan {
  std::array<Step, kMaxSteps> steps{}; ///< The steps in order.
  uint8_t count{0};                    ///< Steps in use.
  uint8_t rx_len{0};                   ///< Bytes the read steps receive in all.
};

/** @brief What the policy needs to know about a call. */
struct Request {
  Op op{Op::None};            ///< The operation.
  const GasRow* gas{nullptr}; ///< The gas row (start and scale reads).
  uint16_t value{0};          ///< Averaging N, or the oxygen fraction in per mille.
  uint16_t filter{0};         ///< The filter argument.
  uint16_t eeprom_base{0};    ///< SF04: the EEPROM base of the active calibration field.
  bool filter_on{false};      ///< Send the filter argument.
  bool restart{false};        ///< SF05: start again after each read.
  bool conservative{true};    ///< `TimingPolicy::Conservative`.
  bool poll_id{false};        ///< Idle polls send the identifier command, not stop
                              ///< (K-STOPIDLE).
  bool serial_low{true};      ///< SF05: read the low serial word through 0x31AF (K-SF05-SERIAL).
};

/// The 3-byte unit of every read: a 16-bit word and its CRC-8.
inline constexpr uint8_t kWordBytes = 3;
/// The longest SF06 reset or wake the conservative timing assumes, in
/// microseconds.
inline constexpr uint32_t kConservativeUs = 20000;
/// Poll interval for wake and reset polls, in microseconds.
inline constexpr uint32_t kPollIntervalUs = 1000;

/// A `Step` that writes the two bytes of `cmd`.
constexpr Step Write16(uint16_t cmd, Error fail) noexcept {
  Step s{};
  s.kind = StepKind::Write;
  s.len = 2U;
  s.on_fail = fail;
  s.bytes[0] = static_cast<uint8_t>((static_cast<uint32_t>(cmd) >> 8U) & 0xFFU);
  s.bytes[1] = static_cast<uint8_t>(static_cast<uint32_t>(cmd) & 0xFFU);
  return s;
}

/// A `Step` that writes one command byte (SF04).
constexpr Step Write8(uint8_t cmd, Error fail) noexcept {
  Step s{};
  s.kind = StepKind::Write;
  s.len = 1U;
  s.on_fail = fail;
  s.bytes[0] = cmd;
  return s;
}

/// A `Step` that writes a command, a 16-bit argument and the argument's CRC
/// (SF06 commands).
constexpr Step WriteArg(uint16_t cmd, uint16_t arg, uint8_t crc_init) noexcept {
  Step s = Write16(cmd, Error::Bus);
  s.len = 5U;
  const auto high = static_cast<uint8_t>((static_cast<uint32_t>(arg) >> 8U) & 0xFFU);
  const auto low = static_cast<uint8_t>(static_cast<uint32_t>(arg) & 0xFFU);
  s.bytes[2] = high;
  s.bytes[3] = low;
  s.bytes[4] = reg::Crc8(crc_init, high, low);
  return s;
}

/// A `Step` that writes the SF04 EEPROM read command and the 12-bit address,
/// left aligned.
constexpr Step WriteEeprom(uint16_t address) noexcept {
  Step s{};
  s.kind = StepKind::Write;
  s.len = 3U;
  s.bytes[0] = reg::kSf04Eeprom;
  const uint32_t aligned = (static_cast<uint32_t>(address) << 4U) & 0xFFFFU;
  s.bytes[1] = static_cast<uint8_t>(aligned >> 8U);
  s.bytes[2] = static_cast<uint8_t>(aligned & 0xFFU);
  return s;
}

/// A read of `words` CRC-protected words; `fail` is what a NACK of the header
/// means.
constexpr Step ReadWords(uint8_t words, Error fail, bool stretch) noexcept {
  Step s{};
  s.kind = stretch ? StepKind::ReadStretch : StepKind::Read;
  s.len = static_cast<uint8_t>(static_cast<uint32_t>(words) * kWordBytes);
  s.on_fail = fail;
  return s;
}

/// A wait of `us` microseconds.
constexpr Step Delay(uint32_t us) noexcept {
  Step s{};
  s.kind = StepKind::Wait;
  s.us = us;
  return s;
}

/// The command a poll sends: the next real, idempotent command of the family,
/// never an empty write. SF06 families and SFx6000: the stop command (or, with
/// `id`, the SF06 identifier command, the knob for a sensor that does not take
/// stop while idle, K-STOPIDLE); SF05: the pointer to the serial number; SF04:
/// the read of read-only register 2. A sensor that is asleep or in reset does
/// not acknowledge its address, so the transfer ends at the address, as an
/// empty probe would; an awake one accepts a whole command.
constexpr Step PollCommand(const PartInfo& p, bool id = false) noexcept {
  if (id && ((p.family == Family::Sf06) || (p.family == Family::Sf06Gen2) ||
             (p.family == Family::Sf06Dual))) {
    return Write16((p.family == Family::Sf06Dual) ? reg::kCmdDeviceIdDual : reg::kCmdProductId,
                   Error::Bus);
  }
  switch (p.family) {
  case Family::Sf05:
    return Write16(reg::kSf05Serial, Error::Bus);
  case Family::Sf04:
    return Write8(reg::kSf04ReadOnly2, Error::Bus);
  case Family::Sf06:
  case Family::Sf06Gen2:
  case Family::Sf06Dual:
  case Family::Sfx6000:
  default:
    break;
  }
  return Write16(reg::kCmdStop, Error::Bus);
}

/// A poll: `PollCommand()` repeated every `kPollIntervalUs` until it is
/// acknowledged, for at most `bound_us`. A NACK on the way is expected and is
/// not a bus error.
constexpr Step PollStep(const PartInfo& p, uint32_t bound_us, Error fail,
                        bool id = false) noexcept {
  Step s = PollCommand(p, id);
  s.kind = StepKind::Poll;
  s.on_fail = fail;
  s.us = kPollIntervalUs;
  const uint32_t tries = (bound_us / kPollIntervalUs) + 1U;
  s.retries = static_cast<uint8_t>((tries < 255U) ? tries : 255U);
  return s;
}

/// Append `step` to `plan`; a step beyond the capacity is dropped (the tests
/// prove no plan reaches it).
constexpr void Add(Plan& plan, const Step& step) noexcept {
  if (plan.count < kMaxSteps) {
    plan.steps[plan.count] = step;
    plan.count = static_cast<uint8_t>(plan.count + 1U);
    if ((step.kind == StepKind::Read) || (step.kind == StepKind::ReadStretch)) {
      plan.rx_len = static_cast<uint8_t>(plan.rx_len + step.len);
    }
  }
}

/// Append a wait when `us` is not zero.
constexpr void AddDelay(Plan& plan, uint32_t us) noexcept {
  if (us != 0U) {
    Add(plan, Delay(us));
  }
}

/// Time between two results of the continuous modes at one averaged sample: 2
/// kHz (SFM4300 3.2).
inline constexpr uint32_t kSamplePeriodUs = 500;

/// How long a sensor in continuous mode may go without a new sample before it
/// counts as silent: the time to the first result, one period of `averaging`
/// samples with 10 % margin, and one poll interval. A read that finds no new
/// sample before that is the sensor working, not a fault.
constexpr uint32_t SilenceBoundUs(const PartInfo& p, uint32_t averaging) noexcept {
  const uint32_t n = (Has(p, feature::kAveraging) && (averaging > 1U)) ? averaging : 1U;
  const uint32_t period = n * kSamplePeriodUs;
  return p.timing.first_sample_us + period + (period / 10U) + kPollIntervalUs;
}

/// The longest wait after a reset or a wake the driver allows for.
constexpr uint32_t ResetBoundUs(const PartInfo& p, bool conservative) noexcept {
  uint32_t bound = p.timing.power_up_us;
  bound = (p.timing.soft_reset_us > bound) ? p.timing.soft_reset_us : bound;
  bound = (p.timing.wake_us > bound) ? p.timing.wake_us : bound;
  if (conservative && ((p.family == Family::Sf06) || (p.family == Family::Sf06Gen2))) {
    bound = (bound > kConservativeUs) ? bound : kConservativeUs;
  }
  // The datasheet figures are typical values: one quarter more before the poll
  // gives up.
  return bound + (bound / 4U);
}

/// The soft-reset time of a part under the timing policy.
constexpr uint32_t SoftResetUs(const PartInfo& p, bool conservative) noexcept {
  if (conservative && (p.family == Family::Sf06)) {
    return (p.timing.soft_reset_us > kConservativeUs) ? p.timing.soft_reset_us : kConservativeUs;
  }
  return p.timing.soft_reset_us;
}

/// The averaging the chip uses after a reset: 0 (average until read) for SF06,
/// N = 2 for the others.
constexpr uint16_t ResetAveraging(const PartInfo& p) noexcept {
  return (p.family == Family::Sf06) ? 0U : 2U;
}

/// The command that reads the product identifier.
constexpr uint16_t IdentifierCommand(const PartInfo& p) noexcept {
  return (p.family == Family::Sf06Dual) ? reg::kCmdDeviceIdDual : reg::kCmdProductId;
}

// --- Plans
// -----------------------------------------------------------------------------------

/// Initialisation, phase 0, for the families with a product identifier.
constexpr Plan InitIdentifierPlan(const PartInfo& p, const Request& rq) noexcept {
  Plan plan{};
  // The poll sends the stop command until it is acknowledged: a sensor that is
  // asleep or still in reset does not answer its address yet, and one that is
  // idle takes the stop as a no-op.
  Add(plan, PollStep(p, ResetBoundUs(p, rq.conservative), Error::NoDevice, rq.poll_id));
  AddDelay(plan, p.timing.stop_us);
  Add(plan, Write16(IdentifierCommand(p), Error::NoDevice));
  AddDelay(plan, p.timing.id_read_us);
  Add(plan, ReadWords(6U, Error::NoDevice, false));
  return plan;
}

/// Initialisation, phase 0, for the SF05 family: wake, reset, read the serial
/// number.
constexpr Plan InitSf05Plan(const PartInfo& p, const Request& rq) noexcept {
  Plan plan{};
  Add(plan, PollStep(p, p.timing.power_up_us, Error::NoDevice));
  if (Has(p, feature::kSoftResetCommand)) {
    Add(plan, Write16(reg::kSf05Reset, Error::NoDevice));
    AddDelay(plan, SoftResetUs(p, rq.conservative));
    Add(plan, PollStep(p, kPollIntervalUs * 4U, Error::NoDevice));
  }
  // The serial number is two words behind two pointers (SFM4200 datasheet 5.2,
  // ambiguity K-SF05-SERIAL).
  Add(plan, Write16(reg::kSf05Serial, Error::NoDevice));
  Add(plan, ReadWords(1U, Error::NoDevice, false));
  if (rq.serial_low) {
    Step ptr = Write16(reg::kSf05SerialLow, Error::NoDevice);
    Step word = ReadWords(1U, Error::NoDevice, false);
    // The SFM3000 application note lists 0x31AE only (K-SF05-SERIAL): there a
    // NACK of the second pair leaves the serial number with its high word
    // instead of failing the start-up.
    const bool legacy = (p.variant == Variant::SFM3000_200_C);
    ptr.optional = legacy;
    word.optional = legacy;
    Add(plan, ptr);
    Add(plan, word);
  }
  return plan;
}

/// Initialisation, phase 0, for SF04: wake and read read-only register 2 (the
/// calibration field).
constexpr Plan InitSf04Plan(const PartInfo& p) noexcept {
  Plan plan{};
  Add(plan, PollStep(p, p.timing.power_up_us, Error::NoDevice));
  Add(plan, Write8(reg::kSf04ReadOnly2, Error::NoDevice));
  Add(plan, ReadWords(1U, Error::NoDevice, false));
  return plan;
}

/// The averaging write: command, N and its CRC.
constexpr Plan AveragingPlan(const PartInfo& p, uint16_t n) noexcept {
  Plan plan{};
  const uint16_t cmd = (p.family == Family::Sf06Dual) ? reg::kCmdAveragingDual : reg::kCmdAveraging;
  Add(plan, WriteArg(cmd, n, p.crc_init));
  return plan;
}

/// Initialisation, phase 1: SF04 reads the serial number, the others set the
/// averaging.
constexpr Plan InitSecondPlan(const PartInfo& p, const Request& rq) noexcept {
  Plan plan{};
  if (p.family == Family::Sf04) {
    Add(plan, WriteEeprom(static_cast<uint16_t>(rq.eeprom_base + reg::kSf04EepromSerial)));
    Add(plan, ReadWords(2U, Error::NoDevice, false));
    return plan;
  }
  if (Has(p, feature::kAveraging)) {
    return AveragingPlan(p, (rq.value == kAveragingDefault) ? ResetAveraging(p) : rq.value);
  }
  return plan;
}

/// Scale read, phase 0 (SF04: phase 0 reads the user register).
constexpr Plan ScalePlan(const PartInfo& p, const Request& rq, uint8_t phase) noexcept {
  Plan plan{};
  const uint16_t code = (rq.gas != nullptr) ? rq.gas->code : uint16_t{0};
  switch (p.family) {
  case Family::Sf06:
  case Family::Sf06Gen2:
    if (phase == 0U) {
      Add(plan, WriteArg(reg::kCmdReadScale, code, p.crc_init));
      Add(plan, ReadWords(3U, Error::Bus, false));
    }
    break;
  case Family::Sfx6000:
    if (phase == 0U) {
      Add(plan, WriteArg(reg::kCmdReadScale, code, p.crc_init));
      Add(plan, Write16(reg::kCmdSfxScalePointer, Error::Bus));
      Add(plan, ReadWords(5U, Error::Bus, false));
    }
    break;
  case Family::Sf05:
    if (phase == 0U) {
      Add(plan, Write16(reg::kSf05ScaleFlow, Error::Bus));
      Add(plan, ReadWords(1U, Error::Bus, false));
      Add(plan, Write16(reg::kSf05OffsetFlow, Error::Bus));
      Add(plan, ReadWords(1U, Error::Bus, false));
    }
    break;
  case Family::Sf04:
    if (phase == 0U) {
      Add(plan, Write8(reg::kSf04UserRegister, Error::Bus));
      Add(plan, ReadWords(1U, Error::Bus, false));
    } else if (phase == 1U) {
      Add(plan, WriteEeprom(static_cast<uint16_t>(rq.eeprom_base + reg::kSf04EepromScale)));
      Add(plan, ReadWords(2U, Error::Bus, false));
    } else {
      // Phase 2 does not exist for SF04.
    }
    break;
  case Family::Sf06Dual:
  default:
    break;
  }
  return plan;
}

/// The start command of a gas, with its argument when the part takes one.
constexpr Plan LaunchPlan(const PartInfo& p, const Request& rq) noexcept {
  Plan plan{};
  if ((rq.gas == nullptr) || (p.family == Family::Sf04)) {
    return plan; // SF04 has no continuous mode: each read triggers its own
                 // measurement
  }
  if (p.family == Family::Sf05) {
    Add(plan, Write16(reg::kSf05Flow, Error::Bus));
  } else if (rq.gas->takes_argument) {
    Add(plan, WriteArg(rq.gas->code, rq.value, p.crc_init));
  } else if (rq.filter_on && Has(p, feature::kFilterArg)) {
    Add(plan, WriteArg(rq.gas->code, rq.filter, p.crc_init));
  } else {
    Add(plan, Write16(rq.gas->code, Error::Bus));
  }
  AddDelay(plan, p.timing.first_sample_us);
  return plan;
}

/// One sample read.
constexpr Plan SamplePlan(const PartInfo& p, const Request& rq) noexcept {
  Plan plan{};
  switch (p.family) {
  case Family::Sf04:
    Add(plan, Write8(reg::kSf04TriggerFlow, Error::Bus));
    Add(plan, ReadWords(1U, Error::Timeout, true));
    break;
  case Family::Sf05:
    Add(plan, ReadWords(1U, Error::NoData, false));
    if (rq.restart) {
      Add(plan, Write16(reg::kSf05Flow, Error::Bus));
    }
    break;
  case Family::Sf06:
  case Family::Sf06Gen2:
  case Family::Sf06Dual:
  case Family::Sfx6000:
  default:
    Add(plan, ReadWords(3U, Error::NoData, false));
    break;
  }
  return plan;
}

/// The SF05 temperature result is updated at 250 Hz (AN-05, 4.3.2): the read
/// follows after 5 ms.
inline constexpr uint32_t kSf05TemperatureUs = 5000;

/// A temperature read by a pointer sequence, ending with the pointer back at
/// the flow result.
constexpr Plan TemperaturePlan(const PartInfo& p) noexcept {
  Plan plan{};
  const bool sf05 = (p.family == Family::Sf05);
  Add(plan, Write16(sf05 ? reg::kSf05Temperature : reg::kCmdSfxTemperature, Error::Bus));
  if (sf05) {
    AddDelay(plan, kSf05TemperatureUs);
  }
  Add(plan, ReadWords(1U, Error::Bus, false));
  Add(plan, Write16(sf05 ? reg::kSf05Flow : reg::kCmdResultPointer, Error::Bus));
  return plan;
}

/// Stop: SF06 families and SFx6000 send the stop command, SF05 moves the
/// pointer out of the measurement space, SF04 has nothing to stop.
constexpr Plan StopPlan(const PartInfo& p) noexcept {
  Plan plan{};
  if (p.family == Family::Sf05) {
    Add(plan, Write16(reg::kSf05Serial, Error::Bus));
  } else if (p.family != Family::Sf04) {
    Add(plan, Write16(reg::kCmdStop, Error::Bus));
    AddDelay(plan, p.timing.stop_us);
  } else {
    // SF04: no continuous mode, no traffic.
  }
  return plan;
}

/// Wake from sleep: poll with the next real command until it is acknowledged
/// (the SFM3304 needs the address plus a byte, which a two-byte command gives).
constexpr Plan WakePlan(const PartInfo& p, const Request& rq) noexcept {
  Plan plan{};
  Add(plan, PollStep(p, ResetBoundUs(p, rq.conservative), Error::Timeout, rq.poll_id));
  AddDelay(plan, p.timing.stop_us);
  return plan;
}

/// Soft reset: the general call, or the part's own command, then wait and poll.
constexpr Plan SoftResetPlan(const PartInfo& p, const Request& rq) noexcept {
  Plan plan{};
  if (Has(p, feature::kGeneralCallReset)) {
    Step gc{};
    gc.kind = StepKind::GeneralCall;
    gc.bytes[0] = reg::kGeneralCallReset;
    Add(plan, gc);
  } else if (p.family == Family::Sf05) {
    Add(plan, Write16(reg::kSf05Reset, Error::Bus));
  } else {
    Add(plan, Write8(reg::kSf04SoftReset, Error::Bus));
  }
  AddDelay(plan, SoftResetUs(p, rq.conservative));
  Add(plan, PollStep(p, kPollIntervalUs * 4U, Error::Timeout, rq.poll_id));
  AddDelay(plan, p.timing.stop_us);
  return plan;
}

/// Hard reset: pulse /RES (SFM4200) or cycle the supply.
constexpr Plan HardResetPlan(const PartInfo& p, const Request& rq) noexcept {
  Plan plan{};
  if (Has(p, feature::kResetPin)) {
    Step low{};
    low.kind = StepKind::ResetPin;
    low.level = true; // asserted
    Add(plan, low);
    AddDelay(plan, p.timing.hard_reset_us);
    Step high{};
    high.kind = StepKind::ResetPin;
    Add(plan, high);
  } else {
    Step cycle{};
    cycle.kind = StepKind::PowerCycle;
    Add(plan, cycle);
  }
  AddDelay(plan, p.timing.power_up_us);
  Add(plan, PollStep(p, p.timing.power_up_us, Error::Timeout, rq.poll_id));
  AddDelay(plan, p.timing.stop_us);
  return plan;
}

/// A live concentration update: two writes with no read between them.
constexpr Plan ConcentrationPlan(const PartInfo& p, const Request& rq) noexcept {
  Plan plan{};
  Add(plan, WriteArg(reg::kCmdUpdateConcentration, rq.value, p.crc_init));
  Add(plan, Write16(reg::kCmdResultPointer, Error::Bus));
  return plan;
}

/// Phase 0 of an initialisation by family.
constexpr Plan InitFirstPlan(const PartInfo& p, const Request& rq) noexcept {
  switch (p.family) {
  case Family::Sf05:
    return InitSf05Plan(p, rq);
  case Family::Sf04:
    return InitSf04Plan(p);
  case Family::Sf06:
  case Family::Sf06Gen2:
  case Family::Sf06Dual:
  case Family::Sfx6000:
  default:
    break;
  }
  return InitIdentifierPlan(p, rq);
}

/// Phase 1 of a wake: the averaging is written again, because the datasheet
/// does not say whether sleep keeps it (ambiguity K-SLEEP-AVG).
constexpr Plan WakeSecondPlan(const PartInfo& p, const Request& rq) noexcept {
  return Has(p, feature::kAveraging) ? AveragingPlan(p, rq.value) : Plan{};
}

/// The one-phase operations.
constexpr Plan BuildSingle(const PartInfo& p, const Request& rq) noexcept {
  switch (rq.op) {
  case Op::Stop:
    return StopPlan(p);
  case Op::SetAveraging:
    return AveragingPlan(p, rq.value);
  case Op::Sleep:
    return Plan{[] {
      Plan plan{};
      Add(plan, Write16(reg::kCmdSleep, Error::Bus));
      return plan;
    }()};
  case Op::SoftReset:
    return SoftResetPlan(p, rq);
  case Op::HardReset:
    return HardResetPlan(p, rq);
  case Op::ReadSample:
    return SamplePlan(p, rq);
  case Op::ReadTemperature:
    return TemperaturePlan(p);
  case Op::Concentration:
    return ConcentrationPlan(p, rq);
  case Op::None:
  case Op::Init:
  case Op::Start:
  case Op::ReadScaling:
  case Op::Wake:
  default:
    break;
  }
  return Plan{};
}

/// Phases 0 to 2 of an operation; an empty plan means the phase does not exist
/// for this part.
constexpr Plan Build(const PartInfo& p, const Request& rq, uint8_t phase) noexcept {
  switch (rq.op) {
  case Op::Init:
    if (phase == 0U) {
      return InitFirstPlan(p, rq);
    }
    return (phase == 1U) ? InitSecondPlan(p, rq) : Plan{};
  case Op::Start:
    if (phase < 2U) {
      return Has(p, feature::kScaleRead) ? ScalePlan(p, rq, phase) : Plan{};
    }
    return (phase == 2U) ? LaunchPlan(p, rq) : Plan{};
  case Op::ReadScaling:
    return ScalePlan(p, rq, phase);
  case Op::Wake:
    if (phase == 0U) {
      return WakePlan(p, rq);
    }
    return (phase == 1U) ? WakeSecondPlan(p, rq) : Plan{};
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
  return (phase == 0U) ? BuildSingle(p, rq) : Plan{};
}

// --- Decoding
// --------------------------------------------------------------------------------

/// Up to six words, after their CRCs were checked.
struct Words {
  std::array<uint16_t, 6> w{}; ///< The words in order.
  uint8_t n{0};                ///< Words in use.
};

/**
 * @brief Check the CRC of every word of `rx` and return the words. The only
 * reader of raw bytes (invariant I1): one bad word discards the whole transfer
 * and every value is `Words{}`.
 * @param crc_init The part's CRC initial value.
 * @param rx The bytes, three per word; the length must be a multiple of 3 and
 * at most 18.
 * @return The words, `InvalidReply` for a wrong length, or `Crc`.
 */
constexpr Result<Words> ParseWords(uint8_t crc_init, std::span<const uint8_t> rx) noexcept {
  if (rx.empty() || ((rx.size() % kWordBytes) != 0U) || (rx.size() > kMaxRead)) {
    return Result<Words>::Failure(Error::InvalidReply);
  }
  Words out{};
  const std::size_t count = rx.size() / kWordBytes;
  for (std::size_t i = 0U; i < count; ++i) {
    const uint8_t high = rx[i * kWordBytes];
    const uint8_t low = rx[(i * kWordBytes) + 1U];
    if (reg::Crc8(crc_init, high, low) != rx[(i * kWordBytes) + 2U]) {
      return Result<Words>::Failure(Error::Crc);
    }
    out.w[i] =
        static_cast<uint16_t>((static_cast<uint32_t>(high) << 8U) | static_cast<uint32_t>(low));
  }
  out.n = static_cast<uint8_t>(count);
  return Result<Words>::Success(out);
}

/// A signed 16-bit value from a word.
constexpr int32_t AsSigned(uint16_t word) noexcept {
  return (word >= 0x8000U) ? (static_cast<int32_t>(word) - 65536) : static_cast<int32_t>(word);
}

/// The band a reported gas temperature must lie in, in milli-degrees Celsius:
/// the sensors work in about -20 to +85 degC, with margin for a hot gas; a
/// value outside it is a stuck line, not data.
inline constexpr int32_t kTemperatureMinMdegC = -60000;
inline constexpr int32_t kTemperatureMaxMdegC = 150000;

/// Whether the bytes are a stuck line: all zero (when a zero word cannot be a
/// value) or all ones.
constexpr bool IsStuck(const PartInfo& p, std::span<const uint8_t> rx) noexcept {
  bool zeros = true;
  bool ones = true;
  for (const uint8_t byte : rx) {
    zeros = zeros && (byte == 0U);
    ones = ones && (byte == 0xFFU);
  }
  const bool zero_is_value = (p.gases.rows[0].scaling.offset == 0);
  return ones || (zeros && !zero_is_value);
}

/// What a decoded sample frame holds.
struct Frame {
  int32_t flow{0};        ///< The raw flow, signed or unsigned as the part reports it.
  int32_t flow2{0};       ///< SFM3505: the oxygen channel.
  int32_t temperature{0}; ///< The raw temperature.
  uint16_t status{0};     ///< The status word.
  bool has_temperature{false};
  bool has_status{false};
  bool has_flow2{false};
};

/// The SFM3505 frame: two 24-bit flows spliced over three words.
constexpr Frame DualFrame(const Words& w) noexcept {
  Frame f{};
  f.flow = static_cast<int32_t>((static_cast<uint32_t>(w.w[0]) << 8U) |
                                (static_cast<uint32_t>(w.w[1]) >> 8U));
  f.flow2 = static_cast<int32_t>(((static_cast<uint32_t>(w.w[1]) & 0xFFU) << 16U) |
                                 static_cast<uint32_t>(w.w[2]));
  f.has_flow2 = true;
  return f;
}

/// The frame of an SF06 or SFx6000 read: flow, temperature (or a reserved
/// word), status.
constexpr Frame StatusFrame(const PartInfo& p, const Words& w) noexcept {
  Frame f{};
  f.flow = AsSigned(w.w[0]);
  f.status = w.w[2];
  f.has_status = true;
  f.has_temperature = Has(p, feature::kTemperatureFrame);
  f.temperature = f.has_temperature ? AsSigned(w.w[1]) : 0;
  return f;
}

/**
 * @brief Decode the bytes of a sample read of `p`: CRC first, then the
 * stuck-line check, then the words of the family's frame.
 * @param p The part.
 * @param rx The bytes the read returned.
 * @return The frame, `Crc`, or `InvalidReply` (stuck line, repeated word, or a
 * bit the part never sets).
 */
constexpr Result<Frame> DecodeFrame(const PartInfo& p, std::span<const uint8_t> rx) noexcept {
  const Result<Words> words = ParseWords(p.crc_init, rx);
  if (!words) {
    return Result<Frame>::Failure(words.error);
  }
  const Words& w = words.value;
  if (IsStuck(p, rx) || ((w.n == 3U) && (w.w[0] == w.w[1]) && (w.w[1] == w.w[2]))) {
    return Result<Frame>::Failure(Error::InvalidReply);
  }
  switch (p.family) {
  case Family::Sf06Dual:
    return Result<Frame>::Success(DualFrame(w));
  case Family::Sf05:
    // SFM3x00 and SFM4200: 14 bits in a 16-bit word, the two least significant
    // bits are zero.
    if ((static_cast<uint32_t>(w.w[0]) & 0x3U) != 0U) {
      return Result<Frame>::Failure(Error::InvalidReply);
    }
    return Result<Frame>::Success(Frame{.flow = static_cast<int32_t>(w.w[0])});
  case Family::Sf04:
    return Result<Frame>::Success(Frame{.flow = AsSigned(w.w[0])});
  case Family::Sf06:
  case Family::Sf06Gen2:
  case Family::Sfx6000:
  default:
    break;
  }
  return Result<Frame>::Success(StatusFrame(p, w));
}

/// What the driver started, for the status-word check.
struct StatusContext {
  uint16_t gas_index{0};    ///< The status nibble of the running start command.
  uint16_t permille{0};     ///< The oxygen fraction of a mixture.
  uint16_t averages{0};     ///< The configured number of averages.
  bool mixture{false};      ///< The running command is a mixture.
  bool filter_given{false}; ///< The filter argument was sent.
};

/// Whether a status nibble is a value the family can send.
constexpr bool NibbleLegal(Family family, uint32_t nibble) noexcept {
  switch (family) {
  case Family::Sf06:
    return (nibble <= 3U) || ((nibble >= 6U) && (nibble <= 8U));
  case Family::Sfx6000:
    return (nibble <= 4U) || (nibble == 10U);
  case Family::Sf06Gen2:
    return nibble == 0U;
  case Family::Sf06Dual:
  case Family::Sf05:
  case Family::Sf04:
  default:
    break;
  }
  return false;
}

/**
 * @brief Check that the status word describes the measurement the driver
 * started (invariant I5).
 * @param p The part.
 * @param status The status word.
 * @param ctx What the driver started.
 * @return `Ok`, `InvalidReply` (a nibble the family never sends) or
 * `StatusMismatch`.
 */
constexpr Error CheckStatus(const PartInfo& p, uint16_t status, const StatusContext& ctx) noexcept {
  const uint32_t nibble = reg::StatusCommand::Extract(status);
  if (!NibbleLegal(p.family, nibble)) {
    return Error::InvalidReply;
  }
  if (nibble != ctx.gas_index) {
    return Error::StatusMismatch;
  }
  if (p.family == Family::Sf06Gen2) {
    const bool fixed = ctx.averages >= 1U;
    const bool ok = ((reg::StatusFixedN::Extract(status) != 0U) == fixed) &&
                    (reg::StatusAverages::Extract(status) == ctx.averages) &&
                    ((reg::StatusFilterArg::Extract(status) != 0U) == ctx.filter_given);
    return ok ? Error::Ok : Error::StatusMismatch;
  }
  if (p.family != Family::Sf06) {
    // SFx6000 [P]: the concentration and averaging fields are not documented;
    // only the nibble is checked.
    return Error::Ok;
  }
  const uint32_t conc = reg::StatusConcentration::Extract(status);
  const uint32_t expected =
      ctx.mixture ? static_cast<uint32_t>(ctx.permille) : uint32_t{reg::kStatusPureGas};
  if (conc != expected) {
    return Error::StatusMismatch;
  }
  const bool fixed = ctx.averages >= 1U;
  return ((reg::StatusFixedN::Extract(status) != 0U) == fixed) ? Error::Ok : Error::StatusMismatch;
}

/// Whether the status word reports exponential smoothing (SF06 and SFM3304).
constexpr bool IsSmoothed(const PartInfo& p, uint16_t status) noexcept {
  const bool has_bit = (p.family == Family::Sf06) || (p.family == Family::Sf06Gen2);
  return has_bit && (reg::StatusSmoothing::Extract(status) != 0U);
}

/** @brief What a product-identifier read must match. */
struct IdentityQuery {
  const PartInfo* expected{nullptr}; ///< The configured row, or `nullptr` for `Variant::Auto`.
  Family family{Family::Sf06};       ///< The configured family.
  uint8_t address{0};                ///< The address the sensor answered at.
  bool accept_prototype{false};      ///< Accept a prototype revision.
};

/// The 32-bit value of two consecutive words.
constexpr uint32_t Join(uint16_t high, uint16_t low) noexcept {
  return (static_cast<uint32_t>(high) << 16U) | static_cast<uint32_t>(low);
}

/**
 * @brief Decode a product identifier read (SF06 families and SFx6000: six
 * words) and match it.
 * @param q What must match.
 * @param crc_init The CRC initial value of the family.
 * @param rx The 18 bytes read.
 * @return The product information, or `Crc`, `InvalidReply` (a serial number of
 * all zeros or all ones) or `DeviceIdMismatch` (not the configured part, a
 * controller product, a prototype).
 */
constexpr Result<ProductInfo> DecodeIdentifier(const IdentityQuery& q, uint8_t crc_init,
                                               std::span<const uint8_t> rx) noexcept {
  const Result<Words> words = ParseWords(crc_init, rx);
  if (!words) {
    return Result<ProductInfo>::Failure(words.error);
  }
  const Words& w = words.value;
  ProductInfo info{};
  info.product_id = Join(w.w[0], w.w[1]);
  info.serial = Join(w.w[4], w.w[5]);
  info.revision = static_cast<uint8_t>(info.product_id & 0xFFU);
  const bool serial_stuck =
      ((w.w[2] == 0U) && (w.w[3] == 0U) && (info.serial == 0U)) ||
      ((w.w[2] == 0xFFFFU) && (w.w[3] == 0xFFFFU) && (info.serial == 0xFFFFFFFFU));
  if (serial_stuck) {
    return Result<ProductInfo>::Failure(Error::InvalidReply);
  }
  const PartInfo* row =
      (q.expected != nullptr) ? q.expected : FindById(q.family, info.product_id, q.address);
  if ((row == nullptr) || ((info.product_id & row->id_mask) != row->id_value) ||
      (row->family != q.family)) {
    return Result<ProductInfo>::Failure(Error::DeviceIdMismatch);
  }
  info.prototype =
      (row->id_mask != 0xFFFFFFFFU) && ((static_cast<uint32_t>(info.revision) >> 4U) == 8U);
  if (info.prototype && !q.accept_prototype) {
    return Result<ProductInfo>::Failure(Error::DeviceIdMismatch);
  }
  info.variant = row->variant;
  info.preliminary = row->preliminary;
  return Result<ProductInfo>::Success(info);
}

/// Decode the serial number of the SF05 (two words) or SF04 (two EEPROM words)
/// families.
constexpr Result<ProductInfo> DecodeSerial(const PartInfo& p,
                                           std::span<const uint8_t> rx) noexcept {
  const Result<Words> words = ParseWords(p.crc_init, rx);
  if (!words) {
    return Result<ProductInfo>::Failure(words.error);
  }
  ProductInfo info{};
  const uint16_t low = (words.value.n > 1U) ? words.value.w[1] : uint16_t{0};
  info.serial = Join(words.value.w[0], low);
  if ((info.serial == 0U) || (info.serial == 0xFFFFFFFFU)) {
    return Result<ProductInfo>::Failure(Error::InvalidReply);
  }
  info.variant = p.variant;
  info.preliminary = p.preliminary;
  return Result<ProductInfo>::Success(info);
}

/// The scale the sensor reports, as the table row would state it (`num / den`,
/// rounded).
constexpr int32_t TableScale(const Scaling& s) noexcept {
  return static_cast<int32_t>(
      DivideRounded(static_cast<int64_t>(s.num), static_cast<int64_t>(s.den)));
}

/// Whether the reported values match the table under `check`.
constexpr bool ScalingMatches(const ScalingInfo& got, const Scaling& table,
                              ScalingCheck check) noexcept {
  if (got.scale == 0) {
    return false;
  }
  if (check == ScalingCheck::Off) {
    return true;
  }
  const int32_t want = TableScale(table);
  const int64_t diff = (got.scale > want) ? (static_cast<int64_t>(got.scale) - want)
                                          : (static_cast<int64_t>(want) - got.scale);
  const bool scale_ok =
      (check == ScalingCheck::Strict) ? (diff == 0) : ((diff * 100) <= static_cast<int64_t>(want));
  return scale_ok && (got.offset == table.offset) && (got.unit == table.unit);
}

/// What the sensor reports for one gas, decoded from the bytes of a scale read
/// (CRC checked).
constexpr Result<ScalingInfo> DecodeScaling(const PartInfo& p,
                                            std::span<const uint8_t> rx) noexcept {
  const Result<Words> words = ParseWords(p.crc_init, rx);
  if (!words) {
    return Result<ScalingInfo>::Failure(words.error);
  }
  const Words& w = words.value;
  ScalingInfo info{};
  info.scale = static_cast<int32_t>(w.w[0]);
  switch (p.family) {
  case Family::Sf05:
    info.offset = static_cast<int32_t>(w.w[1]); // unsigned: SFM3200 and its relatives use 32768
    break;
  case Family::Sf04:
    info.unit = w.w[1];
    break;
  case Family::Sf06:
  case Family::Sf06Gen2:
  case Family::Sfx6000:
  case Family::Sf06Dual:
  default:
    info.offset = AsSigned(w.w[1]);
    info.unit = w.w[2];
    break;
  }
  return Result<ScalingInfo>::Success(info);
}

/// The EEPROM base address of the calibration field a register selects: `field
/// * 0x300`.
constexpr uint16_t EepromBase(uint32_t field) noexcept {
  return static_cast<uint16_t>(field * reg::kSf04FieldStride);
}

/// The calibration field of SF04: user register bits 6:4, or read-only register
/// 2 bits 2:0.
constexpr Result<uint16_t> DecodeField(const PartInfo& p, std::span<const uint8_t> rx,
                                       bool user_register) noexcept {
  const Result<Words> words = ParseWords(p.crc_init, rx);
  if (!words) {
    return Result<uint16_t>::Failure(words.error);
  }
  const uint32_t reg_value = words.value.w[0];
  const uint32_t field = user_register ? ((reg_value >> 4U) & 0x7U) : (reg_value & 0x7U);
  return Result<uint16_t>::Success(EepromBase(field));
}

/// @endcond

} // namespace sfm::detail

#endif // SFM_FLOW_METER_PROTOCOLS_HPP
