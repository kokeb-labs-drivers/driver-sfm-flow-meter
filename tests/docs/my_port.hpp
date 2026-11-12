/**
 * @file my_port.hpp
 * @brief The port the documentation pages show: the smallest port that runs a part of the SFM3xxx
 * SF06 family.
 */
#ifndef SFM_FLOW_METER_DOCS_MY_PORT_HPP
#define SFM_FLOW_METER_DOCS_MY_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "sfm_flow_meter.hpp"

/// Your platform's bus and timer functions, declared here so the snippets compile; the names are
/// examples.
namespace board {
bool I2cBegin() noexcept;
bool I2cWrite(uint8_t addr, const uint8_t* data, std::size_t len) noexcept;
bool I2cRead(uint8_t addr, uint8_t* data, std::size_t len) noexcept;
void SleepUs(uint32_t us) noexcept;
uint32_t MicrosNow() noexcept;
} // namespace board

// --8<-- [start:port]
class MyPort : public sfm::I2cInterface<MyPort> {
public:
  bool EnsureInitialized() noexcept {
    return board::I2cBegin(); // bring the bus up on the first call; true at once afterwards
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    return board::I2cWrite(addr, data, len); // START, addr+W, len bytes (never 0), STOP
  }
  bool Read(uint8_t addr, uint8_t* data, std::size_t len) noexcept {
    return board::I2cRead(addr, data, len); // START, addr+R, len bytes, NACK on the last, STOP
  }
  void DelayUs(uint32_t us) noexcept {
    board::SleepUs(us); // optional; without it only the non-blocking calls work
  }
  uint32_t NowUs() noexcept {
    return board::MicrosNow(); // optional; a free-running microsecond counter
  }
};
// --8<-- [end:port]

#endif // SFM_FLOW_METER_DOCS_MY_PORT_HPP
