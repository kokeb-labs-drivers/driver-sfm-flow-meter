# Testing and CI

How the driver is checked: host tests against an independent model of the sensor for every part, fault injection, a
fuzzer that corrupts what the sensor sends back, compiled documentation, compile-fail tests, an install test, ESP32 and
Cortex-M builds, static analysis and measured quality gates, on every pull request.

!!! warning "No hardware yet"
    Everything on this page runs against a model of the sensor. The model is written from the same documents as the
    driver, so a misreading shows up in both. [What is not verified](contract.md#what-is-not-verified) lists what only
    a bench can settle.

## Host tests

The tests build the real driver against a model of the sensor, with the firmware's flags, `-fno-exceptions -fno-rtti
-Wswitch-enum -Wconversion -Wsign-conversion`, and warnings as errors, on GCC and Clang.

```bash
cmake -S . -B build -DSFM_FLOW_METER_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| File | Covers |
| --- | --- |
| [`tests/test_all_parts.cpp`](../tests/test_all_parts.cpp) | Every one of the 32 variants through the whole life cycle: identify, scaling, start, sample, stop, reset |
| [`tests/test_parts.cpp`](../tests/test_parts.cpp) | The table against hand-typed datasheet expectations, `Validate()`, presets, `CheckedConfig`, the CRC vectors of both initial values |
| [`tests/test_sf06.cpp`](../tests/test_sf06.cpp) | SF06: framing, commands, mixtures, concentration updates, averaging, status checks, sleep and wake, a sensor that NACKs between samples (N = 1 and N = 128 stay `Measuring`, with and without a clock, 20000 tight reads), device loss only after the silence bound, rejected frames and raw reads keep the silence clock running, no escalation without a clock, the stop time after a reported reset, start-up from a sleeping sensor, NACKed polls are not bus errors, the identifier poll option, no empty write, no soft reset in sleep, stuck lines |
| [`tests/test_gen2_dual.cpp`](../tests/test_gen2_dual.cpp) | SFM3304 (filter argument, status layout) and SFM3505 (24-bit frame, two flows) |
| [`tests/test_sf05.cpp`](../tests/test_sf05.cpp) | Legacy pointer commands, EEPROM scale, first-byte rule, restart before read, temperature by pointer, the two-pointer serial number and the SFM3000 whose 0x31AF is refused, oxygen refused on the SFM3200 and SFM4200 and nitrogen at the air scale where listed, getters cleared after a failed call, the error limit leading to `StateUnknown` and a caller's `HardReset()` |
| [`tests/test_sf04.cpp`](../tests/test_sf04.cpp) | SFM4100: trigger bytes, stretching read, one gas per address |
| [`tests/test_sfx.cpp`](../tests/test_sfx.cpp) | SFM6000D: every `[P]` fact has a test that flips it |
| [`tests/test_states.cpp`](../tests/test_states.cpp) | The state machine: every call in every state, `Busy`, `StateUnknown`, `Poll()` |
| [`tests/test_errors.cpp`](../tests/test_errors.cpp) | The base `Error` values are 0 to 9; every error value (16 and up) is returned by some call |
| [`tests/test_kconfig.cpp`](../tests/test_kconfig.cpp) | `KconfigDefaults()` with and without `CONFIG_` values |
| [`tests/test_version.cpp`](../tests/test_version.cpp) | The version getters follow the generated header (also against a 9.8.7 probe) |
| [`tests/fuzz/`](../tests/fuzz/fuzz_sfm_flow_meter.cpp) | The fuzz target; CTest replays its corpus, see below |
| [`tests/compile_fail/`](../tests/compile_fail/not_a_port.cpp) | Eight pieces of code that must not compile, each for its `EXPECT:` diagnostic: a class that is not a port, a hook without `noexcept`, a port without `DelayUs()` in a blocking call, an invalid `CheckedConfig`, an SFM4100 object without `ReadStretch()`, a `PowerCycle()` returning `void`, a bad family mask, an enumeration whose last-enumerator trait is missing |
| [`tests/docs/`](../tests/docs/my_port.hpp) | Every C++ block of the documentation, compiled (target `sfm_flow_meter_doc_snippets`) |
| [`examples/host/`](../examples/host/README.md) | The host example, built with the test flags and run (`sfm_flow_meter_host_example`) |
| [`tests/check_install.cmake`](../tests/check_install.cmake) | Installs into a scratch prefix and builds [`tests/install_consumer/`](../tests/install_consumer/CMakeLists.txt) with `find_package(sfm_flow_meter)` |
| [`tests/check_doxygen.cmake`](../tests/check_doxygen.cmake) | Doxygen with `_config/Doxyfile` and warnings as errors, when `doxygen` is installed |

### The model

[`tests/model_sfm.hpp`](../tests/model_sfm.hpp) is a sensor written as a behaviour, not as a copy of the driver: a
`Chip` that takes a transaction at the bus, decodes it as the datasheet describes, enforces the timing (the first
sample after a start, the reset and wake times, the half millisecond of the SF05 restart), and keeps a log of every
transaction. It can NACK reads between samples (`no_new_sample`, one result every N x 0.5 ms), counts empty writes and
commands the datasheet forbids in the mode (a soft reset in sleep), and accepts a wake on any write that reaches its
address. It keeps its own clock of the last frame it delivered (`last_served_us`) and can refuse the second serial
pointer (`no_serial_low`). [`tests/datasheet_expect.hpp`](../tests/datasheet_expect.hpp) holds the expectations of
each variant typed by hand from the documents (address, scale, offset, span, timing, product identifier), so the table
in the driver is compared with a second copy and not with itself.

The model is independent in the way that counts for a typo: it does not include the driver's register or type
headers for its constants. It is not independent of the documents. Part of its typing was derived from the table and the
design notes, so a misreading of a document appears in both. That is the honest limit of testing without a bench, and
every ambiguity that rests on it has a knob ([Contract](contract.md#ambiguities-and-their-knobs)).

Faults the model injects, one knob each: a failed write or read in turn (the nth transfer), a flipped byte of a read, a
stuck line (all zeros or all ones), a bad status nibble or status XOR, a wrong scale, unit, product identifier or
serial, a sensor that ignores a stop, a sensor that is gone (`dead`), a strict-restart mode, and a bus address
different from the configured one.

## Fuzz tests

Everything the driver reads comes from the sensor over the bus, so the data read is what a noisy cable, a wrong part or
a broken sensor controls. [`tests/fuzz/fuzz_sfm_flow_meter.cpp`](../tests/fuzz/fuzz_sfm_flow_meter.cpp) lets libFuzzer
choose a part, a configuration and a sequence of calls; between the calls it changes the sensor: fails a transfer,
flips a bit, sticks the line, reports another product identifier or status, resets the sensor under the driver,
changes the scale it reports and advances the clock. After every call it checks 17 invariants and aborts, naming the
line, when one breaks:

1. a failed call returns an `Error` the enumeration defines;
2. a failed `ReadSample()` returns a zero `Sample`;
3. a call that returns `NotInitialized`, `Busy`, `InvalidArgument` or `Unsupported` sent nothing (`StateUnknown` can
   follow traffic: the SF05 error limit);
4. a failed call leaves nothing pending;
5. a returned wait is at most one second;
6. every read has a whole number of three-byte words and at most 18 bytes;
7. a returned flow lies inside the span plus the margin unless `RangePolicy::Flag` flagged it;
8. `ReadSample()` succeeds only while `Measuring`;
9. `Measuring` is entered only by a successful `StartMeasurement()` that finished;
10. a successful reset leaves the driver `Uninitialized`;
11. the counters never decrease;
12. `has_flow2`, `has_status` and `has_temperature` follow the part's capabilities;
13. `StateUnknown` is left only by an initialisation (to `Idle`, or `Uninitialized` if it failed) or a reset;
14. a read leaves `Measuring` only with `DeviceReset` or `StateUnknown`, never for "no new sample";
15. no write is ever empty and no command is sent that the datasheet forbids in the current mode;
16. `DeviceReset` follows real silence: the model, with its own clock of the last frame it delivered (rejected
    frames included), has been quiet for at least the whole bound (first-result time, N x 0.5 ms with 10 %, 1 ms);
17. a temperature the getter returns lies in the plausible band, -60 to +150 degC: a stuck bus is never data.

Invariant 9 is asserted in both directions: a successful start ends in `Measuring`, and no other call enters it.

```bash
# libFuzzer needs Clang (on macOS, Homebrew's LLVM)
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DSFM_FLOW_METER_BUILD_TESTS=ON -DSFM_FLOW_METER_BUILD_FUZZERS=ON
cmake --build build-fuzz --target fuzz_sfm_flow_meter
./build-fuzz/tests/fuzz/fuzz_sfm_flow_meter -max_total_time=120 tests/fuzz/corpus/fuzz_sfm_flow_meter
```

The same file also builds without libFuzzer as a replay, and CTest replays every input in
[`tests/fuzz/corpus/fuzz_sfm_flow_meter/`](../tests/fuzz/corpus/fuzz_sfm_flow_meter) on GCC and Clang: the seeds
(one or more per family), and every input that ever broke a promise (name it `regress-*`), so a fixed bug stays fixed.

## Documentation snippets

Every C++ block on this site is included from [`tests/docs/`](../tests/docs/my_port.hpp), which the host build
compiles with the driver instantiated over the documented ports. A change to the API that breaks a page breaks the
build first. The README's example is a generated copy of the same file; `kokeb-docs check` fails when it drifts.

## CI workflows

Everything runs from [`ci.yml`](../.github/workflows/ci.yml), which calls one workflow file per check and ends in the
`CI gate` job, the single required check (`CI gate / gate`).

| Job | What it checks |
| --- | --- |
| Host tests | GCC, then Clang (one job), build and run every test above, then build and run `examples/host` on its own |
| Host quality | The gates below: complexity, coverage, sanitizers, fuzzing |
| Cross-compile | `arm-none-eabi-g++` for Cortex-M0+, M4F and M7F with the strict flags, warnings as errors (nothing linked) |
| C++ lint | clang-format and clang-tidy over the sources |
| MISRA | MISRA C++:2023 and the C++20 profile on traced host and Cortex-M builds, at level `high` (see below) |
| Docs | Strict site build (`_config/mkdocs.yml`), figure and generated-region drift, Doxygen (`_config/Doxyfile`), offline link check, Lighthouse |
| Repository checks | DCO on pull requests, the org standard, Markdown and YAML lint, the [Kokeb Labs handbook](https://github.com/kokeb-labs/handbook) rules for `.kokeb/handbook.yml`, template drift and cppcheck over the driver sources; a second job runs the handbook's source checks |
| CI gate | Passes only if every job above passed: the one required check |

The **ESP32 examples** job builds every app in `examples/esp32/app_config.yml`, for each listed ESP-IDF version and
build type, with the repository root as the driver's component, and lints the example sources. The gate needs it too.

The workflows call the shared, versioned workflows of [kokeb-labs/ci-tools](https://github.com/kokeb-labs/ci-tools)
at `@v1`.

## On hardware

The examples ([Examples](examples.md)) are the bench test: each logs a flow twice a second. Blow through the sensor
and watch the value follow. Record the board, the part and the result in the pull request's bench-status section.
Until that is done, the claim of this driver is "verified against a model".

## Quality gates

The `Host quality` job measures the library code, `inc/` and `src/` only (tests and examples are left out), and
fails when a value is over the limits of this repository's assurance level, `high` (set in
[`.kokeb/handbook.yml`](../.kokeb/handbook.yml), defined by the handbook's ADR-0020):

| Gate | Tool and how it is measured | Limit at `high` |
| --- | --- | --- |
| Complexity | lizard 1.24.1, modified cyclomatic complexity (`-m`: a `switch` counts once) per function | CCN at most 15 |
| Function length | lizard, lines of code per function | at most 60 |
| Parameters | lizard, per function | at most 6 |
| Line coverage | GCC 14 `--coverage -O0`, every CTest, gcovr 8.7 `--merge-lines` (a template's lines count once across its instances), `--exclude-unreachable-branches --exclude-throw-branches` | at least 85 % |
| Branch coverage | same run | at least 75 % |
| MC/DC | GCC `-fcondition-coverage`, same run | measured, not gated at `high` (100 % at `safety`) |
| Sanitizers | Clang, `-fsanitize=address,undefined -fno-sanitize-recover=all`, every CTest | no report |
| Fuzzing | libFuzzer with ASan and UBSan, 120 s per target in CI (60 s in the local rehearsal) | no broken promise |

Last container run (GCC 14, driver sources, gcovr 8.7 with `--merge-lines`): lines 1261 of 1293 (97 %), branches
760 of 888 (85 %, above the 75 % floor), conditions (MC/DC, `-fcondition-coverage`) 625 of 716 (87 %), decisions
419 of 456 (91 %). The sanitizer and fuzz gates (60 s) passed. The gate script prints the line total only; the other
figures come from gcovr with `--txt-metric branch`, `condition` and `decision` on the same run, per file in the run
log kept with the review records. `--merge-lines` does not change the decision total (the same with and without it,
and with or without the unreachable and throw exclusions). MC/DC is measured, not gated, at the `high` level: 87 % is
not the 100 % the `safety` level asks for.

The reports are in the run's `host-quality-reports` artifact and its step summary. To run the gates locally:

```bash
# Coverage (GCC 14 or later), then the complexity limits
cmake -S . -B build-cov -DCMAKE_CXX_COMPILER=g++-14 -DSFM_FLOW_METER_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="--coverage -O0 -fcondition-coverage"
cmake --build build-cov && ctest --test-dir build-cov
gcovr --root . --filter inc/ --filter src/ --merge-lines --exclude-unreachable-branches --txt build-cov
lizard -m -C 15 -L 60 -a 6 inc src

# Sanitizers (Clang)
cmake -S . -B build-san -DCMAKE_CXX_COMPILER=clang++ -DSFM_FLOW_METER_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -O1 -g"
cmake --build build-san && ctest --test-dir build-san --output-on-failure
```

The fuzz gate runs locally as shown in [Fuzz tests](#fuzz-tests).

clang-tidy 20 ([`_config/.clang-tidy`](../_config/.clang-tidy): bugprone, CERT, clang-analyzer,
cppcoreguidelines, modernize, performance, portability and readability, each disabled check with its reason) and
cppcheck (strict) run in the `C++ lint` and `Repository checks` jobs and fail on any finding.

The `MISRA` job ([`ci-misra.yml`](../.github/workflows/ci-misra.yml)) runs the shared `ru-misra` workflow of
ci-tools at level `high`: it traces a host build and the Cortex-M build (`sfm_flow_meter_optimised_build`) of every
translation unit, checks them against MISRA C++:2023 and the handbook's C++20 profile (the CodeQL coding-standards
and profile packs, plus the validated clang-tidy and cppcheck checks), and reports each finding against the
handbook's compliance matrix and deviation records. A finding is fixed in the code; a deliberate exception names
its deviation on the line before it (`// codeql::misra_deviation_next_line(MD-CPP-009)` above the `.ipp` include
of `inc/sfm_flow_meter.hpp`). Until the repository is public the job runs with `open_source_prerelease: true`, the
declaration that the code is released under the licence in `LICENSE` and is to be published.
