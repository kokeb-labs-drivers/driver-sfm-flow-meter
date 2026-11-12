/**
 * @file sfm_flow_meter.ipp
 * @brief SFM flow meter driver implementation, included by
 * `sfm_flow_meter.hpp`.
 * @copyright Copyright (c) 2026 The driver-sfm-flow-meter authors.
 * SPDX-License-Identifier: MIT
 *
 * The plans and decoders are in `sfm_flow_meter_protocols.hpp`; this file runs
 * them one step at a time.
 */
#ifndef SFM_FLOW_METER_IPP
#define SFM_FLOW_METER_IPP

namespace sfm {

// -------------------------------------------------------------------------------------------------
// Plan executor
// -------------------------------------------------------------------------------------------------

template <I2cPort Port, uint32_t FamilyMask>
void SfmFlowMeter<Port, FamilyMask>::loadPhase() noexcept {
  plan_ = detail::Build(*part_, req_, phase_);
  step_ = 0U;
  polls_ = 0U;
  rx_used_ = 0U;
}

template <I2cPort Port, uint32_t FamilyMask>
bool SfmFlowMeter<Port, FamilyMask>::nextPhase() noexcept {
  ++phase_;
  while (phase_ <= detail::LastPhase(op_)) {
    loadPhase();
    if (plan_.count != 0U) {
      return true;
    }
    ++phase_;
  }
  return false;
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::begin(const detail::Request& rq) noexcept {
  req_ = rq;
  op_ = rq.op;
  // A getter never shows a value an earlier call produced once a later call of
  // the same kind began.
  switch (rq.op) {
  case detail::Op::ReadSample:
    last_reply_ = RawReply{};
    break;
  case detail::Op::ReadTemperature:
    temperature_mdegc_ = 0;
    break;
  case detail::Op::Start:
  case detail::Op::ReadScaling:
    scaling_ = ScalingInfo{};
    break;
  case detail::Op::None:
  case detail::Op::Init:
  case detail::Op::Stop:
  case detail::Op::SetAveraging:
  case detail::Op::Sleep:
  case detail::Op::Wake:
  case detail::Op::SoftReset:
  case detail::Op::HardReset:
  case detail::Op::Concentration:
  default:
    break;
  }
  quiet_us_ = 0U;
  skip_optional_read_ = false;
  phase_ = 0U;
  waiting_ = false;
  loadPhase();
  return run();
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::run() noexcept {
  for (uint8_t round = 0U; round <= detail::kMaxPhases; ++round) {
    const Result<Wait> w = advance();
    if (!w || (w.value.us != 0U)) {
      return w;
    }
    const Error e = finishPhase();
    if (e != Error::Ok) {
      return failOp(e);
    }
    if (!nextPhase()) {
      complete();
      return Result<Wait>::Success(Wait{0U});
    }
  }
  return failOp(Error::InvalidReply);
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::advance() noexcept {
  while (step_ < plan_.count) {
    uint32_t wait_us = 0U;
    const Error e = execStep(plan_.steps[step_], wait_us);
    if (e != Error::Ok) {
      return failOp(e);
    }
    if (wait_us != 0U) {
      waiting_ = true;
      wait_us_ = wait_us;
      wait_start_us_ = detail::NowUs(port_);
      return Result<Wait>::Success(Wait{wait_us});
    }
  }
  return Result<Wait>::Success(Wait{0U});
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::doWrite(const detail::Step& step) noexcept {
  if (port_.Write(address_, step.bytes.data(), step.len)) {
    return Error::Ok;
  }
  if (!step.optional) {
    ++stats_.bus_errors; // an optional write may be refused by design
                         // (K-SF05-SERIAL)
  }
  return step.on_fail;
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::doPoll(const detail::Step& step, uint32_t& wait_us) noexcept {
  // A NACK is what a sleeping or resetting sensor answers by design: not a bus
  // error until the bound is used up.
  if (port_.Write(address_, step.bytes.data(), step.len)) {
    polls_ = 0U;
    return Error::Ok;
  }
  if (polls_ < step.retries) {
    ++polls_;
    wait_us = step.us;
    return Error::Busy; // not done: retry this step after the wait
  }
  polls_ = 0U;
  ++stats_.bus_errors;
  return step.on_fail;
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::doRead(const detail::Step& step) noexcept {
  if ((rx_used_ + step.len) > rx_.size()) {
    return Error::InvalidArgument; // no plan reads more than `kMaxRead`; proved
                                   // by the tests
  }
  if (step.optional && skip_optional_read_) {
    skip_optional_read_ = false;
    return Error::Ok; // its pointer was refused: nothing meaningful can be read
  }
  uint8_t* const dest = rx_.data() + rx_used_;
  bool ok = false;
  if (step.kind == detail::StepKind::ReadStretch) {
    if constexpr (detail::HasReadStretch<Port>) {
      ok = port_.ReadStretch(address_, dest, step.len);
    } else {
      return Error::Unsupported;
    }
  } else {
    ok = port_.Read(address_, dest, step.len);
  }
  if (!ok && step.optional) {
    return Error::Ok; // nothing arrived; the decoder works with the words that
                      // did
  }
  if (!ok) {
    stats_.no_data += (step.on_fail == Error::NoData) ? 1U : 0U;
    return step.on_fail;
  }
  rx_used_ += step.len;
  if ((op_ == detail::Op::ReadSample) || (op_ == detail::Op::ReadTemperature)) {
    noteFrame(); // a delivered frame, whatever a later step or the decoder says
  }
  return Error::Ok;
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::writeStep(const detail::Step& step) noexcept {
  Error e = doWrite(step);
  if ((e != Error::Ok) && step.optional) {
    e = Error::Ok;
    skip_optional_read_ = true; // the read that belongs to a refused pointer finds nothing
  }
  if ((e == Error::Ok) && (op_ == detail::Op::ReadTemperature)) {
    // The temperature read moves the pointer and its restore starts the
    // measurement again (SF05): the next result is a first-result time away,
    // whatever the decoder says of the temperature.
    last_frame_us_ = detail::NowUs(port_);
  }
  return e;
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::execStep(const detail::Step& step,
                                               uint32_t& wait_us) noexcept {
  using detail::StepKind;
  Error e = Error::Ok;
  switch (step.kind) {
  case StepKind::Write:
    e = writeStep(step);
    break;
  case StepKind::Poll:
    e = doPoll(step, wait_us);
    if (e == Error::Busy) {
      return Error::Ok; // retry this step after the wait
    }
    break;
  case StepKind::Read:
  case StepKind::ReadStretch:
    e = doRead(step);
    break;
  case StepKind::GeneralCall:
    if constexpr (detail::HasGeneralCall<Port>) {
      e = port_.WriteGeneralCall(step.bytes[0]) ? Error::Ok : Error::Bus;
    } else {
      e = Error::Unsupported;
    }
    break;
  case StepKind::Wait:
    wait_us = step.us;
    break;
  case StepKind::ResetPin:
    if constexpr (detail::HasResetPin<Port>) {
      e = port_.SetResetPin(step.level) ? Error::Ok : Error::Bus;
    } else {
      e = Error::Unsupported;
    }
    break;
  case StepKind::PowerCycle:
    if constexpr (detail::HasPowerCycle<Port>) {
      e = port_.PowerCycle() ? Error::Ok : Error::Bus;
      stats_.power_cycles += (e == Error::Ok) ? 1U : 0U;
    } else {
      e = Error::Unsupported;
    }
    break;
  default:
    e = Error::InvalidArgument;
    break;
  }
  if (e == Error::Ok) {
    ++step_;
  }
  return e;
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::failOp(Error error) noexcept {
  using detail::Op;
  stats_.crc_errors += (error == Error::Crc) ? 1U : 0U;
  bool unknown = false;
  switch (op_) {
  case Op::Start:
    unknown = (phase_ == 2U); // the scale phases leave the chip idle
    break;
  case Op::Stop:
  case Op::Sleep:
  case Op::SetAveraging: // a write reported as failed may have been applied
  case Op::SoftReset:
  case Op::HardReset:
  case Op::Concentration:
    unknown = true;
    break;
  case Op::ReadTemperature:
    unknown = (step_ > 0U) && (step_ < plan_.count); // the pointer moved and was not restored
    break;
  case Op::None:
  case Op::Init:
  case Op::ReadScaling:
  case Op::Wake:
  case Op::ReadSample:
  default:
    break;
  }
  if (unknown) {
    state_ = State::StateUnknown;
    detail::Log(port_, LogLevel::Warning, "sfm_flow_meter: a command failed; the mode is unknown");
  }
  op_ = Op::None;
  waiting_ = false;
  return Result<Wait>::Failure(error);
}

template <I2cPort Port, uint32_t FamilyMask>
void SfmFlowMeter<Port, FamilyMask>::complete() noexcept {
  using detail::Op;
  switch (op_) {
  case Op::Init:
    state_ = State::Idle;
    averaging_ = req_.value;
    detail::Log(port_, LogLevel::Info, "sfm_flow_meter: initialised");
    break;
  case Op::Start:
    state_ = State::Measuring;
    running_ = req_.gas;
    o2_permille_ = req_.value;
    started_us_ = detail::NowUs(port_);
    last_good_us_ = started_us_;
    last_frame_us_ = started_us_;
    break;
  case Op::Stop:
  case Op::Wake:
    state_ = State::Idle;
    break;
  case Op::SetAveraging:
    averaging_ = req_.value;
    break;
  case Op::Sleep:
    state_ = State::Asleep;
    break;
  case Op::SoftReset:
  case Op::HardReset:
    state_ = State::Uninitialized;
    running_ = nullptr;
    break;
  case Op::Concentration:
    o2_permille_ = req_.value;
    break;
  case Op::None:
  case Op::ReadScaling:
  case Op::ReadSample:
  case Op::ReadTemperature:
  default:
    break;
  }
  op_ = Op::None;
  waiting_ = false;
}

// -------------------------------------------------------------------------------------------------
// Decoding the result of a phase
// -------------------------------------------------------------------------------------------------

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::adoptIdentity(const ProductInfo& info) noexcept {
  info_ = info;
  if (auto_) {
    const PartInfo* found = FindPart(info.variant);
    Config checked = config_;
    checked.variant = info.variant;
    checked.address = address_;
    if ((found == nullptr) || (Validate(checked) != Error::Ok)) {
      return Error::InvalidArgument;
    }
    part_ = found;
  }
  return Error::Ok;
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::finishInit() noexcept {
  const PartInfo& p = *part_;
  if ((phase_ == 1U) || (p.family == Family::Sf05)) {
    const Result<ProductInfo> serial = detail::DecodeSerial(p, rx());
    if (serial) {
      info_ = serial.value;
    }
    return serial.error;
  }
  if (p.family == Family::Sf04) {
    const Result<uint16_t> base = detail::DecodeField(p, rx(), false);
    req_.eeprom_base = base.value;
    return base.error;
  }
  const detail::IdentityQuery query{.expected = auto_ ? nullptr : part_,
                                    .family = p.family,
                                    .address = address_,
                                    .accept_prototype = config_.accept_prototype};
  const Result<ProductInfo> id = detail::DecodeIdentifier(query, p.crc_init, rx());
  return id ? adoptIdentity(id.value) : id.error;
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::finishScale() noexcept {
  const PartInfo& p = *part_;
  if ((p.family == Family::Sf04) && (phase_ == 0U)) {
    const Result<uint16_t> base = detail::DecodeField(p, rx(), true);
    req_.eeprom_base = base.value;
    return base.error;
  }
  const Result<ScalingInfo> scale = detail::DecodeScaling(p, rx());
  if (!scale) {
    return scale.error;
  }
  scaling_ = scale.value;
  // The SF05 registers hold the air calibration whichever gas is measured; the
  // other gases are conversions the host applies (SFM3000 datasheet, oxygen).
  const Scaling& want = (p.family == Family::Sf05) ? p.gases.rows[0].scaling : req_.gas->scaling;
  scaling_.matches_table = detail::ScalingMatches(scaling_, want, config_.scaling_check);
  return scaling_.matches_table ? Error::Ok : Error::DeviceFault;
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::finishTemperature() noexcept {
  const Result<detail::Words> words = detail::ParseWords(part_->crc_init, rx());
  if (!words) {
    return words.error;
  }
  const uint16_t word = words.value.w[0];
  const int32_t raw =
      (part_->family == Family::Sf05) ? static_cast<int32_t>(word) : detail::AsSigned(word);
  const int32_t value = TemperatureMdegC(raw, part_->temperature);
  // A stuck bus is not a temperature: with CRC init 0 a zero word is CRC-valid
  // and reads as the offset below zero (-200 degC on the SF05 parts). The band
  // is the check; a word is never refused for its pattern, because -1 is a real
  // reading on the SFx6000 (-0.005 degC).
  if ((value < detail::kTemperatureMinMdegC) || (value > detail::kTemperatureMaxMdegC)) {
    return Error::InvalidReply;
  }
  temperature_mdegc_ = value;
  return Error::Ok;
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::finishPhase() noexcept {
  if (plan_.rx_len == 0U) {
    return Error::Ok;
  }
  switch (op_) {
  case detail::Op::Init:
    return finishInit();
  case detail::Op::Start:
  case detail::Op::ReadScaling:
    return finishScale();
  case detail::Op::ReadTemperature:
    return finishTemperature();
  case detail::Op::None:
  case detail::Op::Stop:
  case detail::Op::SetAveraging:
  case detail::Op::Sleep:
  case detail::Op::Wake:
  case detail::Op::SoftReset:
  case detail::Op::HardReset:
  case detail::Op::ReadSample:
  case detail::Op::Concentration:
  default:
    break;
  }
  return Error::Ok; // samples are decoded by `ReadSample()`
}

// -------------------------------------------------------------------------------------------------
// Requests and gates
// -------------------------------------------------------------------------------------------------

template <I2cPort Port, uint32_t FamilyMask>
uint16_t SfmFlowMeter<Port, FamilyMask>::resolvedAveraging() const noexcept {
  return (config_.averaging == kAveragingDefault) ? detail::ResetAveraging(*part_)
                                                  : config_.averaging;
}

template <I2cPort Port, uint32_t FamilyMask>
detail::Request SfmFlowMeter<Port, FamilyMask>::makeRequest(detail::Op op) const noexcept {
  detail::Request r{};
  r.op = op;
  r.value = (op == detail::Op::Wake) ? averaging_ : resolvedAveraging();
  r.filter = config_.filter;
  r.filter_on = config_.filter_enabled;
  r.restart = config_.restart_before_read && Has(*part_, feature::kRestartBeforeRead);
  r.conservative = (config_.timing == TimingPolicy::Conservative);
  r.poll_id = config_.poll_with_identifier;
  r.serial_low = config_.sf05_serial_low_word;
  return r;
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::gate(bool allowed) const noexcept {
  if (op_ != detail::Op::None) {
    return Error::Busy;
  }
  if constexpr (detail::HasNowUs<Port>) {
    // The stop command an escalation sent needs its power-down time before the
    // next command.
    if ((quiet_us_ != 0U) && ((detail::NowUs(port_) - quiet_start_us_) < quiet_us_)) {
      return Error::Busy;
    }
  }
  if (state_ == State::Uninitialized) {
    return Error::NotInitialized;
  }
  if (state_ == State::StateUnknown) {
    return Error::StateUnknown;
  }
  return allowed ? Error::Ok : Error::Busy;
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::driverConfigOk() const noexcept {
  if ((part_ == nullptr) || (Validate(config_) != Error::Ok)) {
    return Error::InvalidArgument;
  }
  if ((FamilyBit(part_->family) & FamilyMask) == 0U) {
    return Error::Unsupported;
  }
  if ((config_.require_settled || (config_.max_age_us != 0U)) && !detail::HasNowUs<Port>) {
    return Error::InvalidArgument;
  }
  if (Has(*part_, feature::kClockStretch) && !detail::HasReadStretch<Port>) {
    return Error::Unsupported;
  }
  return Error::Ok;
}

// -------------------------------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------------------------------

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::BeginInitialize() noexcept {
  if (op_ != detail::Op::None) {
    return Result<Wait>::Failure(Error::Busy);
  }
  if ((state_ == State::Idle) || (state_ == State::Measuring) || (state_ == State::Asleep)) {
    return Result<Wait>::Success(Wait{0U});
  }
  const Error e = driverConfigOk();
  if (e != Error::Ok) {
    return Result<Wait>::Failure(e);
  }
  if (!port_.EnsureInitialized()) {
    return Result<Wait>::Failure(Error::Bus);
  }
  return begin(makeRequest(detail::Op::Init));
}

template <I2cPort Port, uint32_t FamilyMask>
Result<void> SfmFlowMeter<Port, FamilyMask>::EnsureInitialized() noexcept {
  const Result<Wait> w = BeginInitialize();
  if (!w) {
    return Result<void>::Failure(w.error);
  }
  return AwaitPending();
}

template <I2cPort Port, uint32_t FamilyMask>
Result<void> SfmFlowMeter<Port, FamilyMask>::Deinitialize() noexcept {
  op_ = detail::Op::None;
  waiting_ = false;
  state_ = State::Uninitialized;
  running_ = nullptr;
  return Result<void>::Success();
}

// -------------------------------------------------------------------------------------------------
// Operations that may wait
// -------------------------------------------------------------------------------------------------

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::StartMeasurement(Gas gas,
                                                              uint16_t o2_permille) noexcept {
  const Error g = gate(state_ == State::Idle);
  if (g != Error::Ok) {
    return Result<Wait>::Failure(g);
  }
  const GasRow* row = FindGas(*part_, gas);
  if ((row == nullptr) || (row->takes_argument ? (o2_permille > 1000U) : (o2_permille != 0U))) {
    return Result<Wait>::Failure(Error::InvalidArgument);
  }
  detail::Request r = makeRequest(detail::Op::Start);
  r.gas = row;
  r.value = o2_permille;
  return begin(r);
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::Stop() noexcept {
  const Error g = gate((state_ == State::Idle) || (state_ == State::Measuring));
  if (g != Error::Ok) {
    return Result<Wait>::Failure(g);
  }
  if (state_ == State::Idle) {
    return Result<Wait>::Success(Wait{0U}); // nothing runs: nothing to stop
  }
  return begin(makeRequest(detail::Op::Stop));
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::SetAveraging(uint16_t n) noexcept {
  const Error g = gate(state_ == State::Idle);
  if (g != Error::Ok) {
    return Result<Wait>::Failure(g);
  }
  if (!Has(*part_, feature::kAveraging)) {
    return Result<Wait>::Failure(Error::Unsupported);
  }
  if (!detail::AveragingInRange(*part_, n)) {
    return Result<Wait>::Failure(Error::InvalidArgument);
  }
  detail::Request r = makeRequest(detail::Op::SetAveraging);
  r.value = (n == kAveragingDefault) ? detail::ResetAveraging(*part_) : n;
  return begin(r);
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::ReadScaling(Gas gas) noexcept {
  const Error g = gate(state_ == State::Idle);
  if (g != Error::Ok) {
    return Result<Wait>::Failure(g);
  }
  if (!Has(*part_, feature::kScaleRead)) {
    return Result<Wait>::Failure(Error::Unsupported);
  }
  const GasRow* row = FindGas(*part_, gas);
  if (row == nullptr) {
    return Result<Wait>::Failure(Error::InvalidArgument);
  }
  detail::Request r = makeRequest(detail::Op::ReadScaling);
  r.gas = row;
  return begin(r);
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::Sleep() noexcept {
  const Error g = gate(state_ == State::Idle);
  if (g != Error::Ok) {
    return Result<Wait>::Failure(g);
  }
  if (!Has(*part_, feature::kSleep)) {
    return Result<Wait>::Failure(Error::Unsupported);
  }
  return begin(makeRequest(detail::Op::Sleep));
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::WakeUp() noexcept {
  const Error g = gate(state_ == State::Asleep);
  if (g != Error::Ok) {
    return Result<Wait>::Failure(g);
  }
  return begin(makeRequest(detail::Op::Wake));
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::resetSupport() const noexcept {
  const Error c = driverConfigOk();
  if (c != Error::Ok) {
    return c;
  }
  if (Has(*part_, feature::kGeneralCallReset)) {
    return (config_.allow_general_call_reset && detail::HasGeneralCall<Port>) ? Error::Ok
                                                                              : Error::Unsupported;
  }
  return Has(*part_, feature::kSoftResetCommand) ? Error::Ok : Error::Unsupported;
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::SoftReset() noexcept {
  if (op_ != detail::Op::None) {
    return Result<Wait>::Failure(Error::Busy);
  }
  if (state_ == State::Asleep) {
    return Result<Wait>::Failure(
        Error::Busy); // the datasheets: a sleeping sensor cannot be soft reset
  }
  const Error e = resetSupport();
  if (e != Error::Ok) {
    return Result<Wait>::Failure(e);
  }
  if (!port_.EnsureInitialized()) {
    return Result<Wait>::Failure(Error::Bus);
  }
  return begin(makeRequest(detail::Op::SoftReset));
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::HardReset() noexcept {
  if (op_ != detail::Op::None) {
    return Result<Wait>::Failure(Error::Busy);
  }
  const Error c = driverConfigOk();
  const bool pin = Has(*part_, feature::kResetPin);
  const bool supported = pin ? detail::HasResetPin<Port>
                             : (Has(*part_, feature::kPowerCycle) && detail::HasPowerCycle<Port>);
  if (c != Error::Ok) {
    return Result<Wait>::Failure(c);
  }
  if (!supported) {
    return Result<Wait>::Failure(Error::Unsupported);
  }
  if (!port_.EnsureInitialized()) {
    return Result<Wait>::Failure(Error::Bus);
  }
  return begin(makeRequest(detail::Op::HardReset));
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::UpdateConcentration(uint16_t o2_permille) noexcept {
  const Error g = gate(state_ == State::Measuring);
  if (g != Error::Ok) {
    return Result<Wait>::Failure(g);
  }
  if (!Has(*part_, feature::kConcentration)) {
    return Result<Wait>::Failure(Error::Unsupported);
  }
  if ((running_ == nullptr) || !running_->takes_argument || (o2_permille > 1000U)) {
    return Result<Wait>::Failure(Error::InvalidArgument);
  }
  detail::Request r = makeRequest(detail::Op::Concentration);
  r.value = o2_permille;
  return begin(r);
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::Poll(uint32_t elapsed_us) noexcept {
  if ((op_ == detail::Op::None) || !waiting_) {
    return Result<Wait>::Success(Wait{0U});
  }
  if (elapsed_us < wait_us_) {
    wait_us_ -= elapsed_us;
    wait_start_us_ = detail::NowUs(port_);
    return Result<Wait>::Success(Wait{wait_us_});
  }
  waiting_ = false;
  wait_us_ = 0U;
  return run();
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::Poll() noexcept {
  uint32_t elapsed = wait_us_; // without a clock the caller is taken to have
                               // waited the whole time
  if constexpr (detail::HasNowUs<Port>) {
    elapsed = detail::NowUs(port_) - wait_start_us_;
  }
  return Poll(elapsed);
}

template <I2cPort Port, uint32_t FamilyMask>
Result<void> SfmFlowMeter<Port, FamilyMask>::AwaitPending() noexcept {
  for (uint32_t round = 0U; round < detail::kMaxAwaitRounds; ++round) {
    if (op_ == detail::Op::None) {
      return Result<void>::Success();
    }
    const uint32_t us = wait_us_;
    detail::DelayUs(port_, us);
    const Result<Wait> w = Poll(us);
    if (!w) {
      return Result<void>::Failure(w.error);
    }
  }
  return Result<void>::Failure(Error::Timeout);
}

// -------------------------------------------------------------------------------------------------
// Measurement
// -------------------------------------------------------------------------------------------------

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::checkRange(Sample& sample) const noexcept {
  const PartInfo& p = *part_;
  const int64_t span =
      static_cast<int64_t>(p.span_max_mlpm) - static_cast<int64_t>(p.span_min_mlpm);
  const int64_t margin = (span * static_cast<int64_t>(config_.range_margin_permille)) / 1000;
  const int64_t low = static_cast<int64_t>(p.span_min_mlpm) - margin;
  const int64_t high = static_cast<int64_t>(p.span_max_mlpm) + margin;
  const bool out =
      (sample.flow_ml_per_min < low) || (sample.flow_ml_per_min > high) ||
      (sample.has_flow2 && ((sample.flow2_ml_per_min < low) || (sample.flow2_ml_per_min > high)));
  if (out && (config_.range_policy == RangePolicy::Reject)) {
    return Error::OutOfRange;
  }
  sample.out_of_range = out;
  return Error::Ok;
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::checkFresh(Sample& sample) noexcept {
  const uint32_t age = sample.timestamp_us - last_good_us_;
  last_good_us_ = sample.timestamp_us;
  if ((config_.require_settled && !sample.settled) ||
      (config_.reject_smoothed && sample.smoothed) ||
      ((config_.max_age_us != 0U) && (age > config_.max_age_us))) {
    return Error::Stale;
  }
  return Error::Ok;
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::fillSample(const detail::Frame& frame,
                                                 Sample& sample) noexcept {
  const PartInfo& p = *part_;
  const Scaling& scaling = running_->scaling;
  sample.flow_raw = frame.flow;
  sample.flow_ml_per_min = FlowMlPerMin(frame.flow, scaling);
  sample.has_flow2 = frame.has_flow2;
  sample.flow2_ml_per_min = frame.has_flow2 ? FlowMlPerMin(frame.flow2, scaling) : 0;
  sample.has_temperature = frame.has_temperature;
  sample.temperature_mdegc =
      frame.has_temperature ? TemperatureMdegC(frame.temperature, p.temperature) : 0;
  sample.gas = running_->gas;
  sample.pure_gas = !running_->takes_argument;
  sample.o2_permille = running_->takes_argument ? o2_permille_ : uint16_t{0};
  sample.timestamp_us = detail::NowUs(port_);
  if constexpr (detail::HasNowUs<Port>) {
    sample.settled = (sample.timestamp_us - started_us_) >= p.timing.warm_up_us;
  }
  if (!frame.has_status) {
    return Error::Ok;
  }
  sample.has_status = true;
  sample.status = frame.status;
  sample.smoothed = detail::IsSmoothed(p, frame.status);
  const detail::StatusContext ctx{.gas_index = running_->status_index,
                                  .permille = o2_permille_,
                                  .averages = averaging_,
                                  .mixture = running_->takes_argument,
                                  .filter_given = config_.filter_enabled};
  return detail::CheckStatus(p, frame.status, ctx);
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::captureReply() noexcept {
  const Result<detail::Words> words = detail::ParseWords(part_->crc_init, rx());
  if (!words) {
    return words.error;
  }
  last_reply_ = RawReply{};
  last_reply_.count = words.value.n;
  last_reply_.words = words.value.w;
  return Error::Ok;
}

template <I2cPort Port, uint32_t FamilyMask>
Result<RawReply> SfmFlowMeter<Port, FamilyMask>::ReadRawReply() noexcept {
  const Error g = gate(state_ == State::Measuring);
  if (g != Error::Ok) {
    return Result<RawReply>::Failure(g);
  }
  const Result<Wait> bus = begin(makeRequest(detail::Op::ReadSample));
  if (!bus) {
    return Result<RawReply>::Failure(bus.error);
  }
  noteFrame();
  const Error e = captureReply();
  if (e != Error::Ok) {
    stats_.crc_errors += (e == Error::Crc) ? 1U : 0U;
    return Result<RawReply>::Failure(e);
  }
  return Result<RawReply>::Success(last_reply_);
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::decodeSample(Sample& sample) noexcept {
  static_cast<void>(captureReply());
  const Result<detail::Frame> frame = detail::DecodeFrame(*part_, rx());
  if (!frame) {
    stats_.crc_errors += (frame.error == Error::Crc) ? 1U : 0U;
    return frame.error;
  }
  const Error e = fillSample(frame.value, sample);
  if (e != Error::Ok) {
    return e;
  }
  const Error range = checkRange(sample);
  return (range != Error::Ok) ? range : checkFresh(sample);
}

template <I2cPort Port, uint32_t FamilyMask>
void SfmFlowMeter<Port, FamilyMask>::noteFrame() noexcept {
  // Any reply the sensor delivered, whether the checks accept it or not, proves
  // it is measuring.
  last_frame_us_ = detail::NowUs(port_);
}

template <I2cPort Port, uint32_t FamilyMask>
bool SfmFlowMeter<Port, FamilyMask>::silenceExceeded() const noexcept {
  if constexpr (detail::HasNowUs<Port>) {
    return (detail::NowUs(port_) - last_frame_us_) > detail::SilenceBoundUs(*part_, averaging_);
  } else {
    // No clock, no bound: a count of NACKed reads says nothing about time (a
    // period is up to 64 ms). The caller times the silence and then calls
    // `Stop()`, a real command.
    return false;
  }
}

template <I2cPort Port, uint32_t FamilyMask>
Error SfmFlowMeter<Port, FamilyMask>::escalateSilence() noexcept {
  // No new sample for longer than the datasheet allows. Ask with the next real
  // command (never an empty write): a sensor that does not acknowledge is gone;
  // one that does has also been told to stop, so the measurement is over either
  // way and the state follows it.
  const detail::Step cmd = detail::PollCommand(*part_);
  if (!port_.Write(address_, cmd.bytes.data(), cmd.len)) {
    ++stats_.bus_errors;
    return Error::NoDevice;
  }
  ++stats_.device_resets;
  state_ = State::Idle;
  running_ = nullptr;
  quiet_start_us_ = detail::NowUs(port_);
  quiet_us_ = part_->timing.stop_us;
  detail::Log(port_, LogLevel::Warning, "sfm_flow_meter: the sensor left the measurement");
  return Error::DeviceReset;
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Sample> SfmFlowMeter<Port, FamilyMask>::sampleFailure(Error error) noexcept {
  const PartInfo& p = *part_;
  if ((error == Error::NoData) && (p.family != Family::Sf04)) {
    if (!silenceExceeded()) {
      return Result<Sample>::Failure(error); // no new sample yet: the sensor at work, not a fault
    }
    error = escalateSilence();
    if (error == Error::DeviceReset) {
      return Result<Sample>::Failure(error);
    }
  }
  if (Has(p, feature::kFirstByteAck)) {
    ++stats_.consecutive_errors;
    if (stats_.consecutive_errors >= config_.consecutive_error_limit) {
      state_ = State::StateUnknown; // the caller decides: `HardReset()` cycles
                                    // the supply
      stats_.consecutive_errors = 0U;
      detail::Log(port_, LogLevel::Error, "sfm_flow_meter: too many errors in a row");
      return Result<Sample>::Failure(Error::StateUnknown);
    }
    return Result<Sample>::Failure(error);
  }
  return Result<Sample>::Failure(error);
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Sample> SfmFlowMeter<Port, FamilyMask>::ReadSample() noexcept {
  const Error g = gate(state_ == State::Measuring);
  if (g != Error::Ok) {
    return Result<Sample>::Failure(g);
  }
  const Result<Wait> bus = begin(makeRequest(detail::Op::ReadSample));
  if (!bus) {
    return sampleFailure(bus.error);
  }
  noteFrame();
  Sample sample{};
  const Error e = decodeSample(sample);
  if (e != Error::Ok) {
    return sampleFailure(e);
  }
  stats_.consecutive_errors = 0U;
  return Result<Sample>::Success(sample);
}

template <I2cPort Port, uint32_t FamilyMask>
Result<Wait> SfmFlowMeter<Port, FamilyMask>::ReadTemperature() noexcept {
  const Error g = gate(state_ == State::Measuring);
  if (g != Error::Ok) {
    return Result<Wait>::Failure(g);
  }
  if (!Has(*part_, feature::kTemperatureRead)) {
    return Result<Wait>::Failure(Error::Unsupported);
  }
  return begin(makeRequest(detail::Op::ReadTemperature));
}

} // namespace sfm

#endif // SFM_FLOW_METER_IPP
