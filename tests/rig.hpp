/**
 * @file rig.hpp
 * @brief A driver, its port and the chip model of one part, built together for
 * a test.
 */
#ifndef SFM_FLOW_METER_TEST_RIG_HPP
#define SFM_FLOW_METER_TEST_RIG_HPP

#include <cstdint>

#include "datasheet_expect.hpp"
#include "model_sfm.hpp"
#include "sfm_flow_meter.hpp"
#include "test_harness.hpp"

namespace rig {

/// The chip, the port and the driver of one part. Not movable: the driver holds
/// the port.
template <uint32_t Hooks = model::kAllHooks, uint32_t Mask = sfm::kAllFamilies>
struct Rig {
  using PortT = model::Port<Hooks>;
  using MeterT = sfm::SfmFlowMeter<PortT, Mask>;

  model::Chip chip;
  PortT port;
  MeterT meter;

  Rig(const expect::PartX& spec, const sfm::Config& config) noexcept
      : chip(spec), port(chip), meter(port, config) {
    chip.bus_address = config.address;
  }
  explicit Rig(sfm::Variant variant) noexcept
      : Rig(*expect::Find(variant), sfm::PresetFor(variant)) {}
  Rig(const Rig&) = delete;
  Rig& operator=(const Rig&) = delete;
  ~Rig() = default;

  /// Finish whatever the driver waits for; true when it ended without an error.
  bool Await() noexcept {
    return meter.AwaitPending().ok();
  }
  /// Initialise and wait.
  bool Init() noexcept {
    return meter.EnsureInitialized().ok();
  }
  /// Start a measurement of `gas` and wait for the first sample.
  bool Start(sfm::Gas gas, uint16_t permille = 0) noexcept {
    const sfm::Result<sfm::Wait> w = meter.StartMeasurement(gas, permille);
    return w.ok() && Await();
  }
  /// Run an operation that may wait to its end.
  bool Finish(const sfm::Result<sfm::Wait>& w) noexcept {
    return w.ok() && Await();
  }
};

/// Whether every read the driver made had a length of a whole number of 3-byte
/// words (invariant I1).
template <typename R>
bool ReadsAreWholeWords(const R& r) noexcept {
  const std::size_t n = (r.chip.logged < r.chip.log.size()) ? r.chip.logged : r.chip.log.size();
  for (std::size_t i = 0; i < n; ++i) {
    const model::Txn& t = r.chip.log[i];
    if (!t.write && ((t.len % 3U) != 0U)) {
      return false;
    }
  }
  return true;
}

/// The index in the chip's log of the first write at or after `from` that
/// starts with `hi, lo`, or -1.
template <typename R>
int FindWrite(const R& r, uint8_t hi, uint8_t lo, std::size_t from = 0) noexcept {
  const std::size_t n = (r.chip.logged < r.chip.log.size()) ? r.chip.logged : r.chip.log.size();
  for (std::size_t i = from; i < n; ++i) {
    const model::Txn& t = r.chip.log[i];
    if (t.write && (t.len >= 2U) && (t.data[0] == hi) && (t.data[1] == lo)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

/// Writes the chip saw (acknowledged or not) that carry `len` bytes.
template <typename R>
uint32_t CountWrites(const R& r, uint8_t len) noexcept {
  uint32_t count = 0;
  const std::size_t n = (r.chip.logged < r.chip.log.size()) ? r.chip.logged : r.chip.log.size();
  for (std::size_t i = 0; i < n; ++i) {
    count += (r.chip.log[i].write && (r.chip.log[i].len == len)) ? 1U : 0U;
  }
  return count;
}

} // namespace rig

#endif // SFM_FLOW_METER_TEST_RIG_HPP
