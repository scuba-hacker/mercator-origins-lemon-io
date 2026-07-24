# Sol Code Review — Consolidated Findings (July 2026)

This document merges and supersedes three review documents produced by Sol
(ChatGPT) on 12 July 2026:

- `flash-feature-sol-evaluation.md` — flash telemetry feature evaluation
- `webserial-command-issues.md` — WebSerial command system assessment
- `failure-injection-api-issues.md` — failure-injection API assessment

Each finding was independently verified against the source before being
accepted here. One finding was disproven and is recorded in the corrections
section; one additional issue found during verification has been added.
Findings carry a **Status** line maintained as remediation lands.

Sol re-reviewed the first remediation round in
`sol-code-review-response-1.md`, finding five follow-up issues (R1–R5); the
round-2 fixes are recorded in `sol-code-review-2.md`. Sol's round-3 review
(`sol-code-review-3.md`) then found six further issues (R6–R11), including
two regressions introduced in round 2; the round-3 fixes are recorded in
`sol-code-review-4.md`. Sol's round-4 review (`sol-code-review-5.md`)
rejected branch-level patching, found seven further issues (R12–R18), and
mandated explicit state models, one central failure transition, transactional
recovery, deterministic fault seams, and a test matrix; that restructuring is
recorded in `sol-code-review-6.md`.

**Verification status:** per the round-4 acceptance gate, findings whose
deterministic tests have not yet RUN are marked *IMPLEMENTED, PENDING
TEST_MATRIX* rather than fixed. The `TEST_MATRIX` device command exists and
asserts post-state per case, but no hardware run has happened yet — statuses
will be promoted only on a passing bench run.

## Corrections to Sol's review

### Disproven: "The RECOVERY diagnostic disables timed flushing"

Sol claimed (flash evaluation, medium finding 5) that
`performPowerLossRecoveryTest()` leaves age-based flushing disabled because
`teardown()` resets `m_fn_millis` to null through `resetRuntimeState()` before
`init(m_fn_millis)` is called.

This is incorrect. Neither `teardown()` nor `resetRuntimeState()` touches
`m_fn_millis`; the only assignment is inside `init()`. The member therefore
still holds the original callback when it is passed back to `init()`, and
age-based flushing survives the RECOVERY diagnostic. No fix is required.

### Added: fragile operator precedence in the upload commit condition

`src/main_part3.cpp` tested the upload result with `uploadStatus & 0x01 ==
Q_SUCCESS`. Because `==` binds tighter than `&`, this evaluates as
`uploadStatus & (0x01 == 1)` = `uploadStatus & 1`. It happens to be correct —
the `e_q_upload_status` success codes are all odd and the failure codes even —
but only by coincidence of the enum values.

**Status: FIXED** — parenthesised explicitly with a comment documenting the
odd-success convention.

---

# Part 1 — Flash telemetry feature

Scope: `FlashRingBuffer` (all parts), `FlashTelemetryManager`, `MercatorMQTT`,
`NetworkManager`, `main.cpp`/`_part2`/`_part3`, the partition table, and the
three feature documents (`flash-feature.md`, `flash-testing.md`,
`Power Loss and Recovery.md`).

Sol's overall verdict: the rewritten design (append-only format, per-record
CRC, validated NVS cursor, PSRAM migration, stable peek/consume) is a sound
foundation, but the failure paths did not yet honour the documented
"delete-on-ack" and "never loss" guarantees.

## High-severity findings

### F1. TLS MQTT records were consumed before PUBACK — VERIFIED

The default build uses encrypted MQTT (`enableMQTTEncryption = true`). In the
TLS path, `MercatorMQTT::publish()` treated a non-zero `AsyncMqttClient`
packet ID as `SUCCESS`, but a non-zero packet ID only means the message was
queued locally — it is not the broker's QoS 1 PUBACK. The upload loop then
called `tailBlockCommitted()`, which consumed the flash record. A WiFi/TLS/MQTT
drop between local enqueue and PUBACK permanently lost the record despite the
documented delete-on-ack behaviour. The non-TLS PicoMQTT path already waited
for PUBACK.

**Status: IMPLEMENTED, PENDING TEST_MATRIX/BENCH** — `MercatorMQTT` now registers `onPublish()` callbacks on
both async clients and `publish()` waits (bounded) for the PUBACK matching the
exact packet ID before reporting `SUCCESS` for QoS ≥ 1. No ack within the
timeout → `SEND_ERROR`, the record stays peeked and is retried (at-least-once;
a duplicate at the broker is possible and harmless). Round 2 (R5) added a
disconnect on PUBACK timeout, but round 3 (R8) found it was a graceful
disconnect that queues behind the blocked publish and wedges the client in
`DISCONNECTING`. It is now a **forced** `disconnect(true)` followed by an
explicit `clearQueue()`; correctness no longer depends on disconnect itself
clearing AsyncMqttClient's queue. Round 4 (R17): acknowledgment state is
atomic and client-scoped. Packet IDs from timed-out/disconnected attempts are
quarantined for that client, so a delayed same-ID callback cannot own a later
attempt. *Ack-ownership cases are IMPLEMENTED IN TEST_MATRIX, PENDING device
run; sustained PUBACK loss remains a mandatory broker/bench test.*

### F2. Torn-head repair was not power-fail-safe — VERIFIED

`rescueTornHeadSector()` erased the damaged sector while the rescued records
existed only in the RAM tail cache, then rewrote the header and records. A
second power interruption between the erase and the rewrite destroyed every
previously valid record in the sector — contradicting
`Power Loss and Recovery.md`, which promised no window in which committed
records can be destroyed.

**Status: IMPLEMENTED, PENDING TEST_MATRIX** — repair is now a durable copy-on-write transaction through
a reserved journal sector (the last physical sector, outside the ring): the
rescued records are first written to the journal and committed with a
CRC-protected header (header written last), only then is the damaged sector
erased and rebuilt, and finally the journal is erased. Boot recovery replays a
committed journal before scanning, so power loss at any point during repair is
recoverable and the replay is idempotent. Round 2 hardening: the journal
header is exactly 24 bytes so its data area always fits the full rescued
extent (R3 — no trim can ever drop a valid record), and a failure to clear
the committed journal is fatal to the rescue/replay and to initialization
(R2). Round 3 (R7) extended that to runtime recovery. Round 4 (R14) closed
the final holes: the journal *discard* branches (out-of-range fields, data
CRC mismatch) now also verify their erase, so no exit path can leave a
committed journal coexisting with a writable target — disposition is either
replayed-and-cleared or discarded-and-cleared, or the ring refuses to run
(init failure at boot, fatal state at runtime). *IMPLEMENTED; journal cases
5–6 of TEST_MATRIX PENDING device run.*

### F3. Age-triggered flush failures were reported as successful commits — VERIFIED

`appendRecord()` invoked the time-based `flush()` after adding the new record
and ignored its result, returning `true` unconditionally.
`FlashTelemetryManager::commitBlockToFlash()` interpreted that as a successful
flash commit and never invoked the PSRAM fallback, defeating both the fallback
and the documented ~15-second RAM-loss bound when flash writes fail.

**Status: IMPLEMENTED, PENDING TEST_MATRIX** — the age-based flush now runs
(and is checked) before the new record is appended. Fallback transfer is
commit-before-consume, preflights PSRAM capacity to prevent the underlying
pipeline from evicting its oldest record, and preserves a populated current
block behind older assembly records. A destination refusal retains the source
and returns a visible capture failure. Manager cases M1-M3 assert the failure
and ordering paths; hardware execution is pending.

### F4. Safe shutdown reported success without checking persistence — VERIFIED

`prepareForShutdown()` ignored the results of `flush()` and
`savePersistedState()`, returned `void`, disabled writes, and printed
`SAFE TO POWER OFF` unconditionally — so an operator could cut power while
pending records existed only in RAM, with retries locked out. Additionally, no
serial command or application shutdown path ever invoked it; the documented
workflow did not exist outside object teardown.

**Status: IMPLEMENTED, PENDING TEST_MATRIX/BENCH** — `prepareForShutdown()` now returns a status, only locks
writes and prints `SAFE TO POWER OFF` after both the flush and the NVS save
verified successful, and leaves writes enabled for retry on failure. A new `X`
serial/WebSerial command invokes it from the main loop; `emergencyFlush()` also
reports failures now. Round 2 (R1): the manager-level operation is a full
system shutdown — it quiesces telemetry first (new commits and tail pulls are
refused), migrates pending PSRAM blocks to flash, verifies PSRAM is empty,
then flushes and locks the ring; it fails (and resumes normal operation for a
retry) when any step fails, and refuses outright when flash is unavailable —
volatile-only operation is never reported as a safe shutdown. Round 3 (R10):
the OTA preparation path now runs this persistence sequence FIRST — before
halting processing, deleting tasks, or tearing telemetry down — and loudly
reports when records could not be persisted, instead of silently destroying
them after a failed shutdown. Round 4 (R16): the OTA callback (async web
task) no longer performs any preparation itself — it requests preparation via
an atomic handshake and keeps the upload blocked until completion. The main
loop performs telemetry persistence, display/network shutdown, and task/queue
teardown. There is no timeout path that proceeds before the owning task has
finished. The explicit `X` path is accepted by Sol; *manager migration failure
is IMPLEMENTED IN TEST_MATRIX and OTA concurrency remains PENDING bench run.*

## Medium-severity findings

### F5. Header corruption disappearing from diagnostics after restart — VERIFIED

Boot recovery skips sectors whose headers fail validation, and POST only
counted an invalid header as an anomaly when the sector was still in the
runtime map. After a restart the damaged sector is out of the map, so POST
treated it as erased and DEEP skipped it — a `RANDOM_CORRUPT`-style corruption
became invisible while records were silently excluded from the ring.

**Status: IMPLEMENTED, PENDING TEST_MATRIX** — POST and DEEP now distinguish blank (all-0xFF header)
sectors from nonblank sectors with invalid headers, independent of the runtime
map, and report the latter as anomalies. POST's auto-repair erases such
sectors (their records are unreadable regardless, since recovery cannot order
a sector without a valid header), restoring a clean report afterwards.
Round 2 (R4): `repairCorruption()` now refuses to erase unreadable sectors,
leaves the map untouched on erase failure, and returns false on any failure.
Round 3 (R6/R9) added repair-result checking and post-repair verification.
Round 4 (R12/R13/R15/R18) restructured rather than patched: recovery is now
TRANSACTIONAL (a complete candidate state is built and published atomically —
the live state is never pre-cleared, an empty result is one canonical state,
and a mid-scan failure with unmodified flash retains the previous trusted
state wholesale); POST's critical exits (partition missing/unreadable) enter
the central fatal transition instead of leaving flash active; the open-sector
ownership invariant (any open sector must be the highest-seq head, RAM and
flash agreeing) is enforced in both recovery and verification, refusing to
guess rather than erase; and a deep verification that cannot complete (buffer
unavailable) is reported as failed, never silently downgraded.
The destructive invalid-header cleanup now reports whether it changed flash;
that fact is carried into transactional recovery, so a recovery or final
verification failure after an erase enters FATAL instead of retaining stale
pre-repair bookkeeping. *IMPLEMENTED; matrix cases 1–4, 7, 8 PENDING device
run.*

### F6. A reclaimed sector could be erased twice per reuse cycle — VERIFIED

`advanceTailSector()` erases a fully consumed tail sector immediately; when the
same sector later became the head, `openNextHeadSector()` unconditionally
erased it again. Safe for data, but double wear, conflicting with the
one-erase-per-cycle description in `flash-feature.md`.

**Status: FIXED** — a RAM-only erased-sector bitmap tracks sectors erased this
boot (set on successful erase, cleared on any program operation into the
sector); `openNextHeadSector()` skips the erase when the target is known
erased. The map starts empty at boot, so the first cycle after power-on still
erases conservatively.

## Documentation mismatches (flash feature)

All verified, and the feature documents have been updated accordingly:

- **Boot-time checks are not strictly read-only**: `scanAndRecover()` writes
  the NVS cursor for a non-empty ring (and POST auto-repair can now also erase
  garbage sectors). Only a completely blank partition boots with zero writes.
  Wording corrected in `flash-feature.md` and `flash-testing.md`.
- **Always-online operation still writes flash during startup**: records
  committed before MQTT connectivity is first observed route to flash because
  the uplink state starts unavailable and is updated after the commit. The two
  documents now use the same qualified wording.
- **Capacity reporting**: `getMaximumPipelineLength()` reported four maximum-
  size records per sector (10,240) rather than a capacity based on the
  configured block payload. **FIXED** — the estimate now derives from
  `BlockHeader::s_getMaxPayloadSize()` (≈38,000 records at the 256-byte block
  size, matching the ~41k order of magnitude documented).
- **Safe shutdown described as available**: previously no command invoked it.
  **FIXED** — the `X` command exists and the docs describe it.

## Positive assessment (unchanged from Sol's review)

Sector seq numbers stamped once; CRC-protected open/closed sector states;
meta+payload covered by CRC16; append-only programming into erased bytes;
stable peek until consume; failed publishes leave the record retryable; NVS
cursor accepted only when validated against the flash scan; O(1) cached
counters; PSRAM-to-flash migration preserving order; PSRAM fallback on flash
init failure; destructive diagnostics refusing to run over a non-empty ring;
`flashbuf` partition correctly outside both OTA slots.

---

# Part 2 — WebSerial command system

Scope: `NetworkManager` WebSerial routing, `logs_page.html`, command handlers
in `main.cpp`/`main_part2.cpp`, `SerialConfig.h`, and
`old-docs/webserial-command-system.md` (which remains historical material).

## High-severity findings

### W1. WebSerial is disabled in the default build — VERIFIED

`USE_WEBSERIAL` is commented out in `SerialConfig.h`, so `WebSerial.begin()`
is never called and the `/webserialws` endpoint is not registered; the `/logs`
page loads but cannot connect. `writeLogToSerial` also defaults to `false`, so
even with WebSerial enabled, command-handler output is suppressed.

**Status: DOCUMENTED (intentional configuration)** — the build prerequisites
(`#define USE_WEBSERIAL`, `writeLogToSerial = true`) are now stated in
`flash-testing.md`. This is a configuration choice, not a code defect.

### W2. Flash diagnostics ran from an unsafe asynchronous task context — VERIFIED

`NetworkManager::webSerialReceiveMessage()` runs on the async web server task
and invoked the registered command callbacks directly, which executed flash
diagnostics, factory reset and failure injection immediately — while both
`FlashRingBuffer` and `FlashTelemetryManager` are explicitly single-task-only.
Live telemetry capture/flush/consume on the main loop could race POST, STRESS,
RECOVERY, `R`, or corruption injection. The empty-ring checks do not close
this race; they can pass immediately before the main loop commits a record.

**Status: FIXED** — WebSerial commands are now queued (mutex-protected, same
pattern as the `%` Mako relay) and executed by the main-loop task. The async
callback only enqueues.

### W3. The WebSerial `D` → `C` workflow could not complete — VERIFIED

`D` disconnects WiFi with auto-reconnect disabled **and persists the blocked
state to NVS**, killing the connection that carries WebSerial; the browser can
never send the documented follow-up `C`, and a reboot does not restore
connectivity. Recovery requires `C` over USB serial.

**Status: MITIGATED** — the `D` handler now prints an explicit warning that
recovery requires `C` via USB when the management channel is WiFi, the browser
dropdown labels the command with the same warning, and the page asks for
confirmation before sending it. The command remains available because it is
the offline-capture test entry point when driven over USB.

## Medium-severity findings

### W4. Output routing documented incorrectly — VERIFIED

The old document referenced a `WEB_SERIAL` macro; the real macro is
`USE_WEBSERIAL`, and all `USB_SERIAL_*` output is additionally gated on
`writeLogToSerial` (default `false`). `NetworkManager` writes its
command-received wrapper lines directly to WebSerial, so the browser can show
a command was received while hiding its actual output.

**Status: DOCUMENTED** in `flash-testing.md`.

### W5. `F` is not a toggle — VERIFIED

`F` only prints the compile-time `USE_FLASH_TELEMETRY` setting, but the
dropdown label and the `H` help text called it a toggle.

**Status: FIXED** — both now say "Show Flash Persistence Setting".

### W6. Dropdown `ota-off` and `reboot` entries had no handlers — VERIFIED

Both strings fell through to the unknown-command response (the separate Reboot
shortcut button uses the `/reboot` HTTP endpoint and works).

**Status: FIXED** — the two dead dropdown entries are removed.

### W7. Diagnostic safety was overstated — VERIFIED

`DEEP` is read-only; `POST` with auto-repair can write (torn-head rescue, NVS,
and now garbage-sector erase); `STRESS`/`RECOVERY` write but require an empty
ring. None of this made concurrent execution safe — that was the W2 race, now
resolved by main-loop queueing. Remaining truth: destructive commands still
require an empty ring and should be bench-only.

**Status: FIXED (W2) + DOCUMENTED** — safety wording corrected in
`flash-testing.md`.

### W8. PSRAM diagnostic stubs reported misleading success — VERIFIED

In a PSRAM build the `TelemetryPipeline` compatibility stubs return `true`, so
`POST` printed `PASSED` without any flash being tested.

**Status: FIXED** — in PSRAM builds the diagnostic commands now report
`NOT AVAILABLE IN PSRAM BUILD` instead of invoking the stubs.

### W9. `R` behaviour depends on the build and has no confirmation — VERIFIED

`R` performs a real factory reset only in flash builds, and WebSerial has no
authentication.

**Status: PARTLY FIXED** — the browser now asks for confirmation before
sending `R` (and `D`). WebSerial remains unauthenticated and should be treated
as a bench interface; this is documented.

## Stale documentation (old-docs)

The old document's callback-registration examples, diagnostic durations,
expected transcripts, and "runtime-switchable pipeline" wording are stale;
`%`-relay commands, `TESTING_MODE` injection commands, and the legacy
`serial-off`/`ON`/`OFF` strings were missing. `old-docs/` remains historical;
`flash-testing.md` is the current operational reference.

---

# Part 3 — Failure-injection API

Scope: the `TESTING_MODE` injection methods in `FlashRingBuffer_part3.cpp`,
their manager wrappers, the WebSerial parser in `main_part2.cpp`, and
`old-docs/failure-injection-api-reference.md` (historical).

## High-severity findings

### I1. Injection ran from an unsafe task context — VERIFIED

Same root cause as W2. **Status: FIXED** by the main-loop command queue.

### I2. Sector corruption could destroy live telemetry — VERIFIED

`injectSectorCorruption()` / `injectCRCCorruption()` validated only
initialization and range: they would happily destroy the head, the tail, or
any in-use sector's header, permanently excluding its records — and the old
reference wrongly described this as a test of auto-repair (recovery treats an
invalid header as an unusable sector; nothing reconstructs it).

**Status: FIXED** — both methods now refuse in-use sectors and report whether
the target is in use / head / tail. Corruption tests must target sectors
outside the live ring.

### I3. `simulateIncompleteWrite()` could cross a sector boundary — VERIFIED

The torn-record injection (8-byte header + 32-byte partial payload) did not
check that 40 bytes remain in the open head sector; in the narrow open-head
states (24–36 bytes free) it programmed bytes into the following physical
sector. The initial `flush()` result was also ignored.

**Status: FIXED** — the injection now checks the `flush()` result and rotates
to a fresh sector unless the full injected byte sequence fits in the current
open head.

### I4. Random corruption could report false success — VERIFIED

`injectRandomCorruption()` used `rand() % TOTAL_SECTORS` — it could target
in-use sectors, pick duplicates, ignore each write result, and return `true`
even when every write failed (e.g. after `PARTITION_FAIL`).

**Status: FIXED** — targets are now preselected as unique, not-in-use sectors;
each result is recorded; the function reports partial failure and returns
`true` only when every requested corruption succeeded.

## Parser and reporting findings

### I5. Prefix matching and unvalidated numeric arguments — VERIFIED

`startsWith()` accepted garbage suffixes (`WEAR_TEST_GARBAGE`);
`String::toInt()` let `-1` become `UINT32_MAX` cycles and non-numeric text
become 0.

**Status: FIXED** — the parser now requires an exact command token followed by
at most one strictly numeric argument, with bounds (sector index in range,
`WEAR_TEST` cycles capped, `RANDOM_CORRUPT` count within the 25% limit) and
distinct parameter-error messages.

### I6. Method-level documentation drift — VERIFIED, HISTORICAL

The old reference describes the pre-rewrite engine: `0xDEADBEEF` magic writes
(now `0x00000000`), EEPROM state (now NVS `flashring`/`state2`), a record
`valid` flag (no longer exists), POST-as-repair recovery procedures (wrong),
wear-test details, and the browser JS API. These remain wrong in `old-docs/`
by design; `flash-testing.md` carries the correct per-command recovery
expectations (most injections require a restart, not POST).

### I7. `corruptRingPointers()` ignored the NVS write result — VERIFIED

**Status: FIXED** — the `putBytes()` result is now checked and returned.

### I8. `enableFailureInjection()` implies a gate that does not exist — VERIFIED

It only prints a warning; all compiled injection methods are callable
regardless. **Status: DOCUMENTED** — the help text now describes it as a
status print. A real armed-session state remains future work.

---

# Remaining work (accepted, not yet implemented)

- WebSerial authentication / an armed test-session state with timeout for
  destructive commands.
- Host-side deterministic state-machine tests for every power-cut boundary
  (including power loss during journal replay and disconnect between publish
  enqueue and PUBACK).
- Structured (typed) injection results instead of booleans; renaming the
  injection methods to describe actual behaviour.
- Rerun of the full bench validation plan in `flash-testing.md` on hardware,
  extended with PUBACK-interruption, failed-shutdown-flush, and
  power-loss-during-repair cases — required before at-sea reliance.
