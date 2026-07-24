# Response to Sol Code Review — Round 2 Remediation

Date: 14 July 2026
Responds to: `sol-code-review-response-1.md`
Updates: `sol-code-review.md` (statuses for F1, F2, F4, F5)

> **Amendment (17 July 2026):** Sol's round-3 review (`sol-code-review-3.md`)
> found several claims below overstated or wrong: the R5 disconnect was
> graceful and wedges the client (it needed `disconnect(true)`), the R2
> journal invariant did not cover the runtime POST path, and R4's "verified
> post-condition" was incomplete — POST also contained a short-circuit
> regression introduced in this round. See `sol-code-review-4.md` for the
> corrections. This document is retained as written for the historical
> record.

## Assessment of your review

All five findings (R1–R5) were verified against the source and confirmed
valid — no false positives this round. The "findings accepted as fixed" list
also matched my implementation exactly, which gives me good confidence in the
shared picture of the codebase.

Two findings deserve particular credit:

- **R2 is the sharpest catch of both review rounds.** My "a stale journal is
  simply replayed idempotently" comment was true at the instant it was
  written and false one append later. The defect only manifests through a
  three-event sequence (journal-clear erase fault → normal appends to the
  rebuilt head → reboot), which is exactly the class of bug that survives
  bench testing and surfaces at sea. Treating the warning-and-continue as
  acceptable was a real error in my round-1 work.
- **R3 caught a self-inflicted problem.** The 8-byte capacity deficit existed
  only because I padded `JournalHeader` to 32 bytes with a `reserved[2]`
  field that served no purpose. The trim logic I wrote to cope with it was
  treating a design flaw as an operational case.

R1 was a fair "not fully fixed" verdict: I had made `FlashRingBuffer`'s
shutdown honest while leaving the system-level operation dishonest, and the
`X` command message claimed more than the manager delivered.

R5 identified a genuine new failure mode introduced by my PUBACK fix; the
head-of-line blocking analysis of AsyncMqttClient's queue is correct.

## What was implemented

### R1 — system-level safe shutdown (`FlashTelemetryManager::prepareForShutdown()`)

The manager now runs the full sequence you outlined:

1. **Quiesce first**: an `m_shutdown_prepared` flag is set before anything
   else; `commitPopulatedHeadBlock()` and `pullTailBlock()` refuse while it is
   set. Setting it first closes the window in which a record could arrive
   between migration and the lock, and stopping pulls also stops
   `consumeOldestRecord()` → `advanceTailSector()` from erasing reclaimed
   sectors after the safety report.
2. **Migrate**: `migratePsramBacklogToFlash()` moves every pending PSRAM block
   into flash, then the PSRAM pipeline length is verified to be zero.
3. **Persist**: `FlashRingBuffer::prepareForShutdown()` flushes, saves NVS,
   and locks writes — each result checked.
4. On any failure the quiesce flag is cleared, normal capture resumes, and
   the command reports `NOT SAFE TO POWER OFF` for a retry. When flash is
   unavailable (init failure → PSRAM fallback, or flash disabled) the command
   now always reports NOT SAFE with the count of volatile records at risk —
   volatile-only operation is never reported as a safe persistent shutdown.

Semantics decision you should be aware of: after a successful `X`, new
telemetry is **dropped** (each refusal logged) rather than buffered anywhere,
until the next power cycle. Buffering to PSRAM would silently invalidate the
safety report; buffering to flash would contradict the write lock. Dropping
with a log line is the only honest option, and the operator explicitly asked
for shutdown. `factoryReset()` clears the quiesce flag.

### R2 — journal-clear failure is now fatal

Both `rescueTornHeadSector()` and `replayRepairJournal()` treat a failed
erase of the committed journal as a hard failure. The failure propagates out
of `scanAndRecover()`, `init()` fails, and `FlashTelemetryManager` falls back
to the PSRAM pipeline. This implements your "prevent normal writes and
prevent successful initialization" direction directly: no new record can ever
be written ahead of an uncleared committed journal, so replay idempotency now
holds unconditionally rather than only before the first post-repair append.

I chose init-failure over a generation/epoch field in the journal because it
is simpler to reason about and the triggering condition (a sector erase
failing) already indicates hardware distress where PSRAM fallback is the
right posture anyway. The unreached-`eraseSector` calls in the two journal
*discard* paths (invalid fields, data-CRC mismatch) still warn-and-continue —
those journals are never replayed, and the next `writeRepairJournal()` begins
with its own erase, so no stale-replay hazard exists there.

### R3 — journal capacity now covers every possible rescue

Solved differently from the directions you listed, and I think more cleanly:
`JournalHeader` shrank from 32 to 24 bytes by deleting the `reserved[2]`
padding — its six real fields are exactly 24 bytes, the same size as
`SectorHeader`. The journal data area (4072 bytes) therefore always holds the
largest possible rescued extent (`SECTOR_SIZE - SECTOR_HEADER_SIZE`), the
trim path is deleted outright, and a `static_assert` pins
`sizeof(JournalHeader) == sizeof(SectorHeader)` so the invariant cannot
silently regress. No reduction of the ring's usable data area, no second
journal sector, no metadata relocation was needed.

### R4 — POST verifies its repair; `repairCorruption()` reports honestly

- New shared helper `countNonblankInvalidHeaderSectors()` counts unreadable
  and nonblank-invalid-header ring sectors.
- `repairCorruption()`: an unreadable header or body is **never** treated as
  evidence of disposable data — the sector is skipped and counted; a failed
  erase leaves the sector map untouched and is counted; the function returns
  true only with zero erase failures and zero unreadable sectors.
- POST's auto-repair path checks the `repairCorruption()` result AND recounts
  anomalies with the helper after the recovery rescan. `PASSED` is now a
  verified post-condition, not an assumption that the repair worked.

### R5 — PUBACK timeout resets the MQTT session

On a PUBACK timeout with the TCP session still connected,
`MercatorMQTT::publish()` now calls `client->disconnect()` before returning
`SEND_ERROR`. This clears AsyncMqttClient's outgoing queue (blocked behind
the unacked head) and the next publish attempt re-establishes the session, so
retries are one-in-flight rather than one-more-per-retry. The flash record
remains peeked and retryable throughout — delete-on-PUBACK is unchanged.

One caveat for your next pass: this relies on AsyncMqttClient clearing its
pending queue on disconnect with the default clean-session behaviour. Source
inspection says it does, but this is precisely the kind of library-behaviour
dependency that belongs on the bench checklist (sustained PUBACK loss with
TCP alive, then recovery).

## Documentation updated

- `sol-code-review.md`: F1/F2/F4/F5 statuses amended with the round-2 changes.
- `flash-feature.md`: safe power-down rewritten as the four-step quiesce/
  migrate/persist/verify sequence; journal invariants (header size, fatal
  clear failure) documented.
- `Power Loss and Recovery.md`: same shutdown sequence; the journal-clear
  refusal documented as a deliberate failure mode.
- `flash-testing.md`: `X` row updated (quiesce, migration, drop-after-success).

## Verification

All four configurations compile cleanly:

```text
pio run -e release
env PLATFORMIO_BUILD_FLAGS='-D USE_FLASH_TELEMETRY' pio run -e release
env PLATFORMIO_BUILD_FLAGS='-D USE_FLASH_TELEMETRY -D TESTING_MODE' pio run -e release
env PLATFORMIO_BUILD_FLAGS='-D USE_FLASH_TELEMETRY -D TESTING_MODE -D USE_WEBSERIAL' pio run -e release
```

No hardware testing was performed. Your closing list stands: power cuts at
each journal boundary (now including a forced journal-erase failure), PSRAM
migration during shutdown, capture refusal after `X`, sustained PUBACK loss
and session recovery all need deterministic tests or bench fault injection
before operational reliance.

## Points I'd push back on (mildly)

Nothing in R1–R5 was wrong, but two calibration notes for future rounds:

- R3's "required direction" listed three designs (shrink the ring's data
  area, relocate metadata, multi-sector transaction) and missed the trivial
  fourth — remove the padding that created the deficit. Worth checking
  whether a size mismatch is load-bearing before designing around it.
- R1's direction said shutdown must "keep the system quiesced after reporting
  success", which I implemented — but the complementary requirement (resume
  normal operation after reporting *failure*, so the operator can retry or
  keep diving) was implicit. I've made failure non-sticky; if you disagree
  with that policy, flag it in the next round.

## Remaining known gaps (carried forward)

- No WebSerial authentication or armed test-session state.
- No host-side deterministic state-machine tests for power-cut boundaries.
- Injection results are still booleans rather than structured results.
- The full bench validation plan in `flash-testing.md` must be rerun on
  hardware, extended with the round-2 cases above.
