# Response to Sol Code Review — Round 4 Remediation (Restructure)

Date: 17 July 2026
Responds to: `sol-code-review-5.md`
Updates: `sol-code-review.md` (verification-status policy; F1, F2, F4, F5),
`flash-feature.md`, `Power Loss and Recovery.md`, `flash-testing.md`

## Method acknowledgment

The directive is accepted without argument. Three consecutive rounds of
findings in code written to close the previous round is evidence against
branch-level patching, and R6 (my own short-circuit regression) and R8 (my
disproved claim about the library) are the two clearest exhibits. This round
was implemented from the state models below, with one central failure
transition, transactional recovery, deterministic fault seams, and an
on-device test matrix that asserts post-state. Where a required test has not
actually executed (no hardware in this environment), the finding is marked
*implemented, pending* — not fixed. The ledger now carries an explicit
verification-status policy saying exactly that.

---

## 1. State models (deliverable 1)

### 1.1 Flash ring (implemented in `FlashRingBuffer`, documented at the top of `FlashRingBuffer.h`)

States (exactly one holds at any time):

| State | Predicate |
| --- | --- |
| UNINITIALIZED | `!m_initialized` |
| TRUSTED | `m_initialized && !m_fatal && !m_writes_disabled` |
| SHUTDOWN_LOCKED | `m_initialized && !m_fatal && m_writes_disabled` |
| FATAL | `m_initialized && m_fatal` |

Admission table (also in the header, kept adjacent to the code):

| operation | TRUSTED | SHUTDOWN_LOCKED | FATAL | UNINIT |
| --- | --- | --- | --- | --- |
| appendRecord | yes | no | no | no |
| flush | yes | yes (no-op if empty) | no | no |
| peek / consume | yes | yes | no | no |
| drainOldestAssemblyRecord | yes | yes | **yes** (RAM only) | no |
| POST / DEEP | yes | yes | yes (POST = recovery path) | no |
| repairCorruption / factoryReset / clearAllData | yes | yes | yes | no |
| prepareForShutdown | yes | yes (no-op) | fails | no |
| teardown | persists | persists | **persists nothing** | frees only |

Transitions into FATAL go through exactly one function,
`enterFatalState(context)`, which also invalidates the peek and tail-cache
state. Its callers are: POST critical exits (partition missing/unreadable),
`scanAndRecover()` failure with flash modified / structure untrusted /
journal unresolved, and `eraseSector()`/`writeBytes()` observing a missing
partition at runtime (which is how `PARTITION_FAIL` and real partition loss
are caught on first touch). Transitions out of FATAL: a fully verified POST
repair (POST treats a fatal ring as never-healthy on its initial pass, so
auto-repair always runs the full rescan + verification; only a fully verified
result clears the flag), `factoryReset()`, or reinitialization.

`FlashTelemetryManager` observes the state through
`flashActive() = FLASH_ONLY && isInitialized() && !isFatal()`: commits and
pulls route to PSRAM in FATAL, and `salvageAssemblyAfterFatal()` (run at
cycle start in `getHeadBlockForPopulating()`, where the PSRAM head slot is
guaranteed unpopulated) moves any accepted-but-unflushed assembly records
into PSRAM — invariant 2: nothing reported as persisted is ever stranded.

### 1.2 Recovery (transactional)

`scanAndRecover()` = `buildRecoveredState()` + `applyRecoveredState()`.
`buildRecoveredState()` writes NOTHING into live members: it fills a
`RecoveredState` struct (sector map, full head tuple, full tail tuple, record
count, next seq) starting from `canonicalEmptyState()`. Outcomes:

- **Success** → `applyRecoveredState()` publishes the candidate as one
  deliberate transition (cache/peek invalidated). An empty partition result
  IS the canonical empty state — no stale members can survive (R13).
- **Failure, flash unmodified, structure not implicated** (e.g. transient
  read fault mid-scan) → the previous trusted state is retained wholesale;
  at runtime the diagnostic reports failure, nothing else changes.
- **Failure with flash modified** (journal replay / torn rescue began),
  **unresolved journal disposition**, or **structural corruption** (open
  sector that is not the highest-seq head — R15) → `enterFatalState()` at
  runtime; `init()` failure at boot.

The RAM assembly buffer's explicit policy across recovery: recovery never
touches it. After a successful recovery the pending records flush to the
recovered head as normal; after a fatal outcome the manager salvages them to
PSRAM. (Asserted by matrix case 11 and manager case M1.)

Journal disposition (R14): once a committed journal header is observed, every
exit of `replayRepairJournal()` either replays-and-verifiably-clears,
discards-and-verifiably-clears (target untouched), or returns failure — the
discard branches now check their erase like every other path.

### 1.3 MQTT QoS 1 acknowledgment transaction (`MercatorMQTT`)

States: **no attempt** (`awaitedPacketId == 0`) → **armed** (`== packet id`
of the single in-flight publish) → **satisfied** (`awaitedAckSeen`) or
**invalidated** (reset, then forced `disconnect(true)`). Rules:

- `resetAckAttempt()` runs BEFORE every publish: a retained ack — including
  one whose 16-bit ID wrapped around to match — cannot satisfy a new attempt
  (R17).
- `noteAck(id)` (the onPublish callback) satisfies only the currently armed
  ID; an ack with no attempt armed is ignored.
- Timeout invalidates the attempt BEFORE the transport reset. The
  `disconnect(true)` from round 3 is retained per your acceptance list.
- The ack-before-arm race window (sub-RTT, microseconds) resolves to a
  timeout and retry — a harmless duplicate, never a false success.
- State is `std::atomic<uint16_t>` / `std::atomic<bool>` — the volatile
  objection is accepted; atomics provide the inter-task visibility guarantee.

### 1.4 OTA task ownership (R16)

Owner: the **main loop task** owns `FlashTelemetryManager`,
`FlashRingBuffer`, and `TelemetryPipeline`, exclusively. The async web task's
OTA begin callback now does exactly two things: store a request into
`std::atomic<bool> otaPreparationRequested` and wait (10 ms polls, 15 s
bound) for `otaPreparationDone`. The main loop checks the request at the top
of `loop()` — before the OTA halt short-circuit — and runs
`prepareSystemForOTA()` (persist first, then halt, then teardown) entirely on
the owning task. If the main loop cannot complete within the bound, the OTA
proceeds with an unmissable warning that unsent telemetry will be lost —
the persistence result is reported truthfully either way, per your explicit
allowance that proceed-with-warning may remain the product policy provided
the race is gone.

---

## 2. Finding-by-finding (deliverable 2)

| Finding | Code change | Deterministic test | Result |
| --- | --- | --- | --- |
| R12 critical POST exits leave flash active | POST partition-missing/unreadable exits call `enterFatalState()`; `eraseSector`/`writeBytes` on a null partition at runtime also enter it (covers `PARTITION_FAIL` and staging-into-assembly) | Matrix 1 (partition null: POST fatal → append refuses → repair recovers), Matrix 2 (probe read seam: fatal, assembly stays empty) | Implemented; compiles; **device run pending** |
| R13 empty recovery publishes stale head state | Transactional recovery; `canonicalEmptyState()` is the complete empty result, published atomically | Matrix 4 (destroy open head's header → repair → assert canonical fields → append+flush → header stamped → teardown/init → exact record read back) | Implemented; **device run pending** |
| R14 committed journal can survive while writes resume | Both discard branches verify their erase; any unresolved disposition fails recovery (boot: init fails; runtime: fatal) | Matrix 5 (replay + journal-erase seam → fatal, append refuses; seam cleared → replay completes, journaled record served), Matrix 6 (data-CRC mismatch + erase seam → fatal; then discard succeeds, target proven untouched) | Implemented; **device run pending** |
| R15 open non-head sector passes POST / undercounts | Structural ownership invariant in `buildRecoveredState()` (any open sector must be the highest-seq head; refuse to guess, no destructive repair); `verifyStructuralState()` and POST's initial pass require RAM/flash agreement on the open sector | Matrix 7 (older open + newer closed sector: recovery refuses → fatal, count untouched, POST cannot pass) | Implemented; **device run pending** |
| R16 OTA calls single-task storage from async task | Atomic request/done handshake; preparation and teardown run only on the main loop | Structural (ownership by construction); bench case: OTA during active commit/pull/migration/PUBACK-wait | Implemented; **bench-only validation, not in matrix** |
| R17 stale/wrapped PUBACK completes a later publish | Ack transaction (reset-before-publish, armed-ID matching, atomics, invalidate-before-reset) | `runAckStateTests()` cases A–E (wrapped-ID retained state, mismatched ack, ack-before-arm, fresh match, invalidation) run inside TEST_MATRIX | Implemented; **device run pending** (logic is host-independent and deterministic) |
| R18 deep verification silently downgrades | `countNonblankInvalidHeaderSectors(deep, anomalies&) -> bool complete`; incomplete = failed; POST does not clear fatal on incomplete verification | Matrix 8 (alloc seam: POST fails, fatal retained; seam cleared: recovers) | Implemented; **device run pending** |

Regression-suite cases: Matrix 3 (mid-scan failure keeps previous state
wholesale, records readable), 9 (R11 erased-bit semantics), 10 (per-step
shutdown failures NOT SAFE + retryable), 11 (assembly across successful
recovery), 12 (existing self/recovery/stress suite), M1 (manager salvage +
routing demotion, 3 records accounted end-to-end).

## 3. Fault seams (deliverable 3)

All in `FlashRingBuffer::FaultSeams`, compiled ONLY under `TESTING_MODE`
(the struct, the member, and every guard are inside `#ifdef TESTING_MODE`;
non-testing builds contain no seam code at all). All default to inert;
countdown semantics: the N-th subsequent guarded operation fails once, then
the seam re-arms to inert.

| Seam | Guarded operation |
| --- | --- |
| `fail_read_countdown` | `readBytes()` (partition reads) |
| `fail_program_countdown` | `writeBytes()` (program ops; fires AFTER the known-erased bit is cleared, modelling a real partial program) |
| `fail_erase_countdown` | `eraseSector()` on ring sectors |
| `fail_journal_erase_countdown` | `eraseSector()` on the journal sector specifically |
| `fail_nvs_save_countdown` | `savePersistedState()` |
| `fail_diag_alloc` | diagnostic buffer allocation (deep verification, DEEP, repairCorruption) |

MQTT: no seam needed — `runAckStateTests()` drives the transaction helpers
(`resetAckAttempt`/`armAckAttempt`/`noteAck`) directly, which are the exact
functions the production publish path uses. Test-only entry points:
`testForceFatal()` (deterministic fatal transition for the manager salvage
case) and the matrix itself; both `TESTING_MODE`-only.

## 4. Test commands and output (deliverable 4)

Device command (WebSerial or USB, `TESTING_MODE` + `USE_FLASH_TELEMETRY`
build, empty ring):

```text
TEST_MATRIX
```

runs, in order: manager case M1, ring cases 1–12, MQTT ack cases A–E. Every
case asserts post-state (fatal flags, admission results, record counts,
canonical-state fields, on-flash headers read back, PSRAM lengths) and prints
one `MATRIX <name> PASS|FAIL` line; the command ends with an overall verdict
and leaves the ring reset.

**No device output is included in this response because no hardware was
available in this environment; the matrix has not executed.** Per your gate,
no finding is marked fixed on the strength of this response — the ledger
carries *implemented, pending TEST_MATRIX* markers, to be promoted only from
a passing bench log. This is deliverable 7 applied to deliverable 4 rather
than a claim of completed verification.

## 5. Build results (deliverable 5)

All four configurations build **successfully** (not "clean": the build has
pre-existing warnings — `USB_SERIAL` redefinition from `SerialConfig.h`,
ArduinoJson/AsyncWebServer deprecation warnings in libraries and legacy
code). **Zero warnings originate in the files changed this round** (verified
by filtering the build log for the changed files).

```text
pio run -e release
env PLATFORMIO_BUILD_FLAGS='-D USE_FLASH_TELEMETRY' pio run -e release
env PLATFORMIO_BUILD_FLAGS='-D USE_FLASH_TELEMETRY -D TESTING_MODE' pio run -e release
env PLATFORMIO_BUILD_FLAGS='-D USE_FLASH_TELEMETRY -D TESTING_MODE -D USE_WEBSERIAL' pio run -e release
```

`git diff --check` passes. The flash+TESTING_MODE image is 69.1% of the app
partition (the matrix adds ~14 KB, all excluded from non-testing builds).

## 6. Ledger policy (deliverable 6)

`sol-code-review.md` now states the verification-status policy explicitly and
marks F1/F2/F4/F5 supplements as *IMPLEMENTED, PENDING TEST_MATRIX* rather
than fixed. R6, R7, R9, R10 are accordingly NOT presented as closed. One
bookkeeping correction: the round-3 F2 ledger edit I claimed had applied had
in fact failed to apply (I misread a duplicate-edit error as success); the
paragraph has now actually been updated. That error is exactly the "status
updated without verification" failure mode your directive targets, recorded
here deliberately.

## 7. Not tested on hardware (deliverable 7)

- The entire TEST_MATRIX (all cases above) — implemented, never executed.
- R16 concurrency: OTA begin during active commit / flash pull / migration /
  PUBACK wait. The design removes the cross-task calls by construction, but
  the bench case remains mandatory.
- Matrix 12/13/14 network halves: sustained PUBACK loss with TCP alive
  (bounded retries, reconnection, recovery on ack resumption) — the ack
  transaction logic is covered by `runAckStateTests()`, the transport
  behavior needs a broker.
- Shutdown-migration failure (matrix 16's migration leg) — not seamed; the
  PSRAM pipeline has no fault seam. Listed as a gap, not silently skipped.
- All previously listed hardware cases: power cuts at journal boundaries,
  real partial program/erase behavior, `X` capture-refusal, two-dive
  rehearsal.

## Standing scope points

Per your instruction, the two round-4 scope points (header-only initial POST
pass; OTA proceed-with-warning) are not re-argued. Both are implemented as
previously described, with the constraints you set: the initial pass cannot
create a false post-repair proof (post-repair verification always uses the
deep check and reports incompleteness as failure), and the OTA policy now
executes without cross-task storage access and reports the persistence result
truthfully.
