/**
 * @file test_all_parts.cpp
 * @brief Every part of the table, through the whole life cycle against the chip
 * model: initialise, start each calibrated gas, read flows over the span, stop.
 */
#include <cstdio>

#include "rig.hpp"

namespace {

using rig::Rig;

/// The flow in ml/min a raw word stands for, in 64 bits so a 24-bit span does
/// not overflow.
int32_t Fit(int32_t raw, const expect::GasX& gas) {
  const int64_t counts = static_cast<int64_t>(raw) - gas.offset;
  return static_cast<int32_t>(((counts * 1000) / gas.scale_num) * gas.scale_den);
}

/// Half a count of the part's scale in ml/min, plus the SF05 quantisation (two
/// bits) and a rounding unit.
int32_t Tolerance(const expect::PartX& p) {
  const int32_t count = ((1000 * p.gases[0].scale_den) / p.gases[0].scale_num) + 1;
  return (p.kind == expect::Kind::Sf05) ? (4 * count) : (count + 1);
}

void RunPart(const expect::PartX& spec) {
  Rig<> r(spec.variant);
  const sfm::Result<void> init = r.meter.EnsureInitialized();
  if (!init.ok()) {
    std::printf("  variant %d: init -> %s\n", static_cast<int>(spec.variant),
                sfm::ToString(init.error));
  }
  REQUIRE(init.ok());
  CHECK(r.meter.GetState() == sfm::State::Idle);
  CHECK_EQ(r.meter.GetProductInfo().variant == spec.variant || spec.kind == expect::Kind::Sf05 ||
               spec.kind == expect::Kind::Sf04,
           true);
  for (uint8_t g = 0; g < spec.gas_count; ++g) {
    const expect::GasX& gx = spec.gases[g];
    if (spec.kind == expect::Kind::Sf05 && g > 0) {
      break;
    }
    const sfm::Result<sfm::Wait> started =
        r.meter.StartMeasurement(gx.gas, gx.argument ? uint16_t{210} : uint16_t{0});
    if (!started.ok()) {
      std::printf("  variant %d gas %d: start -> %s\n", static_cast<int>(spec.variant),
                  static_cast<int>(gx.gas), sfm::ToString(started.error));
    }
    REQUIRE(started.ok() && r.Await());
    CHECK(r.meter.GetState() == sfm::State::Measuring);
    // The span the raw word can carry for this gas (a gas with a larger scale
    // has a smaller full scale).
    int32_t raw_max = 32767;
    if (spec.kind == expect::Kind::Sf05) {
      raw_max = 65532;
    } else if (spec.kind == expect::Kind::Dual) {
      raw_max = 16777215;
    }
    const int32_t raw_min =
        (spec.kind == expect::Kind::Sf05 || spec.kind == expect::Kind::Dual) ? 0 : -32768;
    const int32_t fit_max = Fit(raw_max, gx);
    const int32_t fit_min = Fit(raw_min, gx);
    const int32_t lo = (spec.span_min > fit_min) ? spec.span_min : fit_min;
    const int32_t hi = (spec.span_max < fit_max) ? spec.span_max : fit_max;
    for (int32_t step = 0; step <= 4; ++step) {
      r.chip.now_us += 1000U; // the SF05 family delivers a result every 0.5 ms
      r.chip.flow_ml = lo + (((hi - lo) * step) / 4);
      r.chip.flow2_ml = r.chip.flow_ml / 5;
      const sfm::Result<sfm::Sample> s = r.meter.ReadSample();
      if (!s.ok()) {
        std::printf("  variant %d gas %d: read -> %s\n", static_cast<int>(spec.variant),
                    static_cast<int>(gx.gas), sfm::ToString(s.error));
      }
      REQUIRE(s.ok());
      const int32_t diff = s.value.flow_ml_per_min - r.chip.flow_ml;
      CHECK((diff <= Tolerance(spec)) && (diff >= -Tolerance(spec)));
      CHECK(s.value.gas == gx.gas);
    }
    const sfm::Result<sfm::Wait> stop = r.meter.Stop();
    CHECK(r.Finish(stop));
    CHECK_EQ(r.chip.empty_writes, uint32_t{0});
    CHECK(r.meter.GetState() == sfm::State::Idle);
  }
  CHECK(rig::ReadsAreWholeWords(r));
}

TEST_CASE("every part: initialise, start every gas, read the span, stop") {
  for (const expect::PartX& p : expect::kParts) {
    RunPart(p);
  }
}

} // namespace
