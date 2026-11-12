# SFM flow meter ESP32 examples

ESP-IDF applications that run the driver on an ESP32-C6. They are compiled by CI on every pull request. They have
not been run against a sensor: the driver is verified against a model of the chip, not against hardware (see
[the contract](../../docs/contract.md)).

## Wiring (ESP32-C6)

| ESP32-C6 | Sensor | Notes |
| --- | --- | --- |
| GPIO6 | SDA | I2C0, 100 kHz; external pull-ups to the bus voltage are required (the port can enable the internal ones as a bench aid only, `internal_pullup`, off by default) |
| GPIO7 | SCL | |
| 3V3 or 5 V | VDD | The SFM3000/3200/3300/3400, SFM4200 and SFM4100 need 5 V; the SFM3xxx SF06 parts and SFM3304 run on 3.3 V |
| GND | GND | |
| GPIO10 | supply switch | `legacy` example only: a load switch on the sensor supply, used by `PowerCycle()` |

The connector of each module is on the [hardware page](../../docs/hardware.md#connectors). A 5 V sensor needs a
level shifter or a bus that tolerates 5 V; the ESP32-C6 pins are 3.3 V only.

## Build and flash

```bash
git submodule update --init scripts      # the ESP-IDF build scripts
. $IDF_PATH/export.sh                     # ESP-IDF v5.5
./scripts/build_app.sh list
./scripts/build_app.sh sfm_flow_meter_basic_example Debug
./scripts/flash_app.sh flash_monitor sfm_flow_meter_basic_example Debug
```

`idf.py menuconfig` shows "SFM flow meter driver" under Component config: averaging, the scaling check, the range
flag, the timing source, the general-call reset, the restart before read and the error limit that
`sfm::KconfigDefaults()` returns.

## The examples

| App | What it shows |
| --- | --- |
| `sfm_flow_meter_basic_example` | An SFM3003-300-CL, blocking calls, menuconfig options, recovery from a sensor reset |
| `sfm_flow_meter_task_example` | An SFM4300-20-B mixture, the waits owned by a FreeRTOS task, a live oxygen update |
| `sfm_flow_meter_legacy_example` | An SFM3400-AW: temperature, the error limit and the caller's `HardReset()` (power cycle through the port) |

## Expected output (basic)

```text
I (312) sfm_flow_meter_example: SFM flow meter driver 0.1.0
I (330) sfm_flow_meter_example: product 0x04020111, serial 0x00A1B2C3
I (850) sfm_flow_meter_example: 0 ml/min, 25100 mdegC
```

An `EnsureInitialized failed: NoDevice` line means nothing acknowledged the address: check the wiring, the supply
and the pull-ups first. `DeviceIdMismatch` means a sensor answered, but it is not the configured part.

## Files

| File | What it is |
| --- | --- |
| [`main/esp32_sfm_flow_meter_port.hpp`](main/esp32_sfm_flow_meter_port.hpp) | The ESP-IDF port: bus, stretching read, general call, power and reset pins, delay, clock, log |
| [`main/sfm_flow_meter_basic_example.cpp`](main/sfm_flow_meter_basic_example.cpp) | The basic example |
| [`main/sfm_flow_meter_task_example.cpp`](main/sfm_flow_meter_task_example.cpp) | The task example |
| [`main/sfm_flow_meter_legacy_example.cpp`](main/sfm_flow_meter_legacy_example.cpp) | The legacy example |
| [`main/sfm_flow_meter_cxx20_check.cpp`](main/sfm_flow_meter_cxx20_check.cpp) | Compiles the public headers as ISO C++20 |
| [`CMakeLists.txt`](CMakeLists.txt) | The ESP-IDF project; `EXTRA_COMPONENT_DIRS` points at the repository root |
| [`app_config.yml`](app_config.yml) | The app matrix for the scripts, CI and the docs |
