# Failure Injection API Issues

Assessment of `old-docs/failure-injection-api-reference.md` against the current
failure-injection APIs, WebSerial command parser, flash storage format, and
recovery implementation.

## Executive summary

The old reference preserves the current method names and WebSerial command
names, but most implementation details still describe the pre-rewrite storage
engine. It should not be used as a current API or recovery guide.

The most important issues are:

1. Failure-injection commands execute directly from an asynchronous WebSerial
   callback even though the flash classes are not thread-safe.
2. Sector corruption can destroy live head, tail, or backlog records without
   confirmation or protection.
3. `simulateIncompleteWrite()` can write across a sector boundary.
4. Random corruption can target active data, select duplicate sectors, ignore
   failed writes, and still return success.
5. Several documented recovery procedures say to run POST even though the
   current injection requires a restart or causes permanent sector exclusion.
6. Current persisted state uses NVS and contains different fields from those
   described in the old document.

Failure injection should currently be treated as destructive bench-only code
for devices with no valuable telemetry stored.

## Scope

This assessment covers:

- `old-docs/failure-injection-api-reference.md`
- `src/FlashRingBuffer.h`
- `src/FlashRingBuffer.cpp`
- `src/FlashRingBuffer_part2.cpp`
- `src/FlashRingBuffer_part3.cpp`
- `src/FlashTelemetryManager.h`
- `src/FlashTelemetryManager.cpp`
- `src/main_part2.cpp`
- `src/NetworkManager.cpp`
- The `TelemetryPipeline` compatibility stubs
- `docs/flash-testing.md`
- `docs/flash-feature-sol-evaluation.md`
- `docs/webserial-command-issues.md`

## What remains accurate

The following portions of the old reference still match the current system:

- Failure-injection methods are compiled only when `TESTING_MODE` is defined.
- The public `FlashRingBuffer` and `FlashTelemetryManager` method names are
  unchanged.
- The WebSerial command names and their default numeric arguments are
  unchanged.
- Valid sector indices are 0 through 2559.
- `injectRandomCorruption()` rejects zero and values greater than 25% of the
  ring sector count.
- `acceleratedWearTest()` now requires an empty ring.
- `simulatePartitionFailure()` nulls the partition pointer and requires a
  restart to restore access.
- `enableFailureInjection()` only reports that failure injection is compiled
  in; it does not enable a separate runtime mode.
- The PSRAM `TelemetryPipeline` failure-injection methods are compatibility
  stubs that return false, while `enableFailureInjection()` is a no-op.
- `FlashTelemetryManager` wrappers now require an initialized flash buffer but
  do not require `FLASH_ONLY` storage mode.

The July 2026 accuracy note correctly mentions the new 24-byte sector header,
the empty-ring requirement for wear testing, and the removal of the
`FLASH_ONLY` requirement. The body of the old reference contradicts that note
in several places.

## High-severity issues

### 1. Failure injection runs from an unsafe task context

WebSerial messages are received by the asynchronous web server and processed
directly by `NetworkManager::webSerialReceiveMessage()`:

- `src/NetworkManager.cpp:898-957`

The registered extended callback immediately executes failure injection:

- `src/main_part2.cpp:636-701`

Both storage classes explicitly prohibit concurrent access:

- `src/FlashRingBuffer.h:56` says the ring is not thread-safe and must be used
  from one task.
- `src/FlashTelemetryManager.h:37` says the manager must be called from the
  main-loop task only.

Normal telemetry capture, migration, flushing, upload peeking, and consumption
can therefore race with direct flash writes, NVS corruption, diagnostics, or a
partition-pointer change initiated by WebSerial.

All failure-injection requests should be queued and executed by the main-loop
task after telemetry activity has been paused or placed into an explicit test
state.

### 2. Sector corruption can destroy live telemetry

`injectSectorCorruption()` and `injectCRCCorruption()` validate only that the
ring is initialized and the sector index is in range:

- `src/FlashRingBuffer_part3.cpp:347-365`

They do not reject:

- The current open head sector
- The current tail sector
- Any in-use sector containing unread records
- A non-empty ring
- A sector with a pending peek

Both functions program `0x00000000` over part of the sector header. On the next
scan, the sector no longer has a valid header and is excluded from the ring.
Its records are not reconstructed and are normally lost.

The old reference describes these operations as tests of auto-repair. That is
incorrect. Current recovery treats an invalid header as an unusable sector,
not as a repairable header.

At minimum, commands need an explicit destructive confirmation and should
report whether the selected sector is erased, head, tail, or contains unread
records. A safer default would refuse active sectors unless a separate force
option is supplied.

### 3. `simulateIncompleteWrite()` can cross a sector boundary

The current implementation:

1. Calls `flush()`.
2. Opens a head sector if needed.
3. Writes an 8-byte `RecordHeader`.
4. Writes 32 bytes of partial payload.

See `src/FlashRingBuffer_part3.cpp:377-399`.

The function does not verify that at least 40 bytes remain in the head sector.
The normal write path can leave an open head with 24, 28, 32, or 36 bytes free,
because it closes only when less than the minimum 24-byte slot remains.

In one of those states, the partial payload crosses the 4 KB boundary and can
program bytes in the following physical sector. If that sector holds data or a
valid header, the injection damages more than the intended open-head append.

The injection should rotate to a fresh sector unless the complete injected
byte sequence fits in the current head. It should also check the return value of
the initial `flush()`.

### 4. Random corruption can report false success

`injectRandomCorruption()` selects sectors using `rand() % TOTAL_SECTORS` and
calls one of the two header-corruption methods:

- `src/FlashRingBuffer_part3.cpp:461-475`

The current behavior differs from the old reference in important ways:

- It does not avoid the head, tail, or nearby sectors.
- It does not avoid sectors containing unread data.
- It selects only magic or header-CRC corruption, not usage-field corruption.
- It can select the same sector more than once.
- It does not guarantee the requested number of unique corrupted sectors.
- It ignores every individual method return value.
- It returns true after the loop even when every flash write failed.

For example, after `PARTITION_FAIL`, a random-corruption command can print a
successful injected result even though partition access is disabled.

The function should preselect unique eligible sectors, record each result, stop
or report partial failure, and return true only when the requested number of
corruptions succeeded.

## Method-level accuracy issues

### `injectSectorCorruption(uint32_t sector_index)`

Old reference claims:

- Writes `0xDEADBEEF` to the magic field.
- Tests auto-repair.
- POST should repair the sector.

Current behavior:

- Writes `0x00000000` at header offset 0.
- Validates initialization and sector range only.
- Makes a valid sector header invalid.
- Recovery excludes that sector and loses its records.
- POST may report an in-map anomaly before restart, but recovery does not
  reconstruct the destroyed header.
- After restart, the invalid sector is no longer in the runtime map and POST
  can treat it as erased.

### `injectCRCCorruption(uint32_t sector_index)`

Old reference claims:

- Writes `0xDEADBEEF` to the CRC field.
- POST detects and repairs the CRC.

Current behavior:

- Writes `0x00000000` at header offset 8.
- Invalidates the complete sector header.
- Causes the same sector exclusion and record loss as magic corruption.
- Does not repair or recalculate the CRC.

### `corruptPersistedState()`

Old reference claims:

- Corrupts EEPROM.
- Sets state fields to `0xFFFFFFFF`.
- Corrupts head, tail, used-byte, sequence, and write-count fields.

Current behavior:

- Uses the ESP32 Preferences NVS namespace `flashring` and key `state2`.
- Writes `0xA5` across the entire `FlashRingPersistedState` blob.
- Current state contains version, tail sector, tail sequence, tail offset,
  consumed count, next sequence, write count, and CRC.
- Current state does not contain head-sector or head-used fields.
- Requires restart to exercise `loadPersistedState()` and flash-scan fallback.

The current state structure is defined at `src/FlashRingBuffer.h:109-118`.

### `simulateIncompleteWrite()`

Old reference claims:

- Creates a complete 224-byte record with a correct CRC.
- Leaves a record `valid` flag erased.
- POST detects and truncates it.

Current behavior:

- There is no valid flag in the current record format.
- Writes a header with length 224 and deliberately wrong CRC `0x1234`.
- Writes only 32 payload bytes.
- Leaves runtime `m_head_write_offset` unchanged.
- Requires boot recovery to scan the open head and rebuild it.
- DEEP can report the torn append but does not repair it.
- A normal POST can pass without inspecting the open-head record stream, so
  POST is not a reliable recovery trigger.

### `corruptRingPointers()`

Old reference claims:

- Corrupts runtime head and tail sectors beyond their valid ranges.
- Corrupts head-used bytes.
- Saves those values to EEPROM.

Current behavior:

- Does not change runtime ring pointers.
- Writes a CRC-valid NVS `state2` value.
- Uses tail sector 2559, which is in range.
- Uses a tail sequence that should not match the scanned sector.
- Uses tail offset 4096, consumed count 65535, next sequence 1, and write count
  0.
- Requires restart so boot recovery rejects the sequence mismatch.
- Does not check the result of `putBytes()` and always returns true.

This is better described as stale-but-CRC-valid NVS cursor injection, not
runtime ring-pointer corruption.

### `acceleratedWearTest(uint32_t cycles)`

Current behavior that is missing or wrong in the old reference:

- Refuses to run unless the ring is empty.
- Writes 200-byte records.
- Flushes after every appended record.
- Continues until the head advances to the next sector.
- Reads and consumes the records to keep the ring from filling.
- Yields and logs progress every 10 cycles, not every 100.
- Reports elapsed time and the lifetime program-operation counter.
- Does not maintain a per-sector erase counter.

Because the implementation flushes every record, it does not model normal
assembly-buffer write batching. It is a repeated program/erase exercise rather
than a realistic workload benchmark.

### `injectRandomCorruption(uint32_t num_sectors)`

The documented 25% upper bound remains correct. The documented sector
avoidance, three corruption types, unique target count, and comprehensive
auto-repair expectations are incorrect.

### `simulatePartitionFailure()`

The basic description is correct: it sets `m_partition` to null and requires a
restart to restore access.

Important omitted behavior:

- `m_initialized` remains true.
- Manager wrappers continue past their initialized check.
- Existing flash backlog becomes unreadable until restart.
- New commits may fall back to PSRAM when append failure is propagated.
- The known ignored age-triggered flush result can still report a flash commit
  as successful. See `docs/flash-feature-sol-evaluation.md`.
- Shutdown can still print `SAFE TO POWER OFF` after failed flash operations.

The old reference's broad claim that this proves graceful PSRAM fallback is
therefore too strong.

### `enableFailureInjection()`

This function does not alter state. It prints:

```text
WARNING: failure injection methods active (TESTING_MODE build)
```

The name can imply that a runtime safety gate is being enabled, but no such
gate exists. All compiled injection methods are callable before and after this
function.

## FlashTelemetryManager wrapper issues

The old reference says wrapper methods validate `FLASH_ONLY` mode. Current
wrappers use only `REQUIRE_FLASH_INITIALIZED`:

- `src/FlashTelemetryManager.cpp:422-426`
- `src/FlashTelemetryManager.cpp:482-525`

This means diagnostics and injections work whenever the contained flash ring
initialized successfully, regardless of the current storage mode.

The old usage example is not valid as a success example because it:

- Does not call `enableFlashBuffer(true)`.
- Does not call `init()`.
- Uses bare `FLASH_ONLY` rather than
  `FlashTelemetryManager::FLASH_ONLY` outside class scope.

The example should initialize the manager fully and use a real clock callback
before invoking failure injection.

`enableFailureInjection()` is a special case: it silently does nothing when
the flash ring is not initialized because it cannot return a failure status.

## TelemetryPipeline stub assessment

The old reference accurately lists the PSRAM compatibility stubs. All boolean
failure-injection methods return false and `enableFailureInjection()` does
nothing.

For application-level WebSerial testing, `USE_FLASH_TELEMETRY` is not merely
recommended if actual injection is expected. Without it, commands reach the
PSRAM stubs and cannot modify the flash ring.

Direct unit or component code could instantiate `FlashRingBuffer` or
`FlashTelemetryManager` independently, but that is not what the application
WebSerial commands do.

## Build-configuration omissions

The old reference identifies `TESTING_MODE` and recommends
`USE_FLASH_TELEMETRY`. For browser-command testing it also needs:

```cpp
#define USE_WEBSERIAL
```

The current source has `USE_WEBSERIAL` commented out by default, and
`writeLogToSerial` defaults to false. See `docs/webserial-command-issues.md` for
the complete WebSerial prerequisites and output-routing limitations.

A practical failure-injection build therefore needs:

```text
TESTING_MODE
USE_FLASH_TELEMETRY
USE_WEBSERIAL
writeLogToSerial = true
```

USB-only or direct-API testing does not require WebSerial.

## WebSerial parser issues

The command parser is implemented in `src/main_part2.cpp`, not directly in the
main body described by the old reference. It is compiled into the application
through the project's split-source inclusion pattern.

### Prefix matching is too permissive

These commands use `startsWith()`:

- `CORRUPT_SECTOR`
- `WEAR_TEST`
- `RANDOM_CORRUPT`
- `CORRUPT_CRC`

Strings such as `WEAR_TEST_GARBAGE` are therefore accepted and interpreted
using the default value.

### Numeric arguments are not validated

Arguments are parsed with `String::toInt()` and then passed to unsigned API
parameters. Consequences include:

- `WEAR_TEST -1` can become `UINT32_MAX` cycles.
- Non-numeric text becomes zero.
- `WEAR_TEST 0` performs no cycles but can report completion.
- Large values can cause an impractically long synchronous operation.
- Sector and random-count negatives become large unsigned values and are
  rejected by the API, but the error message does not identify the parse
  problem.

The parser needs exact command-token matching, strict unsigned conversion,
reasonable upper limits, and clear parameter errors.

### Destructive commands have no confirmation

Any WebSerial client can submit corruption commands directly. There is no:

- Authentication
- Confirmation token
- Empty-ring requirement, except for wear testing
- Active-sector warning
- Test-session arming state
- Timeout-based disarming

`ENABLE_FAIL_INJECT` only prints a message and does not provide an actual
safety gate.

## Recovery-procedure issues

The old reference repeatedly recommends POST after each injection. Correct
recovery depends on the injection:

| Injection | Required observation or recovery |
| --- | --- |
| `CORRUPT_SECTOR` | Sector becomes invalid and its records are excluded; POST does not reconstruct it. |
| `CORRUPT_CRC` | Same as magic corruption; no CRC repair exists. |
| `SIMULATE_POWER_LOSS` | Restart to exercise open-head scan and torn-sector rescue. |
| `CORRUPT_STATE` | Restart to exercise NVS CRC rejection and flash-scan fallback. |
| `CORRUPT_POINTERS` | Restart to exercise NVS sequence-mismatch rejection. |
| `WEAR_TEST` | No restart required if it completes, but the ring must be empty. |
| `RANDOM_CORRUPT` | Potential permanent sector exclusion; results may be partial or falsely reported as successful. |
| `PARTITION_FAIL` | Restart is required to restore the partition pointer. |
| `ENABLE_FAIL_INJECT` | No recovery; this command only prints status. |

POST can be useful for reporting some runtime anomalies, but it is not a
general undo or repair operation.

## Error-reporting inaccuracies

Many error strings shown in the old reference do not exist in the current
implementation.

Current behavior generally consists of:

- Silent false returns for invalid initialization or sector arguments.
- Low-level `writeBytes()` logging for ESP partition write failures.
- A generic manager message when the flash ring is not initialized.
- A generic WebSerial result such as `FAILED (not in flash mode or not
  supported)`.

There is no current wrapper error that reports the storage mode, because mode
is no longer checked. Parameter parsing errors are also not distinguished from
unsupported mode or flash-operation failure.

The API would be easier to test with a typed result containing status,
operation, target sector, whether the target was in use, and low-level ESP
error information.

## Performance and memory inaccuracies

The timing table in the old reference has not been validated for the rewritten
implementation and should not be treated as authoritative.

Specific stale claims include:

- Persisted-state corruption is one NVS blob write, not multiple EEPROM field
  writes.
- The wear test's runtime depends on repeated record writes, flushes, sector
  transitions, erases, reads, and consumes.
- Random corruption may target duplicate sectors and its reported count is not
  a count of successful writes.
- Failure injection uses local stack buffers, including two 200-byte buffers
  during wear testing. It does not require persistent heap allocation, but
  "no additional RAM" is not literally correct.

Compiling without `TESTING_MODE` removes these methods and parser branches, so
the old statement about no runtime failure-injection activity in production
builds remains broadly correct.

## Invalid integration examples

### Automated test sequence

The old automated sequence is unsafe because it:

- Does not ensure the ring is empty before sector corruption.
- Assumes POST repairs a destroyed header.
- Runs random corruption without protecting active sectors.
- Assumes a successful return means all random writes succeeded.
- Runs methods directly without coordinating with live telemetry activity.
- Proceeds to wear testing even though earlier steps may have left data or
  invalid sectors.

### Browser JavaScript

The old example uses a `webSerial` JavaScript object with `send()` and
`onMessage()` methods. The current page uses a native WebSocket stored in `ws`:

```javascript
ws.send(command);
ws.onmessage = function(event) {
    // handle event.data
};
```

The old JavaScript example cannot be copied into the current page as written.

## Recommended remediation order

1. Queue all failure-injection commands for execution by the main-loop task.
2. Add a real armed test-session state with explicit confirmation and automatic
   timeout.
3. Refuse head, tail, pending-peek, and unread-data sectors by default.
4. Fix `simulateIncompleteWrite()` so all injected bytes fit in one open head
   sector and propagate the initial flush result.
5. Make random corruption select unique eligible targets and propagate partial
   or complete failure accurately.
6. Validate WebSerial command tokens and numeric arguments, including safe
   upper bounds for cycle counts.
7. Check and return the NVS write result from `corruptRingPointers()`.
8. Rename methods to describe their actual behavior, such as
   `invalidateSectorMagic`, `invalidateSectorHeaderCRC`, and
   `injectMismatchedNvsCursor`.
9. Replace POST-based repair instructions with injection-specific restart and
   observation procedures.
10. Return structured injection results rather than ambiguous booleans.
11. Add tests for injection near the end of a sector, failed flash writes,
    duplicate random selections, parser edge cases, and concurrent command
    submission.
12. Update `docs/flash-testing.md`, which currently overstates POST/DEEP
    reporting and graceful partition-failure behavior.

## Documentation recommendation

`old-docs/failure-injection-api-reference.md` should remain historical and
should not claim to be a complete current API reference.

A replacement reference should include, for every command:

- Exact bytes or state changed
- Whether the command is destructive
- Whether an empty ring is required
- Whether active sectors are protected
- Required build flags
- Required execution task
- Parameter bounds
- Whether restart is required
- Expected POST or DEEP behavior
- Expected data loss
- How success and partial failure are reported

Until the safety and task-context problems are fixed, these APIs should only be
used on a bench device whose complete flash telemetry partition can be erased
without consequence.
