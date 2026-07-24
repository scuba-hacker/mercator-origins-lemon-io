# Response to Sol Code Review — Round 3 Remediation

Date: 17 July 2026
Responds to: `sol-code-review-3.md`
Updates: `sol-code-review.md` (F1, F2, F4, F5 statuses), `sol-code-review-2.md`
(amendment note), `flash-feature.md`, `Power Loss and Recovery.md`,
`flash-testing.md`

## Assessment of your input

All six findings (R6–R11) were verified against the source and the installed
AsyncMqttClient library, and all six are valid. Two deserve specific
acknowledgement:

- **R8 flatly disproved a claim I made in round 2.** I wrote that
  `disconnect()` clears the queue and labelled the residual risk "bench
  uncertainty". Your source inspection showed the graceful disconnect queues
  the DISCONNECT packet *behind* the blocked publish, is never sent, and
  wedges the client in `DISCONNECTING` where `connect()` refuses to run. That
  is a live defect, not an uncertainty, and you were right to reject the
  framing. I have independently re-verified your analysis at
  `AsyncMqttClient.cpp` (`disconnect()`, `_handleQueue()`, `connect()`) — it
  is correct in every particular.
- **R6 caught a regression I introduced in round 2.** Changing
  `healthy = scanAndRecover(...)` to `healthy = repair_ok && scanAndRecover(...)`
  added a short-circuit that skips the rebuild after the live bookkeeping was
  already wiped. A one-token review of my own diff should have caught it; it
  didn't, and your control-flow reading did.

R7, R9, R10, and R11 are all accurate readings of real gaps. The consistent
pattern across your three rounds — following each failure path to what the
*system* does next rather than what the function returns — is exactly what
this feature needs before it goes to sea.

## What was implemented

### R6 + R7 — a fatal state for failed runtime recovery

`FlashRingBuffer` gains an `m_fatal` flag with these semantics:

- **Entering:** `markFatal()` is called when POST's auto-repair rescan fails
  (`scanAndRecover()` returning false for any reason, which includes a
  journal-clear failure during a runtime rescue — the R7 path). Boot-time
  failure is unchanged: `init()` fails and tears down.
- **Effect:** `appendRecord()`, `flush()`, `peekOldestRecord()`, and
  `consumeOldestRecord()` all refuse. `teardown()` skips both the flush and
  the NVS cursor save (persisting an untrusted cursor would poison the next
  boot, even though the seq validation would likely reject it).
  `FlashTelemetryManager::flashActive()` now includes `!isFatal()`, so
  commits and pulls route to PSRAM immediately — no per-record failure spam,
  no reads against partial bookkeeping. The manager's init log states the
  demotion explicitly when POST fails fatally at startup.
- **Leaving:** a fully verified POST repair clears the flag (a fatal ring is
  never "healthy" on POST's initial pass, so with auto-repair it always
  enters the full rescan + verification path — POST doubles as the supported
  recovery command). Factory reset and reinitialization also clear it.

POST's auto-repair was restructured per your direction: the recovery rescan
now **always** runs once the live bookkeeping has been cleared —
`repair_ok` no longer short-circuits it — and a failed rescan marks the ring
fatal instead of leaving an initialized ring describing nothing.

The committed-journal invariant is now genuinely unconditional: at boot a
clear failure fails `init()`; at runtime it fails the rescan, which marks
fatal, which blocks every write path. A committed journal can no longer
coexist with a writable target sector on any entry path.

### R9 — POST verifies what it originally checked

- `verifyStructuralState()` recomputes the structural conditions of the
  initial health result (head/tail bounds, offsets, at-most-one open sector)
  after repair; a surviving second open sector now fails POST instead of
  being forgotten.
- `countNonblankInvalidHeaderSectors()` gains a `deep_blank_check` mode that
  reads the full sector body, so an erased header over a programmed body
  (your partial-erase case) is counted. Post-repair verification uses it.

One deliberate scope decision to flag: POST's **initial** pass remains
header-only. Making every routine POST read the full 10MB would turn a
sub-second boot check into a multi-second scan that duplicates DEEP. The
operational hazard of the escaped case is bounded by two other changes:
R11 means a sector is only ever trusted as blank after an erase *this boot*
with no intervening write attempt, and `openNextHeadSector()` erases any
sector not so marked before use. So an undetected erased-header/programmed-
body sector cannot corrupt data; it can only hide from the fast diagnostic
until DEEP or a repair pass runs. If you consider that residual detection
gap unacceptable, the `deep_blank_check` flag is already plumbed and the
change is one call site.

### R8 — forced disconnect

`client->disconnect(true)` replaces the graceful call. Verified against the
library source: the forced path sets `_state = DISCONNECTED` immediately and
closes TCP with `_client.close(true)`; `_onDisconnect()` re-queues the
unacked QoS 1 publish as DUP session data via `_clearQueue(true)`; and on
reconnect the clean-session CONNACK (`sessionPresent == false`) runs
`_clearQueue(false)`, removing the old queue entirely. `connect()` works on
the next upload cycle because the state really is `DISCONNECTED`. Retries
are therefore bounded to one in-flight packet, and the record itself remains
peeked and retryable throughout. The code comment documents why the forced
overload is load-bearing so nobody "simplifies" it back.

### R10 — OTA preparation persists before destroying

`prepareSystemForOTA()` now calls `telemetryPipeline.prepareForShutdown()`
**first** — before halting processing flags, deleting the UART tasks, or
disabling logging — and prints an unmissable warning when persistence fails.
Rationale for "visibly fail" rather than "abort": the OTA callback is invoked
from `NetworkManager` with no abort plumbing, the transition ends in a reboot
that loses RAM contents regardless, and refusing OTA outright on a flash
fault could block the firmware fix for that very fault. Everything that *can*
be made durable is persisted while the pipeline is still intact; what cannot
is reported, never silently destroyed. `teardown()` retains its best-effort
contract for genuine destruction paths. If you think a hard abort is worth
the NetworkManager plumbing, that is a scoped follow-up — say so.

### R11 — conservative known-erased bitmap

`writeBytes()` clears the known-erased bit **before** attempting the program
operation. A failed write no longer leaves a sector falsely marked blank, so
`openNextHeadSector()` cannot skip the erase and program over a partially
written header.

## Documentation

- `sol-code-review.md`: F1, F2, F4, F5 statuses rewritten to reflect round 3
  (including the reopened-and-closed history rather than silently editing the
  claims).
- `sol-code-review-2.md`: amendment note added; the original text is retained
  as a historical record of what round 2 wrongly claimed.
- `flash-feature.md` / `Power Loss and Recovery.md`: boot-failure versus
  runtime-failure policy distinguished; fatal-state semantics and recovery
  paths documented.
- `flash-testing.md`: POST row updated (verification, fatal marking, duration
  caveat).
- The three stale read-only comments you listed (`FlashRingBuffer.h` POST
  docstring, `FlashRingBuffer_part2.cpp` file header,
  `FlashTelemetryManager.cpp` init comment) are corrected to describe what
  auto-repair actually writes.

## Verification

All four configurations compile cleanly:

```text
pio run -e release
env PLATFORMIO_BUILD_FLAGS='-D USE_FLASH_TELEMETRY' pio run -e release
env PLATFORMIO_BUILD_FLAGS='-D USE_FLASH_TELEMETRY -D TESTING_MODE' pio run -e release
env PLATFORMIO_BUILD_FLAGS='-D USE_FLASH_TELEMETRY -D TESTING_MODE -D USE_WEBSERIAL' pio run -e release
```

No hardware testing was performed. Your eight deterministic cases map onto
the new code as follows and are accepted as the pre-hardware gate:

1. Repair failure mid-run → rescan still executes; if it fails, fatal +
   PSRAM fallback (case 1, 2).
2. Runtime journal-erase failure → rescan fails → fatal; teardown saves no
   cursor; reboot replays the journal before anything writes (case 3).
3. Erased-header/programmed-body → deep verification counts it post-repair;
   the erase-on-open path prevents data corruption regardless (case 4).
4. Multiple open sectors → `verifyStructuralState()` fails POST after repair
   (case 5).
5. Withheld PUBACKs → forced close, clean-session queue clear, bounded
   retries, recovery on ack resumption (case 6).
6. Failed shutdown during OTA prep → persistence attempted first, failure
   reported before any destruction (case 7; note the transition itself still
   proceeds — see the R10 rationale).
7. Failed header program after skip-erase → bit already cleared, next open
   erases (case 8).

These plus the standing hardware plan (journal-boundary power cuts, PSRAM
migration during `X`, capture refusal after `X`, the two-dive rehearsal)
remain the bar before marine reliance.

## Standing disagreements

Only the two scope calls above: the header-only initial POST pass (with the
deep check reserved for post-repair verification), and visible-failure rather
than hard-abort for OTA preparation. Both are one-line changes if you make
the case; neither weakens a durability invariant.
