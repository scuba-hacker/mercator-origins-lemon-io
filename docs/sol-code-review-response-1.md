# Response to Sol Code Review - Review 1

Date: 13 July 2026

## Scope

This document assesses the remediation described in `sol-code-review.md` and
the associated code changes to the flash telemetry, MQTT, WebSerial,
diagnostic, and failure-injection paths.

The previously identified upgrade-migration concern is intentionally excluded.
There are currently no deployments with flash telemetry enabled, so reserving
the final physical sector for the new repair journal does not need to preserve
data written by an older flash-enabled firmware.

## Overall assessment

The changes contain several real improvements, including main-loop execution
of WebSerial commands, stricter failure-injection parsing, checked age-triggered
flushes, free-sector protection for corruption injection, corrected capacity
reporting, and waiting for the matching MQTT PUBACK before consuming a record.

However, the implementation is not yet ready to support the documented
power-loss and safe-shutdown guarantees. Three high-severity persistence issues
remain. `sol-code-review.md` consequently overstates findings F2 and F4 as
fixed, and F5 remains only partially fixed.

## High-severity findings

### R1. Safe shutdown does not persist or stop the complete telemetry pipeline

`FlashRingBuffer::prepareForShutdown()` now correctly checks its own flash
flush and NVS-save results. The system-level command is still unsafe because
`FlashTelemetryManager::prepareForShutdown()` only calls the flash buffer:

- It does not migrate records already pending in the PSRAM pipeline to flash.
- It returns `true` when the flash buffer is not initialized, including when
  flash initialization failed and the manager fell back to volatile PSRAM.
- It locks only flash writes; it does not stop telemetry capture or head-block
  commits after the command returns.
- After flash writes are locked, subsequent offline commits fail their flash
  append and fall back to PSRAM. Online commits already route directly to
  PSRAM when there is no flash backlog.

Despite these cases, the `X` command prints:

```text
SAFE TO POWER OFF - all records persisted, flash writes locked
```

The message can therefore be emitted while volatile PSRAM records exist, or it
can become false as soon as the next telemetry record is committed.

Relevant code:

- `src/FlashTelemetryManager.cpp`, `prepareForShutdown()` and
  `commitPopulatedHeadBlock()`
- `src/main_part2.cpp`, the `X` command handler
- `src/main_part3.cpp`, continuing telemetry commits

Impact: an operator can follow the documented procedure and still lose
telemetry on power-off.

Required direction: a safe-shutdown operation must first quiesce telemetry
producers, migrate all PSRAM records to flash, verify that PSRAM is empty,
flush and save the flash state, and keep the system quiesced after reporting
success. A flash-initialization failure must never be reported as a safe
persistent shutdown.

**Assessment of `sol-code-review.md`: F4 is not fully fixed.**

### R2. A journal-clear failure can make a later replay destroy new records

After a torn sector is rebuilt, both `rescueTornHeadSector()` and
`replayRepairJournal()` treat failure to erase the committed journal as a
warning and continue successfully.

The retained journal is only safe while the rebuilt target sector remains
unchanged. Once initialization or recovery continues, the target is an open
head sector and can receive new records. On a later restart, the stale journal
is replayed again. Replay erases the target and rebuilds it from the older
journal snapshot, deleting every record appended after the previous rebuild.

Relevant code:

- `src/FlashRingBuffer.cpp`, journal clear after
  `rebuildSectorFromRecords()` in `rescueTornHeadSector()`
- `src/FlashRingBuffer.cpp`, journal clear in `replayRepairJournal()`
- `src/FlashRingBuffer.cpp`, `rebuildSectorFromRecords()` erasing the target

The comment that a stale journal can simply be replayed idempotently is only
true before any new write reaches the target. It is not true after normal
operation resumes.

Impact: a journal-sector erase fault can convert an otherwise successful
repair into delayed loss of valid, newly committed records.

Required direction: failure to clear a committed journal must prevent normal
writes and prevent successful initialization. Recovery should only resume
normal operation once the journal is durably cleared, or the transaction
format must include enough generation/state information to prove that a replay
is still applicable.

**Assessment of `sol-code-review.md`: F2 is not fully fixed.**

### R3. Journal capacity handling intentionally drops a valid trailing record

A normal ring sector stores record data after a 24-byte `SectorHeader`. The
journal stores data after a 32-byte `JournalHeader`, leaving eight fewer bytes
for rescued record slots.

When a torn open sector has a valid record extent beyond byte 4088,
`rescueTornHeadSector()` rescans with the smaller journal limit and trims the
rescue to an earlier record boundary. This can discard a complete,
CRC-validated record that was committed before the torn write or torn sector
close.

The implementation explicitly logs this as a rescue trim and proceeds as a
successful repair. The permitted mixture of 16-1008 byte records can produce
valid extents in the affected final bytes of a sector.

Relevant code:

- `src/FlashRingBuffer.h`, `SectorHeader` and `JournalHeader`
- `src/FlashRingBuffer.cpp`, `rescueTornHeadSector()` capacity calculation and
  trimming logic

Impact: recovery can lose more than the torn final record, directly
contradicting the documented no-loss guarantee for previously valid records.

Required direction: the journal must accommodate the complete maximum rescued
record extent. Possible designs include reducing the ring's usable data area
to the journal capacity from the outset, storing journal metadata elsewhere,
or using more than one reserved sector for the transaction.

**Assessment of `sol-code-review.md`: F2 is not fully fixed.**

## Medium-severity findings

### R4. POST can report success after repair failure or unresolved anomalies

`performPowerOnSelfTest()` calls `repairCorruption()` but does not check its
return value. It then sets the final health result from `scanAndRecover()`.
That scan reconstructs runtime state but does not report nonblank invalid-header
sectors as a failure, so it is not a verification that the reported anomalies
were removed.

`repairCorruption()` also has two problematic behaviours:

- It returns `true` at the end even if one or more sector erases failed.
- It clears a sector from the runtime map even when its erase failed.

Additionally, a sector-header read failure leaves the local `blank` state
false, causing the function to attempt an erase. A transient read failure
should not be treated as proof that a sector contains disposable data.

Impact: POST can print `PASSED` while corruption remains, and a read failure
can trigger an unsafe erase attempt.

Required direction: distinguish unreadable sectors from confirmed invalid
sectors, propagate every erase failure, and rerun the same anomaly scan after
repair before reporting success.

**Assessment of `sol-code-review.md`: F5 is improved but only partially fixed.**

### R5. PUBACK timeout retries can grow the AsyncMqttClient queue indefinitely

The core delete-on-PUBACK correction is valid: a flash record is no longer
consumed merely because `AsyncMqttClient::publish()` returned a packet ID.

On timeout, however, `MercatorMQTT::publish()` returns `SEND_ERROR` without
canceling the unacknowledged publish or resetting the MQTT connection. The
telemetry record remains pending and the next retry queues another QoS 1
publish. AsyncMqttClient retains the queue head until its PUBACK arrives and
does not send later queued QoS publishes past it.

If TCP remains connected but PUBACKs stop, each retry adds another packet to
the outgoing queue. This can continue until memory pressure destabilizes the
device. It can also leave subsequent retries waiting for packet IDs that have
not yet been transmitted because an older unacknowledged packet blocks them.

Relevant code:

- `src/MercatorMQTT.cpp`, TLS publish and PUBACK timeout path
- AsyncMqttClient queue behaviour: QoS publish packets are released by
  `_onPubAck()`

Impact: a degraded broker connection can cause unbounded queue growth and
eventual memory exhaustion, although the flash record itself remains
retryable.

Required direction: on PUBACK timeout, force a disconnect and clear or safely
reconcile the client's pending queue before retrying, or implement one
explicit asynchronous in-flight publish state instead of enqueueing a new
packet on every retry.

**Assessment of `sol-code-review.md`: F1 fixes premature consumption, but the
new timeout/retry failure mode remains.**

## Findings accepted as fixed

The following remediation claims were confirmed by source inspection:

- The RECOVERY diagnostic does not lose its `millis` callback. The correction
  in `sol-code-review.md` is valid because `resetRuntimeState()` does not clear
  `m_fn_millis`.
- WebSerial command callbacks now enqueue work and the main-loop task executes
  the telemetry, flash, reset, diagnostic, and failure-injection handlers.
- Age-triggered flush failure now propagates from `appendRecord()` and allows
  the manager's PSRAM fallback to engage.
- Direct sector and CRC corruption injection refuses sectors currently marked
  in use.
- Incomplete-write injection checks its initial flush and prevents the direct
  test write from crossing a sector boundary.
- Numeric failure-injection arguments now use exact command matching, digit
  validation, and explicit bounds.
- Random corruption now avoids in-use and previously corrupted targets,
  checks write results, and only reports full success when all requested
  injections succeed.
- `corruptRingPointers()` now checks the NVS write result.
- PSRAM builds no longer report flash diagnostics as successful tests.
- The WebSerial UI no longer describes `F` as a toggle and no longer includes
  the unhandled `ota-off` and `reboot` dropdown commands.
- The WebSerial UI warns or asks for confirmation before the destructive `R`
  command and the connection-severing `D` command.
- The explicit parenthesized upload-status test documents and preserves the
  odd-success/even-failure enum convention.

## Documentation assessment

`sol-code-review.md` is a useful consolidated record and correctly captures
most of the original review. Its status conclusions need revision as follows:

- F1: the premature-consumption defect is fixed, with R5 recorded as a new
  reliability issue.
- F2: not fixed because of R2 and R3.
- F3: fixed.
- F4: not fixed at the complete-system level because of R1.
- F5: partially fixed because detection improved, but repair verification is
  unreliable as described in R4.
- F6: fixed for reclaim and reuse within one boot; the RAM-only bitmap's
  conservative erase after reboot is documented.
- W2/I1 and the parser/injection findings listed above are fixed.

The statements in `flash-feature.md`, `flash-testing.md`, and
`Power Loss and Recovery.md` that `X` guarantees zero loss, journal replay is
always idempotent, and only a torn final record can be lost are not accurate
until R1-R3 are resolved.

## Verification performed

The following release configurations compiled successfully:

```text
pio run -e release
env PLATFORMIO_BUILD_FLAGS='-D USE_FLASH_TELEMETRY' pio run -e release
env PLATFORMIO_BUILD_FLAGS='-D USE_FLASH_TELEMETRY -D TESTING_MODE' pio run -e release
```

Build results:

- Default PSRAM build: 78,720 bytes RAM, 1,404,417 bytes flash.
- Flash telemetry build: 79,552 bytes RAM, 1,428,989 bytes flash.
- Flash telemetry plus TESTING_MODE: 79,552 bytes RAM, 1,436,329 bytes flash.
- `git diff --check` passed.

No hardware tests were performed. In particular, compilation does not verify
power cuts at journal transaction boundaries, journal-sector erase failure,
PSRAM migration during shutdown, continued capture after `X`, broker PUBACK
loss, or MQTT queue recovery. These cases require deterministic tests or bench
fault injection before the feature can be relied upon operationally.
