/**
 * @file model_sfm.hpp
 * @brief A model of the sensors, written from the datasheets without the
 * driver's headers (only its enumerator names and the `I2cInterface` base):
 * every constant here is typed again, so a digit that is wrong in the driver is
 * not copied into the model (handbook KB-TST-030).
 *
 * `model::Chip` behaves as `datasheet_expect.hpp` says for any part: it ACKs
 * and NACKs as the documents describe, keeps its own mode, answers after the
 * datasheet's waits (a read before the first sample is NACKed, a probe before
 * the reset time is NACKed) and frames results with its own CRC.
 * `model::Port<Hooks>` is the port the driver sees; its optional hooks exist or
 * not by constraint.
 */
#ifndef SFM_FLOW_METER_TEST_MODEL_SFM_HPP
#define SFM_FLOW_METER_TEST_MODEL_SFM_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "datasheet_expect.hpp"
#include "sfm_flow_meter_i2c_interface.hpp"

namespace model {

using expect::Kind;

/// CRC-8, polynomial 0x31, one 16-bit word, no reflection, no final XOR.
constexpr uint8_t Crc(uint8_t init, uint8_t high, uint8_t low) noexcept {
  uint8_t crc = init;
  const std::array<uint8_t, 2> bytes{high, low};
  for (const uint8_t byte : bytes) {
    crc = static_cast<uint8_t>(crc ^ byte);
    for (int bit = 0; bit < 8; ++bit) {
      crc =
          static_cast<uint8_t>(((crc & 0x80U) != 0U) ? ((static_cast<unsigned>(crc) << 1U) ^ 0x31U)
                                                     : (static_cast<unsigned>(crc) << 1U));
    }
  }
  return crc;
}

/// One bus transaction as the sensor saw it.
struct Txn {
  bool write{false};
  bool stretch{false};
  bool acked{false};
  uint8_t addr{0};
  uint8_t len{0};
  std::array<uint8_t, 6> data{};
};

/// What a transfer's data is replaced by (fault injection).
enum class Stuck : uint8_t { None, Zeros, Ones };

class Chip {
public:
  explicit Chip(const expect::PartX& spec) noexcept : spec_(spec) {
    reset(0U, 0U);
  }

  // --- The physical world
  // -------------------------------------------------------------------
  int32_t flow_ml{0};        ///< Flow in ml/min.
  int32_t flow2_ml{0};       ///< SFM3505 oxygen channel in ml/min.
  int32_t temp_mdegc{25000}; ///< Temperature in milli-degrees C.
  uint32_t serial{0x0A1B2C3DU};
  uint8_t bus_address{0};  ///< The address the sensor listens at (0: the part's default).
  uint32_t id_override{0}; ///< Non-zero: the product identifier the sensor reports.
  uint8_t field{1};        ///< SFM4100 calibration field.
  bool smoothing{false};   ///< SF06 status bit 11.
  // --- Faults
  // -------------------------------------------------------------------------------
  int fail_write_in{-1};      ///< The n-th write from now fails (0: the next); -1 never.
  int fail_read_in{-1};       ///< The n-th read from now fails.
  int flip_read_byte{-1};     ///< A read flips this byte index ...
  uint8_t flip_mask{0x01};    ///< ... by this mask.
  Stuck stuck{Stuck::None};   ///< Reads return all zeros or all ones.
  bool bad_status{false};     ///< The status word reports another command.
  bool strict_restart{false}; ///< SF05: a read without the 0x1000 restart is NACKed.
  bool ignore_stop{false};    ///< A stop command is acknowledged but ignored.
  bool dead{false};           ///< Nothing answers.
  uint16_t scale_override{0}; ///< Non-zero: the scale factor a scale read reports.
  uint16_t unit_override{0};  ///< Non-zero: the unit word a scale read reports.
  uint16_t status_xor{0};     ///< XORed into the status word.
  uint16_t or_mask{0};        ///< SF05: bits ORed into the flow word (the two low bits
                              ///< must be zero).
  // --- Observations
  // ---------------------------------------------------------------------------
  std::array<Txn, 128> log{};
  std::size_t logged{0};
  uint32_t writes{0};
  uint32_t reads{0};
  uint32_t power_cycles{0};
  uint32_t resets{0};
  uint32_t now_us{0};

  [[nodiscard]] bool Measuring() const noexcept {
    return measuring_;
  }
  [[nodiscard]] bool Asleep() const noexcept {
    return asleep_;
  }
  [[nodiscard]] uint16_t Averaging() const noexcept {
    return averaging_;
  }
  [[nodiscard]] uint16_t Permille() const noexcept {
    return permille_;
  }
  [[nodiscard]] const expect::PartX& Spec() const noexcept {
    return spec_;
  }
  [[nodiscard]] uint8_t Listen() const noexcept {
    return (bus_address != 0U) ? bus_address : spec_.address;
  }

  /// A reset the driver did not ask for (brown-out): the sensor is idle again.
  /// Writes with no data byte the driver sent: an empty transfer is never part
  /// of a protocol here.
  uint32_t empty_writes{0};
  /// Commands the datasheets forbid in the current mode that the driver sent (a
  /// reset in sleep).
  uint32_t forbidden{0};
  /// Samples are available only once per averaged period (SFM4300 3.2, 3.3): a
  /// read in between is NACKed.
  bool no_new_sample{false};
  /// The time of the last measurement start or the last result frame the model
  /// delivered; the driver may call a sensor silent only after it has been
  /// quiet for a while (the model's own clock, independent of the driver's).
  uint32_t last_served_us{0};
  /// The averaging the driver last wrote (a brown-out resets the chip but not
  /// what the driver knows).
  uint16_t written_averaging{0};
  /// The SFM3000 application note lists the serial pointer 0x31AE only: 0x31AF
  /// is an unknown command (ambiguity K-SF05-SERIAL).
  bool no_serial_low{false};

  void BrownOut() noexcept {
    reset(now_us, spec_.reset_us);
  }

  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    ++writes;
    if (len == 0U) {
      ++empty_writes; // the model refuses what the driver must never send
      record(true, false, false, addr, data, len);
      return false;
    }
    const bool ok = doWrite(addr, data, len);
    record(true, false, ok, addr, data, len);
    return ok;
  }

  bool Read(uint8_t addr, uint8_t* data, std::size_t len, bool stretch) noexcept {
    ++reads;
    const bool ok = doRead(addr, data, len);
    record(false, stretch, ok, addr, data, len);
    return ok;
  }

  bool GeneralCall(uint8_t byte) noexcept {
    ++writes;
    forbidden += asleep_ ? 1U : 0U; // a sleeping sensor cannot be soft reset (SFM4300 4.3.8)
    if (asleep_) {
      return false;
    }
    if (dead || (byte != 0x06U)) {
      return false;
    }
    ++resets;
    reset(now_us, spec_.reset_us);
    return true;
  }

  bool PowerCycle() noexcept {
    ++power_cycles;
    reset(now_us, spec_.reset_us);
    return true;
  }

  bool ResetPin(bool asserted) noexcept {
    held_ = asserted;
    if (!asserted) {
      reset(now_us, spec_.reset_us);
      alive_at_ = now_us + 2000U;
    }
    return true;
  }

private:
  enum class Ptr : uint8_t { Result, Temperature, Scale, Offset, Serial, SerialLow, Queue };

  void reset(uint32_t now, uint32_t boot_us) noexcept {
    measuring_ = false;
    asleep_ = false;
    waking_ = false;
    held_ = false;
    ptr_ = Ptr::Result;
    qn_ = 0;
    qpos_ = 0;
    running_ = 0;
    permille_ = 0;
    filter_arg_ = false;
    need_restart_ = false;
    triggered_ = false;
    averaging_ = (spec_.kind == Kind::Sf06) ? uint16_t{0} : uint16_t{2};
    alive_at_ = now + boot_us;
  }

  void record(bool write, bool stretch, bool ok, uint8_t addr, const uint8_t* data,
              std::size_t len) noexcept {
    if (logged < log.size()) {
      Txn& t = log[logged];
      t.write = write;
      t.stretch = stretch;
      t.acked = ok;
      t.addr = addr;
      t.len = static_cast<uint8_t>(len);
      for (std::size_t i = 0; (i < len) && (i < t.data.size()); ++i) {
        t.data[i] = data[i];
      }
    }
    ++logged;
  }

  static bool countdown(int& counter) noexcept {
    if (counter < 0) {
      return false;
    }
    if (counter == 0) {
      counter = -1;
      return true;
    }
    --counter;
    return false;
  }

  [[nodiscard]] bool alive() const noexcept {
    return !dead && !held_ && (static_cast<int32_t>(now_us - alive_at_) >= 0);
  }

  // --- Writes
  // ---------------------------------------------------------------------------------
  bool probe(std::size_t len) noexcept {
    if (asleep_) {
      if (!waking_) {
        // The address (and for the SFM3304 one more byte) wakes the sensor; it
        // answers later.
        const bool starts =
            (len >= 1U); // the address phase wakes it; the SFM3304 wants a byte more
        if (starts) {
          waking_ = true;
          wake_at_ = now_us + spec_.reset_us;
        }
        return false;
      }
      if (static_cast<int32_t>(now_us - wake_at_) < 0) {
        return false;
      }
      asleep_ = false;
      waking_ = false;
    }
    return true;
  }

  bool doWrite(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    if (countdown(fail_write_in) || (addr != Listen()) || dead || held_) {
      return false;
    }
    if (asleep_) {
      return probe(len); // a sleeping sensor answers nothing but the wake-up
    }
    if (!alive()) {
      return false;
    }
    switch (spec_.kind) {
    case Kind::Sf05:
      return writeSf05(data, len);
    case Kind::Sf04:
      return writeSf04(data, len);
    case Kind::Sf06:
    case Kind::Gen2:
    case Kind::Dual:
    case Kind::Sfx:
    default:
      break;
    }
    return writeSf06(data, len);
  }

  bool argOk(const uint8_t* d, std::size_t len, uint16_t& arg) const noexcept {
    if (len != 5U) {
      return false;
    }
    arg = static_cast<uint16_t>((static_cast<unsigned>(d[2]) << 8U) | d[3]);
    return Crc(0xFF, d[2], d[3]) == d[4];
  }

  void queue(uint16_t word) noexcept {
    if ((qn_ + 3U) <= q_.size()) {
      q_[qn_] = static_cast<uint8_t>(word >> 8U);
      q_[qn_ + 1U] = static_cast<uint8_t>(word & 0xFFU);
      q_[qn_ + 2U] = Crc(spec_.crc_init, q_[qn_], q_[qn_ + 1U]);
      qn_ += 3U;
    }
  }

  void startGas(std::size_t index, std::size_t len, const uint8_t* d) noexcept {
    const expect::GasX& g = spec_.gases[index];
    uint16_t arg = 0;
    if (g.argument) {
      (void)argOk(d, len, arg);
      permille_ = arg;
    } else if (len == 5U) {
      filter_arg_ = true;
    } else {
      filter_arg_ = false;
    }
    measuring_ = true;
    running_ = index;
    ptr_ = Ptr::Result;
    qn_ = 0;
    qpos_ = 0;
    ready_at_ = now_us + spec_.first_sample_us;
    last_served_us = now_us;
  }

  bool scaleRead(const uint8_t* d, std::size_t len) noexcept {
    uint16_t arg = 0;
    if (measuring_ || !argOk(d, len, arg)) {
      return false;
    }
    for (std::size_t i = 0; i < spec_.gas_count; ++i) {
      if (spec_.gases[i].code == arg) {
        last_scale_gas_ = i;
        qn_ = 0;
        qpos_ = 0;
        if (spec_.kind != Kind::Sfx) {
          queueScale(i, 3U); // the others answer the next read; SFx6000 after
                             // its pointer command
        }
        return true;
      }
    }
    return false;
  }

  void queueScale(std::size_t gas, std::size_t words) noexcept {
    const expect::GasX& g = spec_.gases[gas];
    const std::array<uint16_t, 5> all{
        (scale_override != 0U) ? scale_override : static_cast<uint16_t>(g.scale_num),
        static_cast<uint16_t>(g.offset), (unit_override != 0U) ? unit_override : g.unit,
        static_cast<uint16_t>(spec_.span_max / 1000), uint16_t{1}};
    for (std::size_t i = 0; (i < words) && (i < all.size()); ++i) {
      queue(all[i]);
    }
  }

  bool writeSf06(const uint8_t* d, std::size_t len) noexcept {
    if (len < 2U) {
      return false;
    }
    const auto cmd = static_cast<uint16_t>((static_cast<unsigned>(d[0]) << 8U) | d[1]);
    uint16_t arg = 0;
    if (cmd == 0x3FF9U) {
      if (!ignore_stop) {
        measuring_ = false;
      }
      return len == 2U;
    }
    if ((cmd == 0x3677U) && (spec_.kind != Kind::Dual) && (spec_.kind != Kind::Sfx)) {
      asleep_ = !measuring_;
      return !measuring_;
    }
    if (cmd == 0x3661U) {
      return scaleRead(d, len);
    }
    if ((cmd == 0x366AU) || (cmd == 0x364DU)) {
      if (measuring_ || !argOk(d, len, arg)) {
        return false;
      }
      averaging_ = arg;
      written_averaging = arg; // what the driver last wrote; a brown-out does not undo it
      return true;
    }
    if ((cmd == 0xE102U) || (cmd == 0x365BU)) {
      return readIdOrTemperature(len);
    }
    if (cmd == 0xE000U) {
      ptr_ = Ptr::Result;
      return len == 2U;
    }
    if (cmd == 0xE151U) {
      qn_ = 0;
      qpos_ = 0;
      queueScale(last_scale_gas_, 5U);
      return len == 2U;
    }
    if (cmd == 0xE17DU) {
      if (!measuring_ || !argOk(d, len, arg)) {
        return false;
      }
      permille_ = arg;
      return true;
    }
    for (std::size_t i = 0; i < spec_.gas_count; ++i) {
      if (spec_.gases[i].code == cmd) {
        if (measuring_) {
          return false;
        }
        startGas(i, len, d);
        return true;
      }
    }
    return false;
  }

  bool readIdOrTemperature(std::size_t len) noexcept {
    if (len != 2U) {
      return false;
    }
    if ((spec_.kind == Kind::Sfx) && measuring_) {
      ptr_ = Ptr::Temperature; // 0xE102 while measuring points at the temperature
      return true;
    }
    if (measuring_) {
      return false;
    }
    const uint32_t id = (id_override != 0U) ? id_override : spec_.product_id;
    qn_ = 0;
    qpos_ = 0;
    queue(static_cast<uint16_t>(id >> 16U));
    queue(static_cast<uint16_t>(id & 0xFFFFU));
    queue(0x0000);
    queue(0x0000);
    queue(static_cast<uint16_t>(serial >> 16U));
    queue(static_cast<uint16_t>(serial & 0xFFFFU));
    return true;
  }

  bool writeSf05(const uint8_t* d, std::size_t len) noexcept {
    if (len != 2U) {
      return false;
    }
    const auto cmd = static_cast<uint16_t>((static_cast<unsigned>(d[0]) << 8U) | d[1]);
    qn_ = 0;
    qpos_ = 0;
    switch (cmd) {
    case 0x1000:
      // The first start waits for the first result; a restart of a running
      // measurement gives the next result after 0.5 ms (AN-05 table 4.1).
      ready_at_ = now_us + ((measuring_ && (ptr_ == Ptr::Result)) ? 500U : spec_.first_sample_us);
      last_served_us = (measuring_ && (ptr_ == Ptr::Result)) ? last_served_us : now_us;
      measuring_ = true;
      ptr_ = Ptr::Result;
      need_restart_ = false;
      return true;
    case 0x1001:
      ptr_ = Ptr::Temperature;
      return spec_.temp_scale != 0;
    case 0x2000:
      if (spec_.variant == sfm::Variant::SFM4200) {
        return false;
      }
      ++resets;
      reset(now_us, spec_.reset_us);
      return true;
    case 0x30DE:
      measuring_ = false;
      ptr_ = Ptr::Scale;
      return true;
    case 0x30DF:
      measuring_ = false;
      ptr_ = Ptr::Offset;
      return true;
    case 0x31AE:
      measuring_ = false;
      ptr_ = Ptr::Serial;
      return true;
    case 0x31AF:
      measuring_ = false;
      if (no_serial_low) {
        // An unknown command: refused. The pointer stays where it was, so a
        // read that follows would return the previous word (CRC-valid) and the
        // driver must not take it for the low word.
        ptr_ = Ptr::Serial;
        return false;
      }
      ptr_ = Ptr::SerialLow;
      return true;
    default:
      break;
    }
    return false;
  }

  [[nodiscard]] uint16_t eeprom(uint32_t address) const noexcept {
    const uint32_t base = static_cast<uint32_t>(field) * 0x300U;
    if (address == (base + 0x2F8U)) {
      return static_cast<uint16_t>(serial >> 16U);
    }
    if (address == (base + 0x2F9U)) {
      return static_cast<uint16_t>(serial & 0xFFFFU);
    }
    if (address == (base + 0x2B6U)) {
      return (scale_override != 0U) ? scale_override
                                    : static_cast<uint16_t>(spec_.gases[0].scale_num);
    }
    if (address == (base + 0x2B7U)) {
      return (unit_override != 0U) ? unit_override : spec_.gases[0].unit;
    }
    return 0xFFFFU;
  }

  bool writeSf04(const uint8_t* d, std::size_t len) noexcept {
    qn_ = 0;
    qpos_ = 0;
    if (len == 1U) {
      switch (d[0]) {
      case 0xF1:
        triggered_ = true;
        return true;
      case 0xE3:
        queue(static_cast<uint16_t>(static_cast<unsigned>(field) << 4U));
        return true;
      case 0xE9:
        queue(field);
        return true;
      case 0xFE:
        ++resets;
        reset(now_us, spec_.reset_us);
        return true;
      default:
        return false;
      }
    }
    if ((len == 3U) && (d[0] == 0xFAU)) {
      const uint32_t address = ((static_cast<uint32_t>(d[1]) << 8U) | d[2]) >> 4U;
      for (uint32_t i = 0; i < 4U; ++i) {
        queue(eeprom(address + i));
      }
      return true;
    }
    return false;
  }

  // --- Reads
  // ----------------------------------------------------------------------------------
  [[nodiscard]] int32_t rawFlow(int32_t ml, const expect::GasX& g) const noexcept {
    const int64_t num = static_cast<int64_t>(ml) * g.scale_num;
    const int64_t den = int64_t{1000} * g.scale_den;
    const int64_t half = den / 2;
    const int64_t q = (num >= 0) ? ((num + half) / den) : ((num - half) / den);
    return static_cast<int32_t>(q + g.offset);
  }

  [[nodiscard]] uint16_t statusWord() const noexcept {
    const expect::GasX& g = spec_.gases[running_];
    unsigned nibble = g.nibble;
    nibble = bad_status ? ((nibble + 1U) & 0xFU) : nibble;
    if (spec_.kind == Kind::Gen2) {
      const unsigned fixed = (averaging_ != 0U) ? 1U : 0U;
      return static_cast<uint16_t>(((nibble << 12U) | ((smoothing ? 1U : 0U) << 11U) |
                                    (fixed << 10U) | ((filter_arg_ ? 1U : 0U) << 9U) |
                                    (averaging_ & 0x1FFU)) ^
                                   status_xor);
    }
    const unsigned conc = g.argument ? permille_ : 0x3FFU;
    const unsigned fixed = (averaging_ != 0U) ? 1U : 0U;
    return static_cast<uint16_t>(
        ((nibble << 12U) | ((smoothing ? 1U : 0U) << 11U) | (fixed << 10U) | conc) ^ status_xor);
  }

  void putWord(uint8_t* out, std::size_t at, uint16_t word) const noexcept {
    out[at] = static_cast<uint8_t>(word >> 8U);
    out[at + 1U] = static_cast<uint8_t>(word & 0xFFU);
    out[at + 2U] = Crc(spec_.crc_init, out[at], out[at + 1U]);
  }

  [[nodiscard]] uint16_t rawTemperature() const noexcept {
    const int64_t scaled = (static_cast<int64_t>(temp_mdegc) * spec_.temp_scale) / 1000;
    return static_cast<uint16_t>(scaled + spec_.temp_offset);
  }

  [[nodiscard]] static uint16_t clamp16(int32_t v) noexcept {
    if (v < 0) {
      return 0U;
    }
    return static_cast<uint16_t>((v > 65535) ? 65535 : v);
  }

  void frame(std::array<uint8_t, 9>& f) const noexcept {
    const expect::GasX& g = spec_.gases[running_];
    const int32_t raw = rawFlow(flow_ml, g);
    switch (spec_.kind) {
    case Kind::Dual: {
      const auto air = static_cast<uint32_t>(raw) & 0xFFFFFFU;
      const auto oxy = static_cast<uint32_t>(rawFlow(flow2_ml, g)) & 0xFFFFFFU;
      putWord(f.data(), 0, static_cast<uint16_t>(air >> 8U));
      putWord(f.data(), 3, static_cast<uint16_t>(((air & 0xFFU) << 8U) | (oxy >> 16U)));
      putWord(f.data(), 6, static_cast<uint16_t>(oxy & 0xFFFFU));
      break;
    }
    case Kind::Sf05:
      putWord(f.data(), 0, static_cast<uint16_t>((clamp16(raw) & 0xFFFCU) | or_mask));
      break;
    case Kind::Sf04:
      putWord(f.data(), 0, static_cast<uint16_t>(static_cast<int16_t>(raw)));
      break;
    case Kind::Sfx:
      putWord(f.data(), 0, static_cast<uint16_t>(static_cast<int16_t>(raw)));
      putWord(f.data(), 3, 0x0000);
      putWord(f.data(), 6, statusWord());
      break;
    case Kind::Sf06:
    case Kind::Gen2:
    default:
      putWord(f.data(), 0, static_cast<uint16_t>(static_cast<int16_t>(raw)));
      putWord(f.data(), 3,
              static_cast<uint16_t>(
                  static_cast<int16_t>((spec_.temp_scale * (temp_mdegc / 100)) / 10)));
      putWord(f.data(), 6, statusWord());
      break;
    }
  }

  bool doRead(uint8_t addr, uint8_t* out, std::size_t len) noexcept {
    if (countdown(fail_read_in) || (addr != Listen()) || !alive() || (len == 0U) || (len > 18U)) {
      return false;
    }
    std::array<uint8_t, 18> bytes{};
    if (stuck != Stuck::None) {
      for (uint8_t& b : bytes) {
        b = (stuck == Stuck::Ones) ? uint8_t{0xFF} : uint8_t{0x00};
      }
    } else if (!produce(bytes, len)) {
      return false;
    }
    if ((flip_read_byte >= 0) && std::cmp_less(flip_read_byte, len)) {
      bytes[static_cast<std::size_t>(flip_read_byte)] ^= flip_mask;
    }
    for (std::size_t i = 0; i < len; ++i) {
      out[i] = bytes[i];
    }
    return true;
  }

  bool produce(std::array<uint8_t, 18>& bytes, std::size_t len) noexcept {
    if (qn_ != 0U) {
      for (std::size_t i = 0; i < len; ++i) {
        bytes[i] = ((qpos_ + i) < qn_) ? q_[qpos_ + i] : uint8_t{0xFF};
      }
      qpos_ += len;
      if (qpos_ >= qn_) {
        qn_ = 0;
        qpos_ = 0;
      }
      return true;
    }
    if (spec_.kind == Kind::Sf05) {
      return produceSf05(bytes);
    }
    if (spec_.kind == Kind::Sf04) {
      if (!triggered_) {
        return false;
      }
      triggered_ = false;
      std::array<uint8_t, 9> f{};
      frame(f);
      for (std::size_t i = 0; i < 3U; ++i) {
        bytes[i] = f[i];
      }
      return true;
    }
    if (!measuring_ || (static_cast<int32_t>(now_us - ready_at_) < 0)) {
      return false;
    }
    std::array<uint8_t, 9> f{};
    if (ptr_ == Ptr::Temperature) {
      putWord(f.data(), 0, rawTemperature());
    } else {
      frame(f);
    }
    for (std::size_t i = 0; i < f.size(); ++i) {
      bytes[i] = f[i];
    }
    last_served_us = now_us;
    if (no_new_sample) {
      const uint32_t n = (averaging_ > 1U) ? averaging_ : 1U;
      ready_at_ = now_us + (n * 500U);
    }
    return true;
  }

  bool produceSf05(std::array<uint8_t, 18>& bytes) noexcept {
    std::array<uint8_t, 9> f{};
    switch (ptr_) {
    case Ptr::Scale:
      putWord(f.data(), 0,
              (scale_override != 0U)
                  ? scale_override
                  : static_cast<uint16_t>(spec_.gases[0].scale_num / spec_.gases[0].scale_den));
      break;
    case Ptr::Offset:
      putWord(f.data(), 0, static_cast<uint16_t>(spec_.gases[0].offset));
      break;
    case Ptr::Serial:
      putWord(f.data(), 0,
              static_cast<uint16_t>(serial >> 16U)); // SFM4200 5.2: the most significant word
      break;
    case Ptr::SerialLow:
      putWord(f.data(), 0,
              static_cast<uint16_t>(serial & 0xFFFFU)); // and the least significant word
      break;
    case Ptr::Temperature:
      putWord(f.data(), 0, rawTemperature());
      break;
    case Ptr::Result:
    case Ptr::Queue:
    default:
      if (!measuring_ || (static_cast<int32_t>(now_us - ready_at_) < 0) ||
          (strict_restart && need_restart_)) {
        return false;
      }
      need_restart_ = true;
      last_served_us = now_us;
      frame(f);
      break;
    }
    for (std::size_t i = 0; i < f.size(); ++i) {
      bytes[i] = f[i];
    }
    return true;
  }

  expect::PartX spec_;
  std::array<uint8_t, 32> q_{};
  std::size_t qn_{0};
  std::size_t qpos_{0};
  std::size_t running_{0};
  std::size_t last_scale_gas_{0};
  uint32_t alive_at_{0};
  uint32_t ready_at_{0};
  uint32_t wake_at_{0};
  uint16_t permille_{0};
  uint16_t averaging_{0};
  Ptr ptr_{Ptr::Result};
  bool measuring_{false};
  bool asleep_{false};
  bool waking_{false};
  bool held_{false};
  bool filter_arg_{false};
  bool need_restart_{false};
  bool triggered_{false};
};

/// Which optional hooks a `Port` offers.
inline constexpr uint32_t kHookStretch = 1U;
inline constexpr uint32_t kHookGeneralCall = 2U;
inline constexpr uint32_t kHookPower = 4U;
inline constexpr uint32_t kHookResetPin = 8U;
inline constexpr uint32_t kHookDelay = 16U;
inline constexpr uint32_t kHookClock = 32U;
inline constexpr uint32_t kHookLog = 64U;
inline constexpr uint32_t kAllHooks = 127U;

template <bool>
struct StretchTrait {};
template <>
struct StretchTrait<true> {
  static constexpr bool kClockStretch = true;
};

/// The port the driver sees: the chip behind optional hooks (present by
/// constraint).
template <uint32_t Hooks = kAllHooks>
class Port : public sfm::I2cInterface<Port<Hooks>>,
             public StretchTrait<(Hooks & kHookStretch) != 0U> {
public:
  explicit Port(Chip& chip) noexcept : chip_(chip) {}

  bool fail_bus_init{false};
  uint32_t bus_inits{0};
  uint32_t log_lines{0};
  bool log_empty{false};

  bool EnsureInitialized() noexcept {
    ++bus_inits;
    return !fail_bus_init;
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    return chip_.Write(addr, data, len);
  }
  bool Read(uint8_t addr, uint8_t* data, std::size_t len) noexcept {
    return chip_.Read(addr, data, len, false);
  }
  bool ReadStretch(uint8_t addr, uint8_t* data, std::size_t len) noexcept
    requires((Hooks & kHookStretch) != 0U)
  {
    return chip_.Read(addr, data, len, true);
  }
  bool WriteGeneralCall(uint8_t byte) noexcept
    requires((Hooks & kHookGeneralCall) != 0U)
  {
    return chip_.GeneralCall(byte);
  }
  bool PowerCycle() noexcept
    requires((Hooks & kHookPower) != 0U)
  {
    return chip_.PowerCycle();
  }
  bool SetResetPin(bool asserted) noexcept
    requires((Hooks & kHookResetPin) != 0U)
  {
    return chip_.ResetPin(asserted);
  }
  void DelayUs(uint32_t us) noexcept
    requires((Hooks & kHookDelay) != 0U)
  {
    chip_.now_us += us;
  }
  uint32_t NowUs() noexcept
    requires((Hooks & kHookClock) != 0U)
  {
    return chip_.now_us;
  }
  void Log(sfm::LogLevel /*level*/, const char* message) noexcept
    requires((Hooks & kHookLog) != 0U)
  {
    ++log_lines;
    log_empty = log_empty || (message == nullptr) || (message[0] == '\0');
  }

private:
  Chip& chip_;
};

} // namespace model

#endif // SFM_FLOW_METER_TEST_MODEL_SFM_HPP
