# Examples

One folder per platform under `examples/`, each with its own port, built by CI on every pull request. The driver
itself has no platform code; [Platforms](platforms.md) lists what exists.

## Host

[`examples/host/`](../examples/host/README.md) runs the driver on any PC against a simulated sensor, the model of the
host tests ([`tests/model_sfm.hpp`](../tests/model_sfm.hpp)) and its port. It has three scenarios: an SFM4300 mixture
with blocking calls, an SFM3003 with the application owning the waits, and an SFM3400-AW with the legacy error
handling. No hardware:

```bash
cmake -S examples/host -B build-example && cmake --build build-example && ./build-example/sfm_flow_meter_host_example
```

The host tests build it with warnings as errors and run it (CTest `sfm_flow_meter_host_example`).

## ESP32

ESP-IDF applications that run the driver on an ESP32-C6. They are compiled by CI and have not been run
against a sensor. The wiring is in the
[example folder's README](../examples/esp32/README.md) and the port in
[`esp32_sfm_flow_meter_port.hpp`](../examples/esp32/main/esp32_sfm_flow_meter_port.hpp). The project points
`EXTRA_COMPONENT_DIRS` at the repository root, which is the driver's ESP-IDF component.

### The apps

<!-- generated: examples -->
| App | What it shows | Source | CI |
| --- | --- | --- | --- |
| `sfm_flow_meter_basic_example` :material-star:{ title="featured" } | Identify an SFM3003-300-CL, start air and log the flow twice a second (menuconfig options) | [`sfm_flow_meter_basic_example.cpp`](../examples/esp32/main/sfm_flow_meter_basic_example.cpp) | yes |
| `sfm_flow_meter_task_example` | SFM4300-20-B mixture measurement with the waits owned by a FreeRTOS task and a live oxygen update | [`sfm_flow_meter_task_example.cpp`](../examples/esp32/main/sfm_flow_meter_task_example.cpp) | yes |
| `sfm_flow_meter_legacy_example` | SFM3400-AW (legacy command set): flow, temperature and recovery after errors in a row | [`sfm_flow_meter_legacy_example.cpp`](../examples/esp32/main/sfm_flow_meter_legacy_example.cpp) | yes |

Build and flash one (from `examples/esp32`, ESP-IDF environment loaded):

```bash
./scripts/build_app.sh sfm_flow_meter_basic_example Release
./scripts/flash_app.sh flash_monitor sfm_flow_meter_basic_example Release
```

Source of this table: [`examples/esp32/app_config.yml`](../examples/esp32/app_config.yml).
<!-- /generated -->

### Set up once

```bash
git submodule update --init examples/esp32/scripts   # the ESP-IDF build scripts
cd examples/esp32
. $IDF_PATH/export.sh                                 # ESP-IDF v5.5
./scripts/build_app.sh list                           # every app and build type
```

The scripts read [`app_config.yml`](../examples/esp32/app_config.yml) (the same file this page's table comes
from) and build each app as its own firmware image.

### What to read in an example

| You want to | Read |
| --- | --- |
| See the lifecycle and every `Result` checked | `sfm_flow_meter_basic_example.cpp` |
| Own the waits in a task, and update a mixture | `sfm_flow_meter_task_example.cpp` |
| Recover a legacy part after errors in a row | `sfm_flow_meter_legacy_example.cpp` |
| Write an ESP-IDF port with every optional hook | `esp32_sfm_flow_meter_port.hpp` |
| Use menuconfig values | `sfm_flow_meter_kconfig.hpp` and [`_config/Kconfig`](../_config/Kconfig) |
