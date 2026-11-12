# Configuration

`Config` selects the part and the policies; `PresetFor(variant)` gives the datasheet configuration of a part. Nothing
in `Config` is sent to the sensor except the averaging and the filter argument.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - Sf05 reset, restart and first read: Sensirion AG SFM3200 1.1, Digital interface (soft reset 80 ms, restart before read), [p. 5](https://sensirion.com/media/documents/70FC8320/62962251/GF_DS_SFM3200.pdf#page=5)
    - Dual-gas frame of 24 bits: Sensirion AG SFM3505 D3 1.0, Digital interface description (24-bit frame, commands), [p. 10](https://sensirion.com/media/documents/03EE880B/68F8CF90/GF_DS_SFM3505.pdf#page=10)
    - Concentration update: Sensirion AG SFM4300 D1 1.0, 4.3.3 Update concentration, [p. 11](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=11)
    - Averaging: Sensirion AG SFM4300 D1 1.0, 3.1 Measurement modes; 4.3.5 Configure averaging, [p. 12](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=12)
    - Scale factor, offset and unit: Sensirion AG SFM4300 D1 1.0, 4.3.6 Read scale factor, offset and flow unit; 4.5 Unit conversion, [p. 13](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=13)
    - Product identifier and serial number: Sensirion AG SFM4300 D1 1.0, 4.3.9 Read product identifier, Table 13, [p. 15](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=15)
    - Sfx6000 frame, start codes and addresses (preliminary): Sensirion AG SFx6000 I2C 1.1, I2C interface reference (preliminary), [p. 6](https://sensirion.com/media/documents/3D857436/65156AC0/GF_AN_SFC6xxx_I2C_InterfaceReference1.1.pdf#page=6)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## The fields

`Config{}` is valid: automatic identification inside the SF06 family, the datasheet reset behaviour, strict scaling
checks and conservative timing.

| Field | Default | What it does |
| --- | --- | --- |
| `variant` | `Auto` | The part. `Auto` identifies it by product identifier inside `family`; a named variant also fixes the family, the address set and every table value |
| `family` | `Sf06` | Used with `Auto` only; the SF05 and SF04 families have no product identifier and need a named variant |
| `address` | 0 | The 7-bit address; 0 is the part's default. It must be in the part's set |
| `averaging` | `kAveragingDefault` | N = 0 (average until read, where offered), 1 to 128, or the family's reset value |
| `filter_enabled`, `filter` | off, 50961 | SFM3304 and SFM3505: send the signal-filter argument with the start command (50961 is the default of 3 ms) |
| `scaling_check` | `Strict` | What `ReadScaling()` does when the sensor's values differ from the datasheet: `Strict` is `DeviceFault`, `Lenient` accepts a scale within 1 %, `Off` uses the sensor's values (ambiguity K-SCALE) |
| `range_policy`, `range_margin_permille` | `Reject`, 20 | A flow outside the part's span plus the margin is `OutOfRange`; `Flag` returns it with `out_of_range` set. Never clamped (K-RANGE) |
| `require_settled` | off | A sample before the warm-up time is `Stale` (needs `NowUs()`) (K-WARM) |
| `reject_smoothed` | off | A sample with the smoothing bit set is `Stale` (K-SMOOTH) |
| `max_age_us` | 0 | When set, a read later than this after the previous good one is `Stale` |
| `accept_prototype` | off | A prototype product identifier (revision high nibble 8) is `DeviceIdMismatch` unless set (K-ID) |
| `allow_general_call_reset` | off | Permit the general-call soft reset; it resets every device on the bus that listens (K-GC) |
| `timing` | `Conservative` | 20 ms for every SF06 reset and wake; `Datasheet` uses the part's own waits and needs a known variant (K-TIME) |
| `consecutive_error_limit` | 5 | SF05: errors in a row before `StateUnknown`; the caller then runs `HardReset()` (K-SF05-LOCK) |
| `restart_before_read` | on | SF05: send the start command again after each read (K-SF05-RESTART) |
| `sf05_serial_low_word` | on | SF05: also read the low serial word through 0x31AF; off sends 0x31AE only (K-SF05-SERIAL) |
| `poll_with_identifier` | off | SF06 families: idle polls send the identifier command instead of stop, if a bench shows a sensor refuses stop while idle (K-STOPIDLE) |

`Validate(config)` checks a configuration without touching the sensor. It fails when an enumeration holds a value that
is not an enumerator, the family is not the part's, the address is not in the part's set, `Auto` is combined with
`TimingPolicy::Datasheet` or an SF05 or SF04 family, averaging or the filter is used on a part without that feature,
averaging is out of range, or the error limit is 0. `EnsureInitialized()` calls it, so an invalid configuration is
`InvalidArgument` before any bus traffic.

## Presets and checked configurations

```cpp
--8<-- "tests/docs/configuration.cpp:custom"
```

`PresetFor(variant)` is `Config{}` with the variant, family and default address of the part's row. A
`CheckedConfig` fails the build when `Validate()` rejects its argument, which proves a board's configuration at
compile time; the driver still validates at start-up for configurations built at run time.

## Averaging

The SF06 parts average the flow signal on the sensor. After a reset they are in "average until read" (N = 0): every
reading is the mean of all samples since the last read, which prevents aliasing but turns into an exponentially
smoothed value after a read gap longer than 64 ms (status bit 11, `Sample::smoothed`). A fixed N from 1 to 128 makes
every reading the mean of N samples. `SetAveraging(n)` works while idle; `Config::averaging` is written at
initialisation and again after a wake (K-SLEEP-AVG). The SFM3304 and SFM3505 take the same N and a filter argument.

## Kconfig

With ESP-IDF, `idf.py menuconfig` shows "SFM flow meter driver" under Component config. The options are
`AVERAGING`, `SCALING_CHECK`, `RANGE_FLAG`, `TIMING_DATASHEET`, `ALLOW_GENERAL_CALL_RESET`,
`NO_RESTART_BEFORE_READ` and `ERROR_LIMIT`. `sfm::KconfigDefaults()` returns the `Config` they describe; without
Kconfig every option falls back to the datasheet default, so it equals `Config{}`. The variant and the address are
not options: they name the part, so the application sets them.

```cpp
--8<-- "tests/docs/configuration.cpp:kconfig"
```

## One preset per part

Every row of [the variant table](hardware.md#variants) is a `Variant` enumerator named after the vendor's suffix,
for example `Variant::SFM4300_20_B`, `Variant::SFM3003_300_CL`, `Variant::SFM3304_250_D` or `Variant::SFM4100_O2`.
Pass it to `PresetFor()`. The SFM6000D variants are `SFM6000D_50`, `SFM6000D_20` and `SFM6000D_5`; every fact about
them is preliminary.
