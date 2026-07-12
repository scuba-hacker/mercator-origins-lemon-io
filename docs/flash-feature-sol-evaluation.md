# Flash Telemetry Solution Evaluation

Evaluation of the flash telemetry feature implemented by `FlashRingBuffer`,
`FlashTelemetryManager`, the MQTT upload path, and their application
integration.

This review compares the implementation with the guarantees described in:

- `docs/flash-feature.md`
- `docs/flash-testing.md`
- `docs/Power Loss and Recovery.md`

## Executive summary

The rewritten flash telemetry design is a significant improvement over the
previous implementation. Its append-only storage format, per-record CRC,
validated NVS cursor, PSRAM-to-flash migration, and stable peek/consume API are
all appropriate foundations for persistent telemetry.

However, the implementation does not yet provide all of the documented
"marine-grade", "delete-on-ack", and "never loss" guarantees. There are four
high-severity durability issues:

1. The default TLS MQTT path consumes records before the broker acknowledges
   them.
2. Torn-sector recovery erases the only flash copy before its rescued records
   have been made durable elsewhere.
3. An age-triggered flash failure can be reported to the caller as a successful
   commit.
4. Safe shutdown reports `SAFE TO POWER OFF` even if flushing or saving state
   failed.

These should be resolved before depending on the feature for unattended or
at-sea operation.

## Scope

The review covered:

- `src/FlashRingBuffer.h`
- `src/FlashRingBuffer.cpp`
- `src/FlashRingBuffer_part2.cpp`
- `src/FlashRingBuffer_part3.cpp`
- `src/FlashTelemetryManager.h`
- `src/FlashTelemetryManager.cpp`
- `src/MercatorMQTT.h`
- `src/MercatorMQTT.cpp`
- `src/main.cpp`
- `src/main_part2.cpp`
- `src/main_part3.cpp`
- `src/NetworkManager.h`
- `src/NetworkManager.cpp`
- `partitions/ota_spiffs_16MB.csv`
- The three flash telemetry documents listed above

The implementation was evaluated statically and by compiling both the normal
and flash-enabled release configurations. Hardware power-loss and corruption
tests were not run as part of this evaluation.

## High-severity findings

### 1. TLS MQTT records are consumed before PUBACK

The application defaults to encrypted MQTT:

- `src/main.cpp:258` sets `enableMQTTEncryption = true`.

In the TLS path, `AsyncMqttClient::publish()` returns a packet ID when the
message has been accepted locally for transmission:

- `src/MercatorMQTT.cpp:143-146` treats a non-zero packet ID as
  `MQTTConnectionResult::SUCCESS`.

The upload loop then immediately commits the pipeline tail:

- `src/main_part3.cpp:269-272` calls `tailBlockCommitted()` after that local
  success.
- `src/FlashTelemetryManager.cpp:270-276` consumes the oldest flash record.

For QoS 1, a non-zero packet ID is not the broker PUBACK. If WiFi, TLS, or MQTT
disconnects after the local enqueue but before PUBACK, the flash record has
already been erased logically and may later be physically reclaimed.

The non-TLS PicoMQTT implementation waits for PUBACK, but the default TLS path
does not. AsyncMqttClient exposes an `onPublish(packetId)` acknowledgement
callback, which should be used to correlate PUBACK with the pending flash
record before calling `tailBlockCommitted()`.

Impact: a connectivity interruption during upload can permanently lose a
record despite the documented delete-on-ack behavior.

### 2. Torn-head repair is not power-fail-safe

During recovery, valid records from a torn open head are held in the RAM tail
cache. The code then rebuilds the same sector in place:

- `src/FlashRingBuffer.cpp:1019-1051` implements
  `rescueTornHeadSector()`.
- `src/FlashRingBuffer.cpp:1025` erases the damaged sector.
- The header and rescued records are rewritten only after that erase.

A second power interruption after the erase and before the rewrite completes
loses every previously valid record in that sector. RAM is the only temporary
copy, so it cannot protect against another reset.

This contradicts `docs/Power Loss and Recovery.md`, which says there is no
window in which committed flash records can be destroyed.

Recovery needs a durable copy-on-write protocol. Options include a reserved
recovery sector, a small journal area, or rebuilding into a free sector and
committing its header before reclaiming the damaged sector.

Impact: repeated or unstable power during boot recovery can destroy an entire
sector of otherwise valid telemetry.

### 3. Age-triggered flush failures are reported as successful commits

`appendRecord()` invokes the time-based flush after adding the new record:

- `src/FlashRingBuffer.cpp:396-399` calls `flush()` but ignores its result.
- `src/FlashRingBuffer.cpp:401` then returns `true` unconditionally.

`FlashTelemetryManager::commitBlockToFlash()` interprets that return value as a
successful flash commit. It therefore does not invoke the PSRAM fallback.

If the flash write fails, records already in the assembly buffer remain only
in RAM while the manager reports success. Further age-triggered flush attempts
can fail in the same way until the buffer-space path eventually propagates an
error.

Impact: flash failures defeat both the documented PSRAM fallback and the
approximately 15-second maximum RAM-loss bound.

### 4. Safe shutdown reports success without checking persistence

`FlashRingBuffer::prepareForShutdown()` performs the expected operations but
does not inspect their results:

- `src/FlashRingBuffer.cpp:1155` ignores the result of `flush()`.
- `src/FlashRingBuffer.cpp:1156` ignores the result of
  `savePersistedState()`.
- `src/FlashRingBuffer.cpp:1157-1158` disables writes and prints
  `SAFE TO POWER OFF` unconditionally.

If either persistence operation fails, pending records can remain only in RAM,
but the operator receives a definitive safety message and writes are locked so
the operation cannot be retried normally.

The method also has a `void` return type, preventing the manager or application
from reporting failure. No normal application shutdown event or serial command
currently invokes it; outside destruction/teardown, the documented safe
shutdown workflow is not wired into the application.

Impact: following the displayed shutdown instruction can still lose pending
records.

## Medium-severity findings

### 5. The recovery diagnostic disables timed flushing

`performPowerLossRecoveryTest()` simulates a reboot by tearing down and
reinitializing the ring:

- `src/FlashRingBuffer_part3.cpp:200` calls `teardown()`.
- `teardown()` resets `m_fn_millis` to null through `resetRuntimeState()`.
- `src/FlashRingBuffer_part3.cpp:201` then calls `init(m_fn_millis)`, passing
  the now-null callback.

After the `RECOVERY` command completes, age-based flushing is disabled for the
rest of that boot. Records will flush only when the sector assembly space is
exhausted or an explicit flush occurs. At a low telemetry rate, the duration of
RAM-only exposure can become much greater than 15 seconds.

The callback should be saved in a local variable before teardown and supplied
to `init()` afterward.

### 6. Header corruption can disappear from diagnostics after restart

Boot recovery ignores sectors whose headers fail validation:

- `src/FlashRingBuffer.cpp:897-901` skips invalid sector headers.

POST only counts an invalid header as an anomaly when that sector is already
present in the runtime map:

- `src/FlashRingBuffer_part2.cpp:51-59` checks `mapGet(sector)` before
  incrementing the anomaly count.

After a restart, recovery has excluded the damaged sector from the map, so POST
treats it as erased. Deep validation similarly skips sectors without a valid
header. Consequently, the documented expectation that `RANDOM_CORRUPT` sectors
are reported by POST or DEEP is not reliable after reboot.

Impact: telemetry can be excluded from the ring while diagnostics report a
healthy system.

### 7. A reclaimed sector can be erased twice per reuse cycle

The read path erases a fully consumed tail sector immediately:

- `src/FlashRingBuffer.cpp:828-840` erases it in
  `advanceTailSector()`.

When that already-erased sector later becomes the head, it is erased again:

- `src/FlashRingBuffer.cpp:458-488` unconditionally erases the next head
  sector.

This is safe for data integrity, but it can double erase wear during normal
drain-and-reuse operation. It conflicts with the one-erase-per-sector-cycle
description and wear estimate in `docs/flash-feature.md`.

The design should choose one erase point or track whether a reclaimed sector
is already known to be erased.

## Documentation mismatches

### Boot-time checks are not always read-only

Both `docs/flash-feature.md` and `docs/flash-testing.md` describe boot recovery
and POST as read-only except for torn-write recovery. For a non-empty healthy
ring, however, `scanAndRecover()` calls `savePersistedState()` at
`src/FlashRingBuffer.cpp:1015`, which writes NVS.

A completely blank flash buffer does return before this call, so the specific
claim that the raw ring partition remains untouched on a fresh boot is true.
The broader claim that all boot-time checks are read-only is not.

### Always-online operation can still write flash during startup

`docs/flash-feature.md` says an online-from-power-on session never writes
flash. In the main processing path, telemetry is committed before
`getNextTelemetryMessagesUploadedToPrivateMQTT()` updates the manager's uplink
state:

- `src/main.cpp:1806` commits telemetry.
- `src/main.cpp:1818` invokes the upload function that calls
  `setUplinkAvailable()`.

The manager also starts with uplink unavailable. Records generated before MQTT
connectivity is observed therefore route to flash. `docs/flash-testing.md`
correctly acknowledges that the pre-connection seconds touch flash, so the two
documents should use the same qualified wording.

### Maximum pipeline capacity reporting disagrees with the documented capacity

`docs/flash-feature.md` estimates approximately 41,000 normal telemetry
records. `FlashTelemetryManager::getMaximumPipelineLength()` instead reports
four maximum-size records per sector, or 10,240 records:

- `src/FlashTelemetryManager.cpp:336-340`

The method is diagnostic rather than a storage limit, but its output does not
represent the configured 256-byte block size or the documented normal record
size. Status displays can therefore significantly understate usable capacity.

### Safe shutdown is described as an available operational workflow

The documentation instructs an operator to call `prepareForShutdown()` or wait
for its safety log. There is currently no serial command, application shutdown
handler, or Lantern-triggered path that calls it during normal operation. The
only automatic path is object teardown.

The documents should either identify the workflow as pending or the
application should expose and invoke it deliberately.

## Positive design assessment

The following parts of the implementation are well structured:

- Sector sequence numbers are written once, giving recovery a stable ordering
  key under normal operation.
- Open and closed sectors have distinct, CRC-protected states.
- Record payload and `roundedUpPayloadSize` metadata are protected together by
  CRC16.
- Record writes append only into erased bytes during normal operation.
- `peekOldestRecord()` is stable until `consumeOldestRecord()` is called.
- Failed synchronous publishes leave the currently peeked record available for
  retry.
- NVS tail state is accepted only when its sector and sequence match the flash
  scan.
- Cached record counters avoid repeatedly scanning the 10 MB partition.
- PSRAM blocks are migrated before newer flash commits when routing changes to
  offline storage.
- Flash initialization failure falls back to the existing PSRAM pipeline.
- Destructive stress and recovery diagnostics refuse to run over a non-empty
  ring.
- The dedicated `flashbuf` partition is correctly placed outside both OTA
  application partitions.

These are strong foundations. The main remaining work is making failure paths
match the guarantees of the normal data path.

## Verification performed

The following release builds completed successfully:

```text
pio run -e release
env PLATFORMIO_BUILD_FLAGS='-D USE_FLASH_TELEMETRY' pio run -e release
```

The flash-enabled build reported:

```text
RAM:   79,160 / 327,680 bytes (24.2%)
Flash: 1,423,085 / 2,097,152 bytes (67.9%)
```

There are no host-side automated tests for the ring state machine, manager
routing, MQTT acknowledgement lifecycle, or power-loss boundaries. The
on-device diagnostics are useful, but some of them exercise only a single
interruption and cannot validate interruption during recovery itself.

## Recommended remediation order

1. Make MQTT tail consumption asynchronous and dependent on the QoS 1 PUBACK
   for the exact packet ID.
2. Replace in-place torn-head repair with a durable copy-on-write recovery
   transaction.
3. Propagate every flash and NVS failure through `appendRecord()`, `flush()`,
   shutdown, and the manager fallback path.
4. Make shutdown return a status, retry where appropriate, and print
   `SAFE TO POWER OFF` only after verified success.
5. Preserve the clock callback across the recovery diagnostic reinitialization.
6. Make POST and DEEP distinguish erased sectors from nonblank sectors with
   invalid headers, including after restart.
7. Remove the duplicate sector erase or update the wear model and documentation
   to reflect actual behavior.
8. Align startup-routing, read-only recovery, shutdown availability, and
   capacity wording across all three documents.
9. Add deterministic state-machine tests for every power-cut boundary,
   including power loss during recovery and disconnect between publish enqueue
   and PUBACK.

## Release recommendation

The feature is suitable for continued bench development and controlled field
testing where duplicate or missing telemetry can be independently detected.
It should not yet be presented as providing strict broker-acknowledged delivery
or loss-free recovery under repeated power interruption.

After the four high-severity findings are fixed, the bench validation plan in
`docs/flash-testing.md` should be rerun on hardware, with additional tests for
PUBACK interruption, failed shutdown flushes, and power loss during torn-sector
repair.
