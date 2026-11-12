# Porting

Write the port class for your MCU's SDK. The driver calls hooks on a class you derive from
`sfm::I2cInterface<YourPort>`; it never calls an SDK. A port is about 20 lines on most platforms.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - Sf04 trigger bytes and the hold-master read: Sensirion AG SFM4100 1.4, Digital interface (trigger, clock stretching, address per gas), [p. 4](https://sensirion.com/media/documents/6A24D8A6/65A002E6/GF_DS_SFM4100.pdf#page=4)
    - Timing (reset, first sample, wake-up, stop): Sensirion AG SFM4300 D1 1.0, 2.4 Timing specification, [p. 5](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=5)
    - I2C address and bus: Sensirion AG SFM4300 D1 1.0, 4 Digital interface description; 4.1 I2C address, [p. 9](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=9)
    - Command framing and read framing: Sensirion AG SFM4300 D1 1.0, 4.2 I2C sequences, [p. 9](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=9)
    - Soft reset (general call): Sensirion AG SFM4300 D1 1.0, 4.3.7 Soft reset, [p. 13](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=13)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## The contract

The sensors have no register map on the bus. A transaction is a raw command write (a command code, optionally an
argument word with its CRC) or a raw read of 16-bit words, each followed by a CRC-8 byte. The port moves bytes; it
never retries, never interprets and never aborts a read early.

| Hook | Contract | Required |
| --- | --- | --- |
| `bool EnsureInitialized() noexcept` | Bring the bus up on the first call; `true` at once afterwards | yes |
| `bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept` | START, `addr` + W, `len` bytes, STOP; `false` on a NACK or a bus error. `len` is at least 1: the driver never sends an empty transfer. A NACK is expected while a sensor sleeps or resets (see below) | yes |
| `bool Read(uint8_t addr, uint8_t* data, std::size_t len) noexcept` | START, `addr` + R, `len` bytes, ACK every byte but the last, STOP; `false` when the address is NACKed (the "no data yet" signal) or on a bus error | yes |
| `bool ReadStretch(uint8_t addr, uint8_t* data, std::size_t len) noexcept` and `static constexpr bool kClockStretch = true` | A read that tolerates the slave holding SCL low | SFM4100 only |
| `bool WriteGeneralCall(uint8_t byte) noexcept` | START, address 0x00 + W, `byte`, STOP | the general-call soft reset of the SF06, SFM3505 and SFM6000D families |
| `bool PowerCycle() noexcept` | Remove and restore the sensor supply, **holding SDA and SCL low while it is off** (see [Hard reset wiring](#hard-reset-wiring)) | optional: `HardReset()` of the SF05 parts and SFM3505, which the caller runs when the SF05 error limit leaves `StateUnknown` |
| `bool SetResetPin(bool asserted) noexcept` | Drive the /RES pin of the SFM4200 (active low), **holding SDA and SCL low while it is asserted** | optional |
| `void DelayUs(uint32_t us) noexcept` | Wait at least `us` microseconds | optional: the blocking calls need it |
| `uint32_t NowUs() noexcept` | A free-running microsecond counter | optional: `settled` and staleness checks need it |
| `void Log(LogLevel, const char*) noexcept` | One log line per event; plain text | optional |

`addr` is the 7-bit address. The hooks are found at compile time: a hook you leave out is not called, and the calls
that need it return `Unsupported` or are refused at build time. A hook you declare with the wrong signature is a
compile error that names it.

The smallest port:

```cpp
--8<-- "tests/docs/my_port.hpp:port"
```

A port with every optional hook, for an SFM4100 (the only part that needs a stretching read):

```cpp
--8<-- "tests/docs/ports.cpp:full-port"
```

A port for the legacy parts, which uses the supply and reset hooks:

```cpp
--8<-- "tests/docs/ports.cpp:hardware-reset"
```

## What the bus must do

| Requirement | Why |
| --- | --- |
| A write ends with a STOP and a read starts with its own START | The documents show a STOP between a command and its read and do not allow or forbid a repeated START (ambiguity K-RS). The driver has no `WriteRead` hook |
| Never abort a read after the first byte | An SF05 sensor locks up when the master NACKs the first data byte; recovery needs a power cycle |
| No empty write, ever | A write has at least one data byte. Wake from sleep and every reset poll send the next real command (the stop command, the SF05 serial pointer, the SF04 register read); a sensor that sleeps or resets does not acknowledge its address and the transfer ends there, as an empty probe would have; an awake one accepts the whole command |
| SCL at most `kMaxClockHz` (1 MHz), 100 kHz when unsure | The limits per part are in `PartInfo::max_clock_hz` and on the [Hardware page](hardware.md#bus-settings). Check your setting with `static_assert` |
| External pull-ups on SDA and SCL | The sensors do not have them, and the datasheets give no value. The weak internal pull-ups of a microcontroller do not suit 400 kHz or 1 MHz or the 5 V SF05 parts (the ESP-IDF port leaves them off unless `internal_pullup` is set, a bench aid only) |
| Clock stretching enabled for the SFM4100 | The sensor holds SCL low while it measures |

`sfm::kRepeatedStart` is `false`, `sfm::kMaxRead` is 18 bytes and `sfm::kMaxWrite` is 5 bytes: size your buffers and
DMA descriptors from them.

## The clock and the delay

| Port has | What works |
| --- | --- |
| `DelayUs()` | `EnsureInitialized()`, `AwaitPending()` and every blocking call; the driver loops over `DelayUs()` |
| `NowUs()` | `Poll()` measures elapsed time itself; `Sample::settled` and `Config::max_age_us` work; `Sample::timestamp_us` is set |
| neither | Only the non-blocking calls: sleep for the wait you were given and call `Poll(elapsed_us)` |

Without `NowUs()` the driver cannot know that a wait ended, so `Poll()` counts the whole wait as elapsed and
`settled` stays `false`.

## Checked at build time

The mistakes a port can make are compile errors, not run-time surprises:

| Mistake | Result |
| --- | --- |
| The class does not derive from `I2cInterface<Self>`, or a required hook is missing | `SfmFlowMeter` is not instantiated: the `I2cPort` concept fails |
| A hook is not `noexcept` or has another signature | The same, naming the hook |
| `DelayUs`, `NowUs`, `Log`, `ReadStretch`, `WriteGeneralCall`, `PowerCycle` or `SetResetPin` declared with the wrong signature | A `static_assert` names the hook |
| A family mask of 0 or with bits outside the families | A `static_assert` |
| `SfmFlowMeter<Port, kFamilySf04>` over a port without `ReadStretch()` | A `static_assert` |
| `CheckedConfig` built from an invalid `Config` | The build fails, naming `ConfigIsInvalid` |

These are tested: `tests/compile_fail/` has one case for each, and CTest runs them.

## Transfers the sensor NACKs by design

These are not bus faults. The driver counts them in `Stats::no_data` or not at all, never in `Stats::bus_errors`.

| Transfer | What the datasheet says | What the driver does |
| --- | --- | --- |
| Any write to a sleeping SF06 sensor | The address is not acknowledged until the sensor is awake (SFM4300 4.3.8); the address phase wakes it | `WakeUp()` and the identification poll send the stop command every 1 ms up to the part's wake bound; a NACK in between is expected; at the bound the result is `Timeout` (wake) or `NoDevice` (initialisation) |
| A write to a sleeping SFM3304 | The address is acknowledged, the data bytes are NACKed until it is awake (SFM3304 4.3.8, Table 11) | The same poll; the two-byte stop command supplies the data bytes |
| Any transfer after a reset or power-up | The sensor does not acknowledge for the reset time (SFM4300 4.3.7) | The driver waits the datasheet time, then polls with the next real command |
| A read before a new sample | The read header is NACKed (SFM4300 3.3, SFM3003 4.3.1, SFM3505 3.3.1); a result comes every N x 0.5 ms | `NoData`, state unchanged. After the silence bound (first-result time plus one averaged period plus 10 % and 1 ms) the driver asks with the stop command: no answer is `NoDevice`, an answer is `DeviceReset` |
| SF05: a read before the first result | The read is NACKed (AN-05 4.1) | `NoData`; the first data byte of a read that is ACKed is never NACKed (lock-up) |
| SFM4200 during /RES | Nothing is acknowledged | The driver waits the reset time, then polls the serial pointer |

## Control pins: idle levels

A port drives each control output to its inactive level before it enables the output and before the first frame, so
configuring it never pulses it. The ESP-IDF port does this in `EnsureInitialized()`.

| Pin | Inactive level | Polarity |
| --- | --- | --- |
| /RES of the SFM4200 | High (released) | Active low |
| Supply switch (`PowerCycle()`) | High (supply on) | Active high; the supply is off only inside `PowerCycle()` |
| SDA, SCL | Released (pulled up) | Held low only inside `PowerCycle()` and while /RES is asserted |

## Hard reset wiring

The SFM4200 datasheet (2.3) says that to interrupt the power to the SF05 chip during the reset, SDA and SCL must be
pulled low at the same time as /RES, and the SFM3505 (3.3.5) says all pins, the communication lines included, must be
at 0 V for a power reset. A sensor that stays powered through the bus pull-ups or the protection diodes does not reset,
so a locked-up SF05 stays locked up. The port's `PowerCycle()` and `SetResetPin(true)` therefore hold SDA and SCL low
for as long as the supply is off or /RES is asserted, and release them afterwards. On the board, switch the supply
of the sensor and its pull-ups together, or make sure the pull-ups cannot back-feed the sensor.

The SF05 error limit does not cycle the supply from inside `ReadSample()` (which never waits): at the limit the state
is `StateUnknown`, and the caller runs `HardReset()`, then `EnsureInitialized()`.

## A port on ESP-IDF

[`examples/esp32/main/esp32_sfm_flow_meter_port.hpp`](../examples/esp32/main/esp32_sfm_flow_meter_port.hpp) is a port
for ESP-IDF's `i2c_master` with every optional hook: a stretching read, the general call, a supply switch for
`PowerCycle()` and a /RES pin, both holding SDA and SCL low, and a bus clear after a timed-out transfer. It is
compiled by CI and has not been run against a sensor.
