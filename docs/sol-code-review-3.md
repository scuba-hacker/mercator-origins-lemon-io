# Sol Code Review - Round 3 Assessment

Date: 16 July 2026
Reviews: `sol-code-review-2.md` and the round-two remediation
Audience: Fable

## Overall assessment

Fable, this round contains real progress. The explicit `X` shutdown path is
now substantially correct, the journal-capacity defect is fully removed, and
`repairCorruption()` now reports read and erase failures honestly. All four
claimed build configurations also compile successfully.

The round is not complete, however. Independent inspection of the application
and the installed AsyncMqttClient source found two high-severity paths that
contradict the new status claims:

- The PUBACK-timeout disconnect is graceful, so it is queued behind the exact
  unacknowledged packet that is blocking the queue. It cannot reset the
  session and leaves the client stuck in `DISCONNECTING`.
- POST mutates the live ring state before recovery has succeeded. A repair or
  rescan failure can leave an initialized ring with cleared bookkeeping, and
  a journal-clear failure during runtime POST can still allow writes ahead of
  a stale committed journal.

There are also three medium/lower-severity issues in POST verification,
teardown failure handling, and the known-erased bitmap. Accordingly, R3 is
accepted as fixed; the explicit `X` portion of R1 is accepted; R2, R4, and R5
need another correction before their statuses can remain fully fixed.

As requested previously, upgrade migration is not part of this assessment.
There are no existing flash-telemetry deployments whose old final ring sector
needs to be migrated.

## High-severity findings

### R6. Failed POST recovery leaves an initialized ring with invalid runtime state

`FlashRingBuffer::performPowerOnSelfTest()` clears live bookkeeping before it
knows that repair and recovery will succeed:

```cpp
memset(m_sector_map, 0, sizeof(m_sector_map));
uint32_t saved_write_count = m_write_count;
m_record_count = 0;
m_tail_cache_valid = false;
invalidatePeek();
...
healthy = repair_ok && scanAndRecover(&rediag);
```

There are two failure branches here:

1. If `repairCorruption()` returns false, C++ short-circuit evaluation means
   `scanAndRecover()` is not called at all. The map and record count remain
   cleared.
2. If `scanAndRecover()` is called but returns false, the partially rebuilt
   runtime state is retained.

Neither branch deinitializes the ring or disables writes. This is especially
serious during normal boot: `FlashTelemetryManager::init()` first completes a
successful `FlashRingBuffer::init()`, then invokes POST with auto-repair. If
POST returns false, the manager logs that it is "continuing" and keeps
`FLASH_ONLY` selected. A valid persisted backlog can therefore become
invisible to the running manager, while later append and consume operations
use incomplete bookkeeping.

Relevant code:

- `src/FlashRingBuffer_part2.cpp`, lines 140-170
- `src/FlashTelemetryManager.cpp`, lines 78-90
- `src/FlashTelemetryManager.cpp`, lines 483-485

Impact: persisted telemetry can disappear from the runtime view, ordering and
record counts can become incorrect, and subsequent writes can operate against
state that no longer describes the partition.

Required direction: POST must not destructively replace the live runtime state
until a complete repair and recovery scan has succeeded. At minimum, once the
current map/count are cleared, recovery must run regardless of `repair_ok`.
More importantly, any failed recovery must put the flash ring into a fatal,
non-writable state and make `FlashTelemetryManager` switch to PSRAM fallback.
Building replacement state separately and committing it only on success would
be safer than trying to restore partially modified members.

**Assessment: R4 is not fully fixed.** `repairCorruption()` now returns an
honest result, but POST handles that result unsafely.

### R7. Runtime POST can still resume writes ahead of a stale committed journal

The journal-clear change is correct for boot initialization. Both
`rescueTornHeadSector()` and `replayRepairJournal()` now return false when the
committed journal cannot be erased, and `FlashRingBuffer::init()` tears down
after a failed recovery scan.

The same `scanAndRecover()` routine is also called from runtime POST, after the
ring is already initialized. If that scan repairs or replays a torn head,
rebuilds the target, and then fails to erase the journal, the failure only
propagates back as a failed diagnostic. The ring remains initialized and
`m_writes_disabled` remains false. `FlashTelemetryManager::performPowerOnSelfTest()`
does not demote flash or tear it down.

Normal capture can then append new records to the rebuilt head. On the next
boot, the still-committed journal is replayed and replaces that head with the
older journal snapshot, deleting those post-repair appends. This is the same
three-event loss sequence identified in R2, reached through a runtime entry
point instead of `init()`.

A deterministic TESTING_MODE route can exercise the structure of this path:

1. Create a torn head with `SIMULATE_POWER_LOSS`.
2. Create another anomaly that makes POST enter auto-recovery.
3. Force journal-sector erase failure during the rescue.
4. Let capture continue, then reboot.

Relevant code:

- `src/FlashRingBuffer.cpp`, lines 913-920
- `src/FlashRingBuffer.cpp`, lines 1082-1088 and 1199-1206
- `src/FlashRingBuffer_part2.cpp`, line 159
- `src/FlashTelemetryManager.cpp`, lines 483-485

Impact: a failed maintenance diagnostic can reintroduce delayed loss of newly
committed records despite the documented unconditional journal invariant.

Required direction: a failed `scanAndRecover()` on any entry path, not only
`init()`, must prevent further flash writes. The manager must observe that
fatal state and route new telemetry to PSRAM. A committed journal must never
coexist with a writable target sector.

**Assessment: R2 is fixed for boot initialization, but not for runtime
recovery.** The unconditional wording in `sol-code-review-2.md` and
`sol-code-review.md` is therefore too strong.

### R8. The PUBACK-timeout disconnect cannot get past the blocked publish

R5 added this call on timeout:

```cpp
client->disconnect();
```

In the installed AsyncMqttClient version, the default argument is
`force = false`. A graceful disconnect does not close the TCP connection. It
sets the client state to `DISCONNECTING` and appends a DISCONNECT packet to the
back of the outgoing queue.

The unacknowledged QoS 1 publish is still at the queue head with
`released() == false`. `_handleQueue()` explicitly stops at that packet until
PUBACK arrives, so the queued DISCONNECT packet is never sent. At the same
time:

- `connected()` returns false because the state is no longer `CONNECTED`.
- The next upload calls `connect()`, but `connect()` immediately returns
  because the state is not `DISCONNECTED`.
- Keepalive handling also stops because it is conditioned on `CONNECTED`.

The client remains wedged until the broker or network independently closes
the TCP connection. Clean-session behavior does not help because no
disconnect/reconnect cycle has actually occurred.

Relevant code:

- `src/MercatorMQTT.cpp`, lines 164-178
- `.pio/libdeps/release/AsyncMqttClient/src/AsyncMqttClient.cpp`, lines 405-452
- `.pio/libdeps/release/AsyncMqttClient/src/AsyncMqttClient.cpp`, lines 691-729
- `.pio/libdeps/release/AsyncMqttClient/src/AsyncMqttClient.hpp`, line 81

Impact: sustained PUBACK loss stops TLS uploads indefinitely. Flash records
remain retryable initially, but the backlog keeps growing and can eventually
reach the ring's drop-oldest behavior.

Required direction: force the transport down rather than queueing a graceful
disconnect behind the blocked packet. For this library that begins with
`disconnect(true)`, followed by verified queue/session cleanup before another
publish is accepted. The implementation should be validated against the
library's `_onDisconnect()`, `_clearQueue()`, and clean-session reconnect
behavior rather than assuming that any overload of `disconnect()` clears the
queue.

**Assessment: R5 is not fixed.** The dependency was correctly identified as
worth checking, but source inspection disproves the claimed behavior; this is
not merely a remaining bench uncertainty.

## Medium-severity findings

### R9. POST still does not verify every condition that made it fail

The new `countNonblankInvalidHeaderSectors()` helper checks only the 24-byte
`SectorHeader`. If those bytes are all `0xFF` but bytes later in the sector are
programmed, the sector is counted as blank. A failed or partial erase can
produce exactly this state. `repairCorruption()` reads the full sector and
would detect it, but POST never calls repair when its header-only first pass
reports no anomaly.

POST also does not recompute `state_consistent` after `scanAndRecover()`. The
initial scan treats multiple valid open sectors as unhealthy. The recovery
scan can select the highest-sequence sector as the head and return true
without closing, removing, or otherwise resolving the additional open
sector. The final verification only reruns the header-anomaly helper, so POST
can print `PASSED` even though the original state-consistency condition still
fails.

Relevant code:

- `src/FlashRingBuffer_part2.cpp`, lines 20-42
- `src/FlashRingBuffer_part2.cpp`, lines 69-122
- `src/FlashRingBuffer_part2.cpp`, lines 159-169

Impact: POST can still produce a false healthy result after partial erase
failure or unresolved structural anomalies.

Required direction: define one complete health-check routine and run the same
routine before and after repair. Blank-sector classification must inspect the
whole sector, or use another reliable full-sector proof. Post-repair checking
must include open-sector count, head/tail bounds, offsets, map consistency,
and every other condition included in the initial result.

**Assessment: R4's statement that `PASSED` is now a verified post-condition
is not yet accurate.**

### R10. The actual teardown path discards the safe-shutdown result

The explicit `X` command now handles `prepareForShutdown()` correctly. It
quiesces first, migrates PSRAM, verifies it is empty, persists flash, and only
then reports success. The non-sticky failure policy is reasonable for this
operator command.

`FlashTelemetryManager::teardown()` still calls the same method and ignores
its boolean result:

```cpp
if (m_initialized) {
    prepareForShutdown();
}
m_flash_buffer.teardown();
m_psram_pipeline.teardown();
```

The application uses this path from `prepareSystemForOTA()`. If flash is
unavailable, PSRAM migration fails, flash flush fails, or NVS persistence
fails, teardown nevertheless frees the PSRAM pipeline. By then the OTA path
has already disabled processing and deleted the UART tasks, so the documented
"normal capture resumes" behavior does not apply.

Relevant code:

- `src/FlashTelemetryManager.cpp`, lines 104-115
- `src/main.cpp`, lines 1106-1165

Impact: an OTA preparation or other explicit teardown can destroy volatile
records after persistence has reported failure.

Required direction: separate forced destruction from graceful persistent
shutdown. The OTA/system-shutdown caller should run and check the persistence
operation before irreversible teardown, and abort or visibly fail the
transition if telemetry cannot be made durable. A destructor may still need a
best-effort forced cleanup, but that is a different contract from a safe
application shutdown.

**Assessment: the explicit R1 `X` path is fixed, but lifecycle teardown is not
failure-safe.**

## Lower-severity finding

### R11. Failed writes do not invalidate the known-erased bitmap

`eraseSector()` marks a sector as known erased after a successful erase.
`writeBytes()` clears that bit only when `esp_partition_write()` returns
success. If a program operation fails after changing some bits, the sector is
no longer known blank but remains marked erased.

This matters most when `openNextHeadSector()` skips an erase because a
reclaimed sector is marked blank, then the 12-byte header program fails. The
next attempt can skip erase again and program over a partially written header.

Relevant code:

- `src/FlashRingBuffer.cpp`, lines 197-223
- `src/FlashRingBuffer.cpp`, lines 513-530

Impact: a transient or partial flash-program failure can turn the wear
optimization into a malformed-header or repeated-write failure.

Required direction: after argument validation, conservatively clear the
known-erased bit before attempting any program operation. A successful write
proves nonblank state, but a failed write does not prove that the sector is
still erased.

## Findings accepted in this round

The following changes were confirmed and should remain credited:

- **R1 explicit shutdown command:** `FlashTelemetryManager::prepareForShutdown()`
  quiesces before migration, verifies PSRAM emptiness, checks flash persistence,
  stays quiesced after success, and resumes after command failure. Refusing a
  safe-persistent result when flash is unavailable is correct.
- **R3 journal capacity:** removing the unnecessary padding is the cleanest
  solution. The 24-byte `JournalHeader`, deleted trim path, and
  `static_assert(sizeof(JournalHeader) == sizeof(SectorHeader))` fully remove
  the eight-byte rescue deficit.
- **R4 repair primitive:** `repairCorruption()` now refuses to erase on failed
  evidence, leaves the map unchanged after erase failure, and returns false on
  unreadable sectors or failed erases. The remaining problems are in POST's
  state transition and verification around that primitive.
- The four requested build configurations compile.
- The previous WebSerial deferral, failure-injection parsing, injection target
  protection, age-flush propagation, capacity reporting, and exact-PUBACK
  consumption changes remain valid.

## Documentation corrections required

The implementation findings require reopening several status statements:

- `sol-code-review.md` F1 must not say the timeout "forces" a disconnect or
  starts a fresh session. The current call is graceful and becomes blocked.
- `sol-code-review.md` F2 must not claim that no normal write can resume ahead
  of a stale journal until the runtime POST path is made fatal.
- `sol-code-review.md` F5 and `sol-code-review-2.md` R4 must not describe POST
  success as a fully verified post-condition.
- `flash-feature.md` and `Power Loss and Recovery.md` should distinguish boot
  initialization failure from runtime recovery failure once the code policy
  is finalized.

There are also stale source-level comments:

- `FlashRingBuffer.h` describes POST as read-only and says auto-repair only
  logs, although POST now erases garbage sectors and can journal-repair.
- `FlashRingBuffer_part2.cpp` says every routine in the file is read-only
  except explicit repair/reset calls, although POST calls repair itself.
- `FlashTelemetryManager.cpp` calls startup POST a read-only check that never
  writes or destroys data, which no longer matches `auto_repair = true`.

## Verification performed

The following configurations completed successfully:

```text
pio run -e release
env PLATFORMIO_BUILD_FLAGS='-D USE_FLASH_TELEMETRY' pio run -e release
env PLATFORMIO_BUILD_FLAGS='-D USE_FLASH_TELEMETRY -D TESTING_MODE' pio run -e release
env PLATFORMIO_BUILD_FLAGS='-D USE_FLASH_TELEMETRY -D TESTING_MODE -D USE_WEBSERIAL' pio run -e release
```

`git diff --check` also passed. No hardware testing was performed.

## Required validation after remediation

The existing bench gap remains, but the next pass should first add
deterministic tests for the newly identified control-flow failures:

1. POST repair returns false after some sectors were processed: verify the old
   ring state remains usable or flash becomes unavailable and PSRAM fallback
   engages.
2. POST `scanAndRecover()` fails: verify no flash append is possible afterward.
3. Runtime torn-head rescue rebuilds the target but journal erase fails:
   verify capture falls back to PSRAM and reboot cannot overwrite later data.
4. A sector has an erased 24-byte header and a programmed body: verify POST
   fails and repair does not claim success until the full sector is blank.
5. Multiple valid open sectors: verify post-repair POST still fails unless the
   structural anomaly is actually resolved.
6. PUBACKs are withheld while TCP stays alive: verify the transport is forced
   down, the old queue cannot block reconnection, retries remain bounded, and
   upload recovers when PUBACKs resume.
7. `prepareForShutdown()` fails during OTA preparation: verify teardown is
   aborted and PSRAM records are not freed.
8. Header programming fails after a sector was known erased: verify the next
   open attempt erases before reprogramming.

After those deterministic cases pass, the hardware plan should still cover
power cuts at each journal boundary, journal-erase failure, PSRAM migration
during `X`, capture refusal after successful `X`, sustained PUBACK loss,
normal reconnect, and the full two-dive rehearsal.

## Final disposition

This round should not be treated as ready for hardware sign-off yet. R3 is
closed, and the explicit R1 shutdown command can be closed subject to bench
validation. R2 and R4 remain partially open, R5 remains open, and R6-R11 above
should be addressed before relying on the flash feature for lossless marine
telemetry.
