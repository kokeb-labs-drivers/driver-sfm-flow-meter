# SFM flow meter host example

The driver on your PC, with no hardware: the port of the sensor model in
[`tests/model_sfm.hpp`](../../tests/model_sfm.hpp) connects the real driver to a simulated sensor, and
[`main.cpp`](main.cpp) runs three scenarios against it.

| Scenario | Part | What it shows |
| --- | --- | --- |
| 1 | SFM4300-20-B | Initialise, start an air and oxygen mixture at 30 percent, read a sample (blocking calls) |
| 2 | SFM3003-300-CE | The application owns the waits: `BeginInitialize()`, `StartMeasurement()` and `Poll()` |
| 3 | SFM3400-AW | A legacy part: temperature by pointer, and the error limit after errors in a row |

## Build and run

From the repository root, with CMake 3.16+ and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/sfm_flow_meter_host_example
```

The program prints what each scenario does and ends with "all scenarios passed". The host tests build this folder
too and run the example (CTest `sfm_flow_meter_host_example`), so CI fails if it stops compiling or a call fails.

## Files

| File | What it is |
| --- | --- |
| [`main.cpp`](main.cpp) | The three scenarios |
| [`CMakeLists.txt`](CMakeLists.txt) | Standalone project; adds the driver with `add_subdirectory(../..)` |
