// clang-format off: the table below must keep one row per line.
/**
 * @file sfm_flow_meter_i2c_interface.hpp
 * @brief The I2C port of the SFM flow meter driver: the CRTP base, the `I2cPort` concept and the
 * bus requirements of every supported family.
 * @copyright Copyright (c) 2026 The driver-sfm-flow-meter authors. SPDX-License-Identifier: MIT
 *
 * @details
 * The driver never calls an SDK. It calls hooks on a class you derive from
 * `sfm::I2cInterface<Self>`. The chip has no register map on the bus: a transaction is a raw
 * command write (a pointer or command code, optionally an argument) or a raw read of 16-bit words
 * that are each followed by a CRC-8 byte.
 *
 * | Hook | Contract | Required |
 * | --- | --- | --- |
 * | `bool EnsureInitialized() noexcept` | Bring the bus up on the first call; `true` at once afterwards | yes |
 * | `bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept` | START, `addr` + W, `len` bytes, STOP; `false` on a NACK or bus error. `len` is at least 1: the driver never sends an empty transfer. A NACK is expected while a sensor sleeps or resets; the driver polls with the next real command | yes |
 * | `bool Read(uint8_t addr, uint8_t* data, std::size_t len) noexcept` | START, `addr` + R, `len` bytes, the master ACKs every byte but the last, STOP; `false` when the address is NACKed (the "no data yet" signal) or on a bus error. It must never abort after the first byte (SF05 lock-up hazard) | yes |
 * | `bool ReadStretch(uint8_t addr, uint8_t* data, std::size_t len) noexcept` and `static constexpr bool kClockStretch = true` | A read that tolerates the slave holding SCL low (SF04, "hold master") | SFM4100 only |
 * | `bool WriteGeneralCall(uint8_t byte) noexcept` | START, address 0x00 + W, `byte`, STOP | reset of SF06 families |
 * | `bool PowerCycle() noexcept` | Remove and restore the sensor supply; hold SDA and SCL low while it is off, so the sensor is not powered through the bus (SFM4200 2.3, SFM3505 3.3.5) | optional |
 * | `bool SetResetPin(bool asserted) noexcept` | Drive the /RES pin of the SFM4200 | optional |
 * | `void DelayUs(uint32_t us) noexcept`, `uint32_t NowUs() noexcept`, `void Log(LogLevel, const char*) noexcept` | waits, a free-running microsecond clock, one log line per event | optional |
 *
 * `addr` is the 7-bit address. The port moves raw bytes and never retries. A write is followed by a
 * STOP and a read by its own START: the SF06 and SFx6000 documents show the read header after a STOP
 * and neither permit nor forbid a repeated START (ambiguity K-RS), so the driver has no `WriteRead`
 * hook. What the chip needs from the bus is below as `constexpr` values; a port checks its
 * settings against them with `static_assert`.
 */
// clang-format on
#ifndef SFM_FLOW_METER_I2C_INTERFACE_HPP
#define SFM_FLOW_METER_I2C_INTERFACE_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>

#include "sfm_flow_meter_detail.hpp"
#include "sfm_flow_meter_types.hpp"

namespace sfm {

/**
 * @name Bus requirements
 * What the SFM family needs from the I2C bus. A port and a board configuration
 * check their settings against these with `static_assert` (handbook
 * KB-CPP-123). Per-part clock limits are in `PartInfo::max_clock_hz`.
 * @{
 */
/// The default 7-bit address of the family `Config{}` selects (SF06; the
/// SFM4300 with ADDR open or at GND). Every part's addresses are in
/// `PartInfo::addresses`; `PresetFor()` fills the right one.
inline constexpr uint8_t kI2cAddress = 0x2A;
/// Highest SCL frequency of the SF06 family and the SFx6000 (datasheet: 1 MHz
/// maximum, 400 kHz typical).
inline constexpr uint32_t kMaxClockHz = 1'000'000;
/// Typical SCL frequency of the SF06 family.
inline constexpr uint32_t kTypClockHz = 400'000;
/// Conservative default clock for any part: the lowest "maximum" of the
/// supported parts is 100 kHz.
inline constexpr uint32_t kSafeClockHz = 100'000;
/// `false`: a command write and the following read are separate transfers with
/// a STOP between them.
inline constexpr bool kRepeatedStart = false;
/// Longest read the driver issues, in bytes (the product identifier: six words
/// with CRC).
inline constexpr std::size_t kMaxRead = 18;
/// Longest write the driver issues, in bytes (a command, one argument word and
/// its CRC).
inline constexpr std::size_t kMaxWrite = 5;
/** @} */

/**
 * @brief CRTP base of the platform I2C port.
 *
 * Derive `class MyPort : public sfm::I2cInterface<MyPort>` and implement the
 * hooks in this file's description. The base declares no hook itself (a
 * forwarding hook would turn a missing override into infinite recursion) and no
 * data; the driver checks your class with the `I2cPort` concept, so a missing
 * or misdeclared hook is a compile error that names it.
 *
 * @tparam Derived Your port class.
 */
template <typename Derived>
class I2cInterface {
public:
  /** @brief Ports are not copied: the driver holds a reference. */
  I2cInterface(const I2cInterface&) = delete;
  /** @brief Ports are not copied: the driver holds a reference. */
  I2cInterface& operator=(const I2cInterface&) = delete;

protected:
  /** @brief Only the derived port is constructed. */
  I2cInterface() noexcept = default;
  /** @brief Destroyed through the derived port only. */
  ~I2cInterface() noexcept = default;
};

/**
 * @brief Satisfied by a class that derives from `I2cInterface<Port>` and
 * implements the required hooks with exactly the types above, `noexcept` and
 * returning `bool`.
 *
 * Each hook's address is matched with `detail::ExactHook`, which takes a member
 * function of exactly the signature and converts no value: a hook declared with
 * a narrower length or without `noexcept` does not satisfy the concept
 * (handbook KB-CPP20-006).
 *
 * @tparam Port The port class.
 */
template <typename Port>
concept I2cPort = std::derived_from<Port, I2cInterface<Port>> && requires {
  detail::ExactHook<Port, bool() noexcept>::Take(&Port::EnsureInitialized);
  detail::ExactHook<Port, bool(uint8_t, const uint8_t*, std::size_t) noexcept>::Take(&Port::Write);
  detail::ExactHook<Port, bool(uint8_t, uint8_t*, std::size_t) noexcept>::Take(&Port::Read);
};

} // namespace sfm

#endif // SFM_FLOW_METER_I2C_INTERFACE_HPP
