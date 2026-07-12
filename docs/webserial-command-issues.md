# WebSerial Command System Issues

Assessment of `old-docs/webserial-command-system.md` against the current
WebSerial implementation, browser interface, command handlers, and flash
telemetry diagnostics.

## Executive summary

The old document describes the general WebSerial architecture correctly, but
it is not accurate enough to use as current operational guidance. In
particular:

1. WebSerial is disabled in the default build.
2. Flash diagnostics are called directly from an asynchronous web callback,
   even though the flash classes are explicitly not thread-safe.
3. A `D` command sent through WebSerial disconnects the connection required to
   send the documented follow-up `C` command.
4. Command output is not automatically routed to the browser under the current
   default configuration.
5. Several dropdown labels and commands do not match their handlers.
6. Diagnostic safety, timing, and expected output are overstated or stale.

The old document should remain historical material. Current operational
documentation should state the build prerequisites and safety limitations
explicitly.

## Scope

This assessment compared `old-docs/webserial-command-system.md` with:

- `src/SerialConfig.h`
- `src/main.cpp`
- `src/main_part2.cpp`
- `src/NetworkManager.h`
- `src/NetworkManager.cpp`
- `src/logs_page.html`
- `src/logs_page.h`
- `src/FlashRingBuffer.h`
- `src/FlashRingBuffer_part2.cpp`
- `src/FlashRingBuffer_part3.cpp`
- `src/FlashTelemetryManager.h`
- The installed WebSerial library
- `docs/flash-testing.md`
- `docs/flash-feature-sol-evaluation.md`

## High-severity issues

### 1. WebSerial is disabled in the default build

The old document presents `/logs` as an available console without clearly
stating that WebSerial must be enabled at compile time.

The current configuration has `USE_WEBSERIAL` commented out:

- `src/SerialConfig.h:13-15`

That macro also controls `NetworkConfig::enableWebSerial`:

- `src/main.cpp:406-410`

When disabled, `NetworkManager` does not call `WebSerial.begin()` and the
`/webserialws` WebSocket endpoint is not registered:

- `src/NetworkManager.cpp:502-508`

The `/logs` HTML page can still be served by the main web server, but its
JavaScript will repeatedly fail to connect to `/webserialws`.

Current build prerequisites should therefore be documented as:

```cpp
#define USE_WEBSERIAL
```

In addition, `writeLogToSerial` must be enabled if command-handler output is
expected in the console.

### 2. Flash diagnostics run from an unsafe asynchronous task context

The WebSerial library receives messages through the asynchronous web server.
`NetworkManager::webSerialReceiveMessage()` invokes the registered command
callbacks directly:

- `src/NetworkManager.cpp:898-957`
- `src/main.cpp:1292-1298`

Extended callbacks call flash diagnostics immediately:

- `src/main_part2.cpp:613-705`

However, both flash classes explicitly prohibit this usage:

- `src/FlashRingBuffer.h:56` says it is not thread-safe and must be called from
  a single task.
- `src/FlashTelemetryManager.h:37` says it must be called from the main-loop
  task only.

The main loop can capture, flush, peek, consume, or migrate telemetry at the
same time that WebSerial invokes POST, DEEP, STRESS, RECOVERY, factory reset,
or a failure-injection operation. There is no mutex or command queue protecting
the manager or ring state.

This invalidates the old document's claim that diagnostics can be run safely
during field deployment.

WebSerial commands that touch telemetry or shared application state should be
queued and executed by the main-loop task. The existing `%` Mako command path,
which stores work for later main-loop processing, demonstrates the intended
direction.

### 3. The documented WebSerial `D` then `C` workflow cannot complete

The old document recommends this browser workflow:

```text
D -> accumulate offline data -> C -> observe draining
```

The `D` handler does the following:

- Sets `wifiTestingBlocked = true`.
- Calls `WiFi.disconnect(true)`, disabling automatic reconnection.
- Persists the blocked state to NVS.

See `src/main_part2.cpp:434-446`.

Once `D` executes, the device is no longer reachable through the WiFi
connection carrying WebSerial. The browser therefore cannot send `C`.
Because the blocked state is persisted, a restart does not automatically
restore connectivity either.

The `C` recovery command must currently be issued through USB serial or another
out-of-band control path. The WebSerial UI should not offer `D` without a clear
warning and recovery instruction.

## Medium-severity issues

### 4. Output routing is documented incorrectly

The old document refers to a `WEB_SERIAL` macro. The actual macro is:

```cpp
USE_WEBSERIAL
```

`src/SerialConfig.h:18-22` selects either WebSerial or USB serial as
`USB_SERIAL_BASE`.

Diagnostic output is also conditional:

- `USB_SERIAL_PRINTF`, `USB_SERIAL_PRINT`, and `USB_SERIAL_PRINTLN` only emit
  output when `writeLogToSerial` is true.
- `src/main.cpp:5` currently initializes `writeLogToSerial` to false.

`NetworkManager` writes its command-received wrapper messages directly to
WebSerial, so the browser may show that a command was received while hiding the
actual output from `processSerialCommand()` or `processExtendedCommand()`.

Therefore, the statement that all diagnostic output and system logs appear in
the browser is false under the current defaults.

### 5. The `F` command does not toggle flash persistence

The old document's production command table correctly describes `F` as showing
the compile-time setting, but later sections call it a toggle.

The current handler only prints whether `USE_FLASH_TELEMETRY` was compiled in:

- `src/main_part2.cpp:463-472`

It cannot alter the active pipeline at runtime. Nevertheless:

- `src/logs_page.html:319` labels it `Toggle Flash Buffer Enable/Disable`.
- The `H` output in `src/main_part2.cpp:529` also calls it a toggle.

Both labels should say `Show Flash Persistence Setting`.

### 6. Dropdown `ota-off` and `reboot` commands are not implemented

The browser dropdown includes:

```text
ota-off
reboot
```

See `src/logs_page.html:328-329`.

The dropdown sends these strings over WebSerial. Neither
`NetworkManager::webSerialReceiveMessage()` nor `processExtendedCommand()` has
a matching handler, so both are forwarded to the unknown-command response.

The separate Reboot shortcut button does work because it calls the `/reboot`
HTTP endpoint. The dropdown Reboot entry does not use that path.

The dropdown entries should be removed or connected to implemented handlers.

### 7. Diagnostic safety is overstated

The old document says the flash diagnostics can be run safely during field
deployment. Actual behavior is more limited:

- `DEEP` is designed as a read-only scan.
- `POST` can rerun recovery with auto-repair enabled. That path can repair a
  torn head and write NVS, so it is not always read-only.
- `STRESS` writes and consumes test records. It refuses to start when the ring
  is non-empty.
- `RECOVERY` writes records, injects a torn append, tears down the ring, and
  reinitializes it. It also refuses to start when the ring is non-empty.

The empty-ring checks reduce the chance of overwriting a backlog, but they do
not make the operations safe while live telemetry runs concurrently. The
check can pass immediately before the main loop commits a new record.

`RECOVERY` also has a known side effect: it resets the ring and reinitializes it
using a null clock callback, disabling age-based flushing for the rest of that
boot. See `docs/flash-feature-sol-evaluation.md`.

### 8. PSRAM diagnostic stubs can report misleading success

When `USE_FLASH_TELEMETRY` is not defined, the original `TelemetryPipeline`
provides compatibility stubs for the diagnostic methods. Those stubs return
success without testing flash.

This keeps compilation and command dispatch uniform, but a WebSerial command
can print `PASSED` even though no flash diagnostic ran. The old document calls
this graceful degradation; operationally, it should instead be reported as
`NOT AVAILABLE IN PSRAM BUILD`.

### 9. Factory reset behavior depends on the selected build

`R` performs a real flash factory reset only when `USE_FLASH_TELEMETRY` is
defined:

- `src/main_part2.cpp:475-487`

In a PSRAM build it prints that flash persistence is disabled and does
nothing. The old document often describes `R` as an unconditional factory
reset and should qualify that behavior.

Because `R` is destructive and the WebSerial interface has no authentication,
the browser should also require explicit confirmation before sending it.

## Stale or incomplete documentation

### Callback registration example

The old document shows direct assignment to callback members. The current API
uses setter methods:

```cpp
networkManager.setWebSerialCommandCallback([](char command) {
    processSerialCommand(command);
});

networkManager.setWebSerialExtendedCommandCallback([](const String& command) {
    processExtendedCommand(command);
});
```

See `src/main.cpp:1292-1298` and `src/NetworkManager.h:206-207`.

### Diagnostic durations

The old document gives estimates such as 2-20 seconds for POST and 30-300
seconds for DEEP. These values were written for the previous implementation.
The rewritten documentation describes POST as normally under one second and
DEEP as taking seconds, but actual duration still depends on the hardware,
watchdog yields, active logging, and flash state.

Timing should be described as an observed benchmark for a named firmware and
device version, not as a guaranteed range.

### Expected diagnostic output

The example POST, DEEP, and STRESS transcripts no longer match the current
implementation. For example, current POST reports:

- Sectors in use, closed, open, and free
- Closed-sector record count
- Unread record count
- State consistency
- Header anomaly count
- Duration

The old examples mention partition tests, performance checks, progress lines,
and wording that the rewritten diagnostics do not emit.

### Missing current commands

The old document does not cover:

- `%`-prefixed commands, which are relayed to Mako through the main loop.
- `TESTING_MODE` failure-injection commands such as `CORRUPT_SECTOR`,
  `CORRUPT_CRC`, `CORRUPT_STATE`, `CORRUPT_POINTERS`, `SIMULATE_POWER_LOSS`,
  `WEAR_TEST`, `RANDOM_CORRUPT`, and `PARTITION_FAIL`.
- The `serial-off`, `ON`, and `OFF` legacy strings recognized by
  `NetworkManager`. `ON` and `OFF` currently have no operational effect.

### Pipeline terminology

The old overview calls the telemetry architecture runtime-switchable. The
application chooses the global pipeline type at compile time with
`USE_FLASH_TELEMETRY`.

`FlashTelemetryManager` internally contains both flash and PSRAM stores and has
storage-mode APIs, but the WebSerial command system does not expose runtime
pipeline switching. The document should distinguish these two concepts.

## Accurate parts of the old document

The following descriptions still match the current system:

- `/logs` serves the custom browser serial-console page.
- The page connects to the WebSerial library at `/webserialws` when WebSerial
  is enabled.
- Single-character commands are routed to `processSerialCommand()`.
- Recognized and unrecognized multi-character commands are routed to
  `processExtendedCommand()` after NetworkManager handles its legacy strings.
- `S`, `F`, `R`, `D`, `C`, `H`, and `?` are the current single-character
  command names.
- `POST`, `DEEP`, `STRESS`, and `RECOVERY` are the current standard extended
  diagnostic names.
- The page provides custom command input, Enter-to-send, a dropdown, Clear,
  Save Log, Copy Log, scroll controls, auto-scroll, resizing, and text-size
  controls.
- Flash versus PSRAM pipeline selection is controlled at compile time.
- The `flashbuf` partition is 10 MB and contains 2,560 4 KB sectors.
- WebSerial routes currently have no authentication, so anyone able to reach
  the device web server can submit commands.

## Recommended remediation order

1. Queue WebSerial commands and execute them from the main-loop task.
2. Decide how an operator can recover after WebSerial `D`; remove the command
   from the browser until an out-of-band or timed recovery mechanism exists.
3. Document and centralize the required `USE_WEBSERIAL` and
   `writeLogToSerial` configuration.
4. Correct the `F`, `ota-off`, and `reboot` dropdown entries.
5. Add confirmation and authentication for destructive commands, especially
   `R` and failure injection.
6. Make PSRAM diagnostic stubs report unavailable rather than passed.
7. Correct POST safety wording and fix the `RECOVERY` clock-callback defect.
8. Replace the stale timing and output examples with current observed output.
9. Add `%` commands and `TESTING_MODE` commands to the command reference, with
   prominent safety warnings.
10. Update `docs/flash-testing.md`, which currently repeats the claim that USB
    and WebSerial are equivalent paths for `D` and `C`.

## Documentation recommendation

`old-docs/webserial-command-system.md` should keep its historical status and
should not claim that its command routing and operating procedures are fully
current.

A replacement current-system document should separate:

- Build and runtime prerequisites
- Read-only operational commands
- Destructive maintenance commands
- Failure-injection commands
- Commands safe for WebSerial callback submission
- Commands that must be deferred to the main loop
- Commands that can intentionally disconnect the management channel

Until task-safe command dispatch and a recoverable WiFi-disconnect workflow are
implemented, WebSerial should be treated as a development and bench interface,
not as a safe remote field-maintenance console.
