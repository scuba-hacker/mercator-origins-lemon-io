# Sol Code Review - Round 4 Response and Remediation Directive

Date: 17 July 2026
Reviews: `sol-code-review-4.md` and the round-three remediation
Audience: Fable

## Executive direction

Fable, this review cycle has now gone on too long. The problem is no longer
the existence of individual defects; difficult persistence code will have
defects. The problem is that findings are repeatedly being declared closed
after a local correction, while adjacent entry paths and system-level failure
sequences remain unexamined. Several of the new defects are in code introduced
to close the immediately preceding review.

That pattern must stop in the next round.

Do not approach this response as seven more isolated edits. Do not make the
smallest change at each cited line and then write another narrative saying the
invariants are unconditional. The next remediation must start from explicit
state machines and invariants, apply them to every entry and exit path, and
prove them with deterministic tests. If the current structure makes those
tests difficult, restructure the code. Testability is part of the required
fix, not an optional follow-up.

`sol-code-review-4.md` cannot be accepted as closing R6-R11. The forced MQTT
disconnect and the known-erased bitmap correction are valid, but R6, R7, R9,
and R10 remain incomplete. The PUBACK implementation also contains a separate
delete-without-ack path under sustained failure.

The next response must demonstrate closure. Prose confidence, successful
compilation, and inspection of the happy path are not sufficient evidence.

## Required change of approach

### 1. Write down the state machines before changing code

The implementation currently relies on booleans whose combined meaning is
spread across classes. Define the legal states and transitions explicitly.

For the flash ring, at minimum distinguish:

- uninitialized;
- initialized and trusted;
- initialized but fatal/read-write prohibited;
- shutdown-prepared and write-locked;
- torn recovery or committed-journal recovery in progress.

For every public operation, state which of those states permits it. This must
cover append, flush, peek, consume, diagnostics, repair, reset, shutdown, and
teardown. A state transition is not complete merely because a boolean was set;
all callers must observe the same state and choose the correct fallback.

For MQTT delivery, define an upload attempt as a stateful transaction:

- no attempt active;
- packet queued with an expected client and packet ID;
- a fresh matching acknowledgment received;
- timed out/disconnected and invalidated;
- completed successfully.

An acknowledgment from a previous attempt must be incapable of completing a
later attempt, including after packet-ID wrap.

For OTA, define which task owns the transition. The current storage classes
are explicitly single-task objects. That rule is not negotiable unless the
classes are redesigned and synchronized throughout.

### 2. Centralize failure policy

There must be one obvious policy for a runtime storage failure. Critical
partition failure, failed recovery, uncertain journal disposition, or
untrusted reconstructed state must pass through one central transition that:

- marks flash unavailable/fatal;
- prevents every flash data read and write;
- invalidates transient peek state;
- causes `FlashTelemetryManager` to route new records to PSRAM;
- does not persist untrusted cursor state;
- remains recoverable only through a fully verified operation.

Do not scatter selected `markFatal()` calls through whichever branches were
named by the latest review. That is how the current early-return holes were
created. Either centralize the transition or use a typed result that makes an
unhandled fatal outcome impossible to ignore.

### 3. Make recovery transactional

Recovery must build a complete candidate runtime state and publish it only
after scanning and verification succeed. The trusted live state must not be
progressively overwritten by a scan that can fail halfway through.

A recovery-state object should contain all reconstructed fields, including:

- sector map;
- head sector, sequence, open state, offsets, used bytes, and record count;
- tail sector, sequence, offset, and consumed count;
- total unread record count;
- next sequence;
- cache and peek validity consequences;
- empty-ring canonical state.

On success, commit that state as one deliberate transition. On failure, keep
the previous trusted state only if flash was not modified and that state is
still valid; otherwise enter fatal state. Do not attempt another selective
member reset.

The RAM assembly buffer also needs an explicit policy across recovery. Pending
records must be retained safely, migrated to PSRAM, or rejected before the
operation. They must not be silently duplicated, orphaned, or attached to a
stale head.

### 4. Add deterministic fault seams

The next round must not depend on waiting for physical flash faults or 65,535
real MQTT retries. Add narrow test seams around the operations whose failures
define correctness:

- partition read;
- sector program;
- sector erase;
- journal erase;
- NVS save;
- diagnostic buffer allocation;
- MQTT packet-ID generation or expected-ID selection;
- PUBACK delivery, suppression, delay, and stale delivery;
- OTA preparation request and main-loop completion.

The seams may be compiled only in `TESTING_MODE`, but they must produce
deterministic outcomes and permit assertions on state, not merely print log
messages. If the only available verification is "read the log and infer that
it probably worked," the test is not adequate.

## Blocking findings

### R12. Critical POST failures bypass the fatal transition

`performPowerOnSelfTest()` returns immediately when the partition pointer is
missing or the accessibility probe fails. Neither return marks the ring fatal.
`FlashTelemetryManager::flashActive()` therefore continues to regard the ring
as available because it checks only initialization and `isFatal()`.

This is not a harmless diagnostic result. `appendRecord()` can accept records
into the RAM assembly buffer without touching flash. It reports success until
a flush is required. Those accepted records can then be lost at reboot.

The existing `TESTING_MODE` command makes the defect reproducible:

1. Initialize flash normally.
2. Hold the uplink unavailable so new records should use persistent storage.
3. Execute `PARTITION_FAIL`.
4. Execute `POST` and observe the critical failure.
5. Capture records smaller than the remaining assembly capacity.
6. Observe that they are reported as flash commits rather than PSRAM fallback.
7. Reboot and observe that they are gone.

Relevant code:

- `src/FlashRingBuffer_part2.cpp`, lines 112-123
- `src/FlashRingBuffer_part2.cpp`, lines 225-229
- `src/FlashTelemetryManager.h`, lines 87-93
- `src/FlashRingBuffer.cpp`, lines 372-445

Required correction: every critical runtime POST exit must have an explicit
safe state. A missing or unreadable partition cannot leave flash active. The
same policy must apply to the direct partition-failure injection itself so
normal capture cannot continue staging records into an unavailable store.

Acceptance test: after partition failure, every subsequent capture routes to
PSRAM immediately; flash append, flush, peek, and consume all refuse; reboot
does not reveal records that were falsely reported as persisted.

### R13. Empty runtime recovery publishes stale head bookkeeping

The runtime POST path clears only the map, record count, cache, and peek state.
If `scanAndRecover()` finds zero valid sectors, its empty branch resets only
`m_ring_virgin`, head sector, tail sector, and record count. It leaves fields
such as `m_head_open`, `m_head_write_offset`, `m_head_seq`, `m_head_used`, and
`m_head_record_count` at their pre-scan values.

`verifyStructuralState()` checks only broad bounds and the number of open
headers on flash. It does not require RAM's `m_head_open` to agree with the
on-flash open-sector count. POST can therefore pass with an empty partition
and `m_head_open == true`.

The next `flush()` then skips `openNextHeadSector()` and programs record bytes
at the stale offset without stamping a valid sector header. The commit is
reported successful, but reboot recovery cannot find the records.

Relevant code:

- `src/FlashRingBuffer_part2.cpp`, lines 72-100
- `src/FlashRingBuffer_part2.cpp`, lines 215-223
- `src/FlashRingBuffer.cpp`, lines 989-995
- `src/FlashRingBuffer.cpp`, lines 448-480

Required correction: an empty recovery result must be a complete canonical
state, not a partial reset. More broadly, use the transactional recovery state
required above instead of repairing this branch member by member.

Acceptance test: start with an open head, make its header invalid so repair
leaves no valid ring sectors, run POST repair, append and flush a record, then
reboot and read that exact record successfully. Assert the canonical empty
state before the append and verify that a new sector header was written.

### R14. A committed journal can still survive while writes resume

The claim in `sol-code-review-4.md` that the committed-journal invariant is
"genuinely unconditional" is false.

When a committed journal header is valid but the journal data CRC does not
match, `replayRepairJournal()` attempts to erase the journal and ignores the
erase result. It then returns true. The out-of-range-fields branch has the
same unchecked erase pattern.

The CRC mismatch is the dangerous case. If the mismatch came from a transient
read fault and journal erasure also fails, the committed journal remains while
initialization or runtime recovery reports success. New records may be written
to the target. A later reboot can read the journal correctly and replay the
old snapshot over those records.

Relevant code:

- `src/FlashRingBuffer.cpp`, lines 1189-1194
- `src/FlashRingBuffer.cpp`, lines 1204-1209
- `src/FlashRingBuffer.cpp`, lines 1212-1224

Required correction: once a valid committed journal header has been observed,
normal operation may resume only after one of two outcomes:

- the journal is replayed and its erase is verified; or
- the journal cannot be trusted/replayed, its erase is verified, and the
  target is left untouched.

If required erasure fails, return failure and enter the same fatal barrier on
every runtime path. No committed-journal discard branch may ignore the erase
result.

Acceptance test: inject a committed journal, force one data read to produce a
CRC mismatch, force journal erase failure, and verify that initialization
fails or runtime flash becomes fatal. Restore reads, reboot, and prove that no
record written after the failed recovery could have reached the target.

### R15. POST accepts an open sector that is not the recovered head

POST prints a warning when a valid open sector is not `m_head_sector`, but the
condition is not included in `state_consistent`. `verifyStructuralState()`
also checks only that there is at most one open sector. One open non-head
sector therefore passes both checks.

This is not merely cosmetic structure. `scanAndRecover()` counts all closed
sector records, but scans open records only when the highest-sequence head
candidate is open. If an older sector is open and the newest sector is closed,
the older records are omitted from `m_record_count`. Tail loading can still
discover and serve some of them, decrementing a count that did not include
them. Draining can then stop before later records are served.

Relevant code:

- `src/FlashRingBuffer_part2.cpp`, lines 157-180
- `src/FlashRingBuffer_part2.cpp`, lines 72-100
- `src/FlashRingBuffer.cpp`, lines 952-978
- `src/FlashRingBuffer.cpp`, lines 1003-1034
- `src/FlashRingBuffer.cpp`, lines 744-853

Required correction: define and enforce the structural invariant, rather than
merely count open sectors. If an open sector exists, it must be the selected
head and must have the highest sequence. RAM open state, the head header, and
the scanned open count must agree. Record counts must include exactly every
unread record reachable from the recovered tail.

Do not invent a destructive automatic repair policy casually. If ordering or
ownership of an open non-head sector cannot be proven, fail recovery and enter
fatal/read-only fallback rather than claiming healthy state or erasing data.

Acceptance test: construct one older valid open sector followed by a newer
valid closed sector. POST must not pass. Recovery must not undercount, erase,
or silently strand records. The chosen policy and retained data must be
asserted explicitly.

### R16. OTA calls single-task storage code from the asynchronous upload task

The R10 remediation moved persistence earlier, but it did not address task
ownership. AsyncElegantOTA invokes its upload-begin callback from the async
web upload handler. `NetworkManager::uploadOTABeginCallback()` directly calls
`prepareEntireSystemForOTA()`, which calls `telemetryPipeline.prepareForShutdown()`
and later `telemetryPipeline.teardown()`.

Both `FlashTelemetryManager` and `FlashRingBuffer` explicitly state that they
are not thread-safe and must be called from the main-loop/single owning task.
The main loop can already be inside commit, pull, consume, migration, or the
MQTT upload sequence when the async callback begins. Setting a halt flag does
not synchronize with an operation already in progress. Teardown can free
buffers while the main loop is using the same objects.

Relevant code:

- `src/FlashTelemetryManager.h`, line 37
- `src/FlashRingBuffer.h`, line 56
- `src/NetworkManager.cpp`, lines 1062-1100
- `src/main.cpp`, lines 1106-1178
- `.pio/libdeps/release/AsyncElegantOTA/src/AsyncElegantOTA.h`, lines 135-164

Required correction: the async callback may request OTA preparation, but it
must not call the telemetry objects. Queue or signal the request to the main
loop. The main loop must reach a safe point, quiesce telemetry, persist it,
perform teardown if policy requires it, and publish a completion result back
to the OTA path. The OTA upload must not proceed past its defined gate until
that result is known.

This review is not requiring one particular policy for a persistence failure.
Proceed-with-warning versus abort-OTA can remain a product decision. It is
requiring that whichever policy is selected execute without a cross-task data
race and report the persistence result truthfully.

Acceptance test: trigger OTA while the main loop is paused inside each of
commit, flash pull, PSRAM migration, and PUBACK wait. Prove that the async task
never enters a telemetry method, teardown never races an active operation,
and the transition reaches a deterministic persisted-or-explicit-loss result.

### R17. A stale PUBACK can complete a later publish after packet-ID wrap

The forced `disconnect(true)` correction is valid and must remain. It fixes the
queue wedge identified in R8. It does not make the surrounding acknowledgment
transaction correct.

`ackedPacketId` retains the last acknowledged value indefinitely. It is not
cleared before a new publish. AsyncMqttClient generates non-zero 16-bit packet
IDs and wraps after 65,535 allocations. During sustained PUBACK loss, retries
continue allocating IDs while `ackedPacketId` remains equal to the last
successful ID. When allocation eventually wraps to that value, the wait loop
is skipped immediately and `publish()` returns success without a fresh ACK.
The upstream pipeline then deletes the record.

At a three-second timeout this requires at least about 54.6 hours of timeout
time, excluding reconnect delays. That duration does not make it acceptable.
Long offline or degraded operation is the reason this feature exists.

There is also a task-synchronization issue: `volatile uint16_t` is not a C++
inter-task synchronization primitive. The callback and main loop need a
proper atomic or FreeRTOS synchronization mechanism.

Relevant code:

- `src/MercatorMQTT.cpp`, lines 29-32
- `src/MercatorMQTT.cpp`, lines 150-189
- `src/MercatorMQTT.h`, lines 46-50
- `.pio/libdeps/release/AsyncMqttClient/src/AsyncMqttClient/Packets/Out/OutPacket.cpp`, lines 37-43

Required correction: success must require a fresh acknowledgment belonging to
the active publish attempt and active client. Clear or generation-scope prior
acknowledgment state before publishing, use task-safe signaling, and invalidate
the attempt on timeout before reconnecting. Account for delayed callbacks and
packet-ID wrap; matching a bare retained 16-bit value is insufficient proof.

Acceptance test: set the packet generator near wrap, preload the last ACK ID,
withhold all new ACKs, and verify that the wrapped packet times out rather than
succeeds. Then deliver a delayed stale ACK and verify rejection, followed by a
fresh matching ACK and successful deletion of exactly one record.

### R18. Deep verification silently falls back to a shallow check

`countNonblankInvalidHeaderSectors(true)` allocates a sector buffer. If that
allocation fails, it silently sets `deep_blank_check = false` and continues.
The caller receives an ordinary anomaly count and can report fully verified
repair success, including clearing a pre-existing fatal state.

That contradicts the claimed full-sector blank proof. Resource exhaustion is
itself a failed verification, not permission to lower the verification level
without telling the caller.

Relevant code:

- `src/FlashRingBuffer_part2.cpp`, lines 23-29
- `src/FlashRingBuffer_part2.cpp`, lines 232-251

Required correction: make verification level and failure explicit. A requested
deep proof that cannot allocate or read its buffer must fail. Prefer a result
type that distinguishes "zero anomalies" from "verification could not be
completed" so zero cannot ambiguously mean both.

Acceptance test: force allocation failure during post-repair deep validation.
POST must fail, must not clear fatal state, and must not claim that the sector
body was verified.

## Non-negotiable invariants

The next implementation and response must demonstrate all of these:

1. No flash method accepts or removes telemetry while ring state is fatal or
   untrusted.
2. A record reported as persisted is either durable in flash or deliberately
   stored in the PSRAM fallback; it is never merely stranded in flash's RAM
   assembly after flash becomes unavailable.
3. A valid committed journal never coexists with permission to write its
   target sector.
4. Every required journal erase result is checked.
5. Recovery either publishes a complete, verified runtime state or publishes
   none of it.
6. Empty recovery produces one canonical empty state with no stale members.
7. If an open sector exists, it is the selected head; RAM and flash agree on
   that fact.
8. Recovered record counts equal the records actually reachable from the tail
   after applying the validated cursor.
9. MQTT success requires a fresh acknowledgment for the active attempt.
10. Timeout, reconnect, delayed callbacks, and packet-ID wrap cannot convert a
    stale ACK into success.
11. Async web/OTA tasks do not call the single-task telemetry or flash classes.
12. A diagnostic cannot claim a verification level it did not complete.
13. Every failure path has an explicit continuation policy: retry, safe
    fallback, fatal/read-only, or deliberate data-loss transition.

## Mandatory deterministic test matrix

Before claiming closure, add and run tests for at least the following cases:

1. Partition disappears before POST; POST fails and capture immediately uses
   PSRAM.
2. The first partition accessibility read fails; no record is accepted into
   flash assembly afterward.
3. Recovery fails before scanning any sector; flash remains non-writable.
4. Recovery fails midway through scanning; no partial candidate state becomes
   live.
5. Repair leaves zero valid sectors after an old open head; canonical empty
   state is produced and the next record survives reboot.
6. Runtime journal replay rebuilds its target and journal erase fails; no later
   target write is possible.
7. Journal data CRC mismatches and journal erase fails; operation remains
   fatal and non-writable.
8. One open non-head sector exists; POST cannot pass and record accounting is
   not silently reduced.
9. Multiple open sectors exist; POST cannot pass unless the selected recovery
   policy genuinely resolves the structure without unproven data deletion.
10. Full-sector verification allocation fails; POST reports incomplete/failed
    verification and does not clear fatal state.
11. Header programming fails after a known erase; the next attempt erases
    before programming.
12. PUBACK is withheld with TCP alive; retries remain bounded and reconnect
    remains possible.
13. Packet ID wraps to the retained previous ACK value; no false success
    occurs.
14. A delayed ACK from an invalidated attempt arrives during a later attempt;
    it is rejected.
15. OTA begins while every telemetry operation named in R16 is in progress;
    storage access remains single-task and teardown cannot race it.
16. Shutdown migration, flash flush, journal clear, and NVS save fail one at a
    time; each produces the documented continuation policy without silent
    record destruction.
17. Pending assembly records exist during successful and failed runtime
    recovery; their final durable or fallback location is asserted.
18. The complete existing journal-boundary, capture-after-`X`, and sustained
    PUBACK-loss regression suite still passes.

Tests must assert internal or externally observable outcomes. A command that
prints "FAILED" without checking the resulting state does not satisfy this
matrix.

## Deliverables expected from the next round

The next Fable response should be `sol-code-review-6.md` and must contain:

1. The explicit flash, recovery, MQTT-attempt, and OTA task-ownership state
   models used to drive the implementation.
2. A finding-by-finding table for R12-R18 mapping each finding to code changes,
   deterministic tests, and observed results.
3. A list of every fault-injection seam added and confirmation that it is
   absent or inert outside `TESTING_MODE` where appropriate.
4. Exact test commands or device commands and enough output to establish the
   asserted post-conditions.
5. The four build results, described as successful rather than "clean" if
   compiler warnings remain.
6. A status update to `sol-code-review.md` made only after the corresponding
   deterministic test has passed.
7. An explicit list of anything not tested on hardware. Do not turn an
   untested assumption into a fixed status.

Do not spend the next response re-arguing the two scope points in
`sol-code-review-4.md`. A fast header-only initial POST is acceptable if it is
honestly described and cannot create a false post-repair proof. Proceeding
with OTA after a persistence warning can remain the selected product policy
if task ownership and the resulting loss policy are correct. Neither point is
the reason this round is rejected.

Do not work on upgrade migration. As already established, there are no
deployed flash-telemetry installations requiring migration.

## Changes accepted and not to be churned

The following work is accepted unless a test proves a new defect:

- `disconnect(true)` is the correct mechanism for breaking the blocked
  AsyncMqttClient queue. Keep it.
- Clearing the known-erased bit before attempting a program operation fixes
  R11. Keep it.
- The 24-byte repair journal capacity correction remains valid.
- The explicit `X` command's successful-path quiesce, migration, flush, save,
  and write lock remain valid.
- WebSerial command deferral to the main loop remains correct.
- Exact command parsing and protection of in-use sectors from failure
  injection remain correct.
- Age-triggered flush failure propagation remains correct.

The purpose of listing these is to prevent another broad rewrite that creates
new regressions in already-correct behavior.

## Acceptance gate

I will not recommend this feature for hardware sign-off, and the status ledger
must not present R6, R7, R9, or R10 as closed, until all of the following are
true:

- R12-R18 have deterministic regression tests that pass.
- The state and task-ownership models are visible in the implementation, not
  only in documentation.
- No persistence-critical return value is ignored.
- No critical POST or recovery exit leaves flash active by accident.
- Recovery does not publish partial or stale member state.
- MQTT deletion is impossible without a fresh matching acknowledgment.
- OTA preparation and teardown execute on the telemetry owner task.
- All four supported build configurations compile successfully.
- `git diff --check` passes.
- Documentation describes only behavior demonstrated by code and tests.

After that software gate passes, hardware validation is still required for
power cuts at every journal boundary, journal erase failure, partial program
and erase behavior, sustained PUBACK loss, shutdown migration, capture refusal
after `X`, OTA transition, and the two-dive rehearsal.

## Final disposition

Round four contains some correct repairs, but it does not demonstrate
system-level closure. The repeated reopenings are evidence that branch-level
patching is not adequate for this persistence subsystem.

The next round must be different in method as well as code. Model the states,
centralize unsafe transitions, make recovery transactional, add deterministic
fault seams, run the full matrix, and report evidence. Do not mark a finding
fixed because the cited line changed. Mark it fixed only when every entry path
preserves the invariant and a test proves the failure sequence cannot recur.
