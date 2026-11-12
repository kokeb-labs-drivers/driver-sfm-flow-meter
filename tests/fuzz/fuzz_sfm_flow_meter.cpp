/**
 * @file fuzz_sfm_flow_meter.cpp
 * @brief Fuzz target: the driver over a bus whose behaviour the fuzzer
 * controls.
 *
 * What the driver reads comes from the sensor, so read data is the untrusted
 * input. The fuzzer's bytes choose a part (and with it the family), a
 * configuration, then a sequence of driver calls; between the calls they change
 * the sensor: fail a transfer, flip a bit of a read, stick the line, report
 * another product identifier or status, reset the sensor under the driver,
 * change the scale it reports, advance the clock.
 *
 * After every call the target checks the driver's promises (docs/contract.md)
 * and aborts, naming the line, when one breaks:
 *
 *  1. a failed call returns an `Error` the enum defines;
 *  2. a failed `ReadSample()` returns a zero `Sample`;
 *  3. a call that returns `NotInitialized`, `Busy`, `InvalidArgument` or
 * `Unsupported` sent nothing
 *     (`StateUnknown` can follow traffic: the SF05 error limit);
 *  4. a failed call leaves nothing pending;
 *  5. a returned wait is at most one second;
 *  6. every read the driver made has a whole number of 3-byte words and at most
 * 18 bytes;
 *  7. a returned flow lies inside the span plus the margin unless
 * `RangePolicy::Flag` flagged it;
 *  8. `ReadSample()` succeeds only while `Measuring`;
 *  9. `Measuring` is entered only by a successful `StartMeasurement()` that
 * finished;
 * 10. a successful reset leaves the driver `Uninitialized`;
 * 11. the counters never decrease;
 * 12. `has_flow2`, `has_status` and `has_temperature` follow the part's
 * capabilities;
 * 13. `StateUnknown` is left only by an initialisation or a reset (to `Idle` or
 * `Uninitialized`);
 * 14. a read leaves `Measuring` only with `DeviceReset` or `StateUnknown`,
 * never for "no new sample";
 * 15. no write is ever empty (zero bytes) and no command is sent that the
 * datasheet forbids in the current mode (a soft reset in sleep);
 * 16. `DeviceReset` follows real silence: the model has delivered no frame for
 * at least the whole bound (first-result time, N x 0.5 ms with 10 %, 1 ms; the
 * model keeps its own clock of the last frame, rejected ones included);
 * 17. a temperature the getter returns lies in the plausible band (a stuck bus
 * is never data).
 *
 * Built two ways (tests/fuzz/CMakeLists.txt): with libFuzzer (Clang,
 * `-DSFM_FLOW_METER_BUILD_FUZZERS=ON`), and as a plain executable that replays
 * the corpus in tests/fuzz/corpus/ as a CTest on any compiler.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "rig.hpp"

namespace {

using sfm::Error;

/// The fuzzer's bytes, read front to back; zeros once they run out.
class Input {
public:
  Input(const uint8_t* data, std::size_t size) noexcept : data_(data), size_(size) {}

  [[nodiscard]] bool Empty() const noexcept {
    return pos_ >= size_;
  }
  uint8_t Byte() noexcept {
    return pos_ < size_ ? data_[pos_++] : uint8_t{0};
  }
  uint16_t Word() noexcept {
    const auto high = Byte();
    return static_cast<uint16_t>((static_cast<uint32_t>(high) << 8U) | Byte());
  }

private:
  const uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

/// A broken promise: name the line and abort, so libFuzzer saves the input as a
/// crash.
void Check(bool promise, int line = __builtin_LINE()) noexcept {
  if (!promise) {
    (void)std::fprintf(stderr, "fuzz_sfm_flow_meter: promise broken at line %d\n", line);
    std::abort();
  }
}

using Rig = rig::Rig<>;

sfm::Config FuzzConfig(Input& in, const expect::PartX& part) noexcept {
  sfm::Config c = sfm::PresetFor(part.variant);
  const uint8_t b = in.Byte();
  if ((b & 0x01U) != 0U) {
    c.variant = sfm::Variant::Auto; // inside the family; the families without
                                    // an identifier refuse it
    c.family = sfm::PresetFor(part.variant).family;
    c.address = part.address;
  }
  c.scaling_check = static_cast<sfm::ScalingCheck>((b >> 1U) % 3U);
  c.range_policy = ((b & 0x08U) != 0U) ? sfm::RangePolicy::Flag : sfm::RangePolicy::Reject;
  c.require_settled = (b & 0x10U) != 0U;
  c.reject_smoothed = (b & 0x20U) != 0U;
  c.allow_general_call_reset = (b & 0x40U) != 0U;
  c.accept_prototype = (b & 0x80U) != 0U;
  const uint8_t b2 = in.Byte();
  c.averaging = ((b2 & 0x80U) != 0U) ? static_cast<uint16_t>(b2 & 0x7FU) : sfm::kAveragingDefault;
  c.restart_before_read = (b2 & 0x01U) == 0U;
  c.consecutive_error_limit = static_cast<uint8_t>(1U + ((b2 >> 1U) % 7U));
  c.max_age_us = ((b2 & 0x08U) != 0U) ? 20000U : 0U;
  return c;
}

/// What the fuzzer does to the sensor between two calls.
void FuzzChip(model::Chip& chip, Input& in) noexcept {
  const uint8_t kind = in.Byte();
  switch (kind % 14U) {
  case 0:
    chip.fail_write_in = static_cast<int>(in.Byte() % 6U);
    break;
  case 1:
    chip.fail_read_in = static_cast<int>(in.Byte() % 6U);
    break;
  case 2:
    chip.flip_read_byte = static_cast<int>(in.Byte() % 12U) - 1;
    chip.flip_mask = static_cast<uint8_t>(in.Byte() | 1U);
    break;
  case 3:
    chip.stuck = static_cast<model::Stuck>(in.Byte() % 3U);
    break;
  case 4:
    chip.id_override =
        (in.Byte() % 3U == 0U) ? 0U : (static_cast<uint32_t>(in.Word()) << 16U) | in.Word();
    break;
  case 5:
    chip.bad_status = (in.Byte() & 1U) != 0U;
    chip.status_xor = ((in.Byte() & 1U) != 0U) ? in.Word() : uint16_t{0};
    break;
  case 6:
    chip.BrownOut();
    break;
  case 7:
    chip.dead = (in.Byte() & 1U) != 0U;
    break;
  case 8:
    chip.scale_override = in.Word();
    chip.unit_override = ((in.Byte() & 1U) != 0U) ? in.Word() : uint16_t{0};
    break;
  case 9:
    chip.now_us += static_cast<uint32_t>(in.Word()) * 16U;
    break;
  case 10:
    chip.flow_ml = static_cast<int32_t>(static_cast<int16_t>(in.Word())) * 16;
    chip.flow2_ml = static_cast<int32_t>(static_cast<int16_t>(in.Word())) * 16;
    chip.temp_mdegc = static_cast<int32_t>(static_cast<int16_t>(in.Word()));
    break;
  case 11:
    chip.strict_restart = (in.Byte() & 1U) != 0U;
    chip.or_mask = ((in.Byte() & 1U) != 0U) ? in.Word() : uint16_t{0};
    chip.smoothing = (in.Byte() & 1U) != 0U;
    break;
  case 12:
    chip.ignore_stop = (in.Byte() & 1U) != 0U;
    chip.serial =
        (in.Byte() % 4U == 0U) ? 0U : (static_cast<uint32_t>(in.Word()) << 16U) | in.Word();
    break;
  default:
    chip.bus_address = static_cast<uint8_t>(in.Byte() % 3U == 0U ? 0x55U : 0U);
    break;
  }
}

/// The counters the driver keeps.
struct Counters {
  uint32_t crc, no_data, bus, resets, power;
};
Counters Read(const sfm::Stats& s) noexcept {
  return {s.crc_errors, s.no_data, s.bus_errors, s.device_resets, s.power_cycles};
}

/// What one call must satisfy, checked after it (promises 1, 3, 4, 5).
void CheckCall(Rig& r, Error e, uint32_t transfers_before,
               const sfm::Result<sfm::Wait>* wait) noexcept {
  Check(std::string_view{sfm::ToString(e)} != "Unknown");
  const bool silent = (e == Error::NotInitialized) || (e == Error::Busy) ||
                      (e == Error::InvalidArgument) || (e == Error::Unsupported);
  if (silent) {
    Check((r.chip.writes + r.chip.reads) == transfers_before);
  }
  if (e != Error::Ok) {
    Check(!r.meter.HasPending());
  }
  if ((wait != nullptr) && wait->ok()) {
    Check(wait->value.us <= 1000000U);
  }
}

void CheckSample(Rig& r, const sfm::Result<sfm::Sample>& s) noexcept {
  const sfm::Capabilities caps = r.meter.GetCapabilities();
  if (!s.ok()) {
    Check(s.value.flow_raw == 0 && s.value.flow_ml_per_min == 0 && s.value.status == 0 &&
          !s.value.has_status);
    return;
  }
  Check(r.meter.GetState() == sfm::State::Measuring); // 8
  Check(s.value.has_flow2 == caps.two_flows);         // 12
  Check(s.value.has_status == caps.status_word);
  Check(s.value.has_temperature == caps.temperature_in_frame);
  const sfm::PartInfo& p = *r.meter.GetPart();
  const int64_t span = static_cast<int64_t>(p.span_max_mlpm) - p.span_min_mlpm;
  const int64_t margin = (span * r.meter.GetConfig().range_margin_permille) / 1000;
  const bool inside = (s.value.flow_ml_per_min >= p.span_min_mlpm - margin) &&
                      (s.value.flow_ml_per_min <= p.span_max_mlpm + margin);
  Check(inside ||
        (s.value.out_of_range && r.meter.GetConfig().range_policy == sfm::RangePolicy::Flag)); // 7
}

void CheckReads(const Rig& r) noexcept {
  const std::size_t n = (r.chip.logged < r.chip.log.size()) ? r.chip.logged : r.chip.log.size();
  for (std::size_t i = 0; i < n; ++i) {
    const model::Txn& t = r.chip.log[i];
    Check(t.write || (((t.len % 3U) == 0U) && (t.len <= 18U) && (t.len != 0U))); // 6
  }
}

/// One step of the fuzzer's choosing.
void Step(Rig& r, Input& in, const expect::PartX& part) noexcept {
  const sfm::State before = r.meter.GetState();
  const Counters counters = Read(r.meter.GetStats());
  const uint32_t transfers = r.chip.writes + r.chip.reads;
  const uint8_t op = in.Byte();
  sfm::Result<sfm::Wait> w = sfm::Result<sfm::Wait>::Success(sfm::Wait{0});
  bool is_wait = true;
  bool reset_call = false;
  bool start_call = false;
  switch (op % 14U) {
  case 0:
    w = r.meter.BeginInitialize();
    break;
  case 1: {
    // One pick past the part's gases asks for a gas the part does not list.
    const std::size_t pick = in.Byte() % (static_cast<std::size_t>(part.gas_count) + 1U);
    const sfm::Gas gas = (pick < part.gas_count) ? part.gases[pick].gas : sfm::Gas::HeOxO2;
    w = r.meter.StartMeasurement(gas, in.Word() % 1100U);
  }
    start_call = true;
    break;
  case 2:
    w = r.meter.Stop();
    break;
  case 3:
    w = r.meter.SetAveraging(in.Word() % 140U);
    break;
  case 4:
    w = r.meter.ReadScaling(part.gases[in.Byte() % part.gas_count].gas);
    break;
  case 5:
    w = r.meter.Sleep();
    break;
  case 6:
    w = r.meter.WakeUp();
    break;
  case 7:
    w = r.meter.SoftReset();
    reset_call = true;
    break;
  case 8:
    w = r.meter.HardReset();
    reset_call = true;
    break;
  case 9:
    w = r.meter.UpdateConcentration(in.Word() % 1100U);
    break;
  case 10:
    w = r.meter.ReadTemperature();
    break;
  case 11:
    w = r.meter.Poll(static_cast<uint32_t>(in.Word()) * 8U);
    break;
  case 12: {
    r.chip.now_us += 1000U;
    // The model's last served frame before this read: the driver's own
    // escalation write is a real command and moves the model's clock.
    const uint32_t served_before = r.chip.last_served_us;
    const auto s = r.meter.ReadSample();
    CheckCall(r, s.error, transfers, nullptr);
    CheckSample(r, s);
    // A measurement is left by a read only through a reset report or an unknown
    // mode (promise 14).
    if ((before == sfm::State::Measuring) && (r.meter.GetState() != sfm::State::Measuring)) {
      Check((s.error == Error::DeviceReset) || (s.error == Error::StateUnknown)); // 14
    }
    // A reset report needs a sensor that was silent for longer than its
    // first-result time (16).
    if (s.error == Error::DeviceReset) {
      // The datasheet bound from the model's own numbers: first result, N x 0.5
      // ms with 10 %, 1 ms.
      const expect::PartX& spec = r.chip.Spec();
      const bool averages = (spec.kind != expect::Kind::Sf05) &&
                            (spec.kind != expect::Kind::Sf04) && (spec.kind != expect::Kind::Sfx);
      const uint32_t n =
          (averages && (r.chip.written_averaging > 1U)) ? r.chip.written_averaging : 1U;
      const uint32_t period = n * 500U;
      const uint32_t bound = spec.first_sample_us + period + (period / 10U) + 1000U;
      Check((r.chip.now_us - served_before) >= bound); // 16
    }
    is_wait = false;
    break;
  }
  default:
    FuzzChip(r.chip, in);
    is_wait = false;
    break;
  }
  if (is_wait) {
    CheckCall(r, w.error, transfers, &w);
    if (w.ok() && reset_call && !r.meter.HasPending()) {
      Check(r.meter.GetState() == sfm::State::Uninitialized); // 10
    }
    if (w.ok() && start_call && !r.meter.HasPending()) {
      Check(r.meter.GetState() == sfm::State::Measuring); // 9
    }
    if (w.ok() && r.meter.HasPending() && (op % 14U) != 11U) {
      (void)r.meter.AwaitPending();
    }
  }
  const Counters after = Read(r.meter.GetStats());
  Check(after.crc >= counters.crc && after.no_data >= counters.no_data &&
        after.bus >= counters.bus && after.resets >= counters.resets &&
        after.power >= counters.power); // 11
  if ((before == sfm::State::StateUnknown) && (r.meter.GetState() != sfm::State::StateUnknown)) {
    // Only an initialisation (Idle, or Uninitialized when it failed) or a reset
    // leaves it.
    const bool init = (op % 14U) == 0U;
    const bool reset = ((op % 14U) == 7U) || ((op % 14U) == 8U);
    Check((init && ((r.meter.GetState() == sfm::State::Idle) ||
                    (r.meter.GetState() == sfm::State::Uninitialized))) ||
          (reset && (r.meter.GetState() == sfm::State::Uninitialized))); // 13
  }
  if ((before != sfm::State::Measuring) && (r.meter.GetState() == sfm::State::Measuring)) {
    Check(start_call); // 9: only a start enters Measuring
  }
  if ((op % 14U) == 10U) {
    const int32_t t = r.meter.GetTemperature(); // 17: never a stuck-line value
    Check((t >= sfm::detail::kTemperatureMinMdegC) && (t <= sfm::detail::kTemperatureMaxMdegC));
  }
  Check(r.chip.empty_writes == 0U); // 15
  Check(r.chip.forbidden == 0U);    // 15
  CheckReads(r);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
  Input in(data, size);
  const expect::PartX& part = expect::kParts[in.Byte() % expect::kParts.size()];
  const sfm::Config config = FuzzConfig(in, part);
  Rig r(part, config);
  for (int calls = 0; calls < 96 && !in.Empty(); ++calls) {
    Step(r, in, part);
  }
  return 0;
}
