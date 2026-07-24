# Power Loss and Recovery

How the flash persistence system behaves through power interruptions.
Companion documents: `flash-feature.md` (design) and `flash-testing.md`
(how to exercise each recovery path).

## What power loss can hit, and what happens

| Power lost during... | Result after next boot |
| --- | --- |
| Idle / between flushes | Nothing lost from flash. Up to ~15s of records in the RAM assembly buffer are lost (bounded by the age-based flush). |
| A record append (flash program op) | Torn append detected: valid records rescued through the journal transaction, sector rebuilt with the same seq. Only the torn final record lost. |
| The torn-append repair itself | Recoverable at every step: before the journal header is committed the damaged sector is untouched and the rescue reruns; after it, boot replays the journal (idempotently) until the rebuild and journal-clear complete. |
| Sector close (sentinel or marker write) | Close marker CRC invalid → sector treated as open and record-scanned. No data loss. |
| Sector open (header write) | Header CRC invalid → sector holds no committed data; POST reports it and auto-repair erases it back into the free pool. |
| Tail sector reclaim (erase) | Records in that sector were already MQTT-acknowledged (PUBACK received). NVS cursor may be stale → worst case some already-uploaded records are re-uploaded. Never loss. |
| NVS cursor save | Old cursor wins → re-upload of some acknowledged records. Never loss. |

There is no window in which previously committed flash records can be
destroyed: sectors holding committed data are never erased or rewritten. The
torn-append rescue rewrites only the sector that was already damaged, and only
after its valid records have been made durable in the reserved journal sector
— a second (or third) power cut during the repair cannot lose them.

## Detection mechanisms

1. **Sector header CRC** (`hdr_crc` over magic+seq, written once at open).
2. **Close marker CRC** (`closed_crc` over used+count, written once at close).
   Open vs closed is unambiguous: the fields are 0xFF until the single 8-byte
   close write.
3. **Per-record CRC16-CCITT** over meta+payload, checked on every read and
   during recovery scans.
4. **Erased-space check**: on an open (head) sector, every byte after the last
   CRC-valid record must be 0xFF. Programmed bytes there = torn append.
5. **0x55AA sentinel fill**: written into the unused remainder when a sector
   is closed, so a closed sector contains no erased gaps (verified by the DEEP
   diagnostic).

## Boot recovery sequence (`FlashRingBuffer::init`)

1. Replay the repair journal if a committed journal exists (an interrupted
   torn-head repair) — the target sector is rebuilt and the journal cleared
   before anything else runs.
2. Read all 2559 ring sector headers (24 bytes each — sub-second).
3. Sectors with valid `hdr_crc` are in use; order them by `seq`
   (stamped once, never rewritten → head = max, tail = min, no ambiguity).
4. Closed sectors: trust `closed_used`/`closed_count` via `closed_crc`.
5. Head sector, if open: record-by-record scan for the true append point;
   journal-rescue if torn (see above).
6. Restore the tail cursor from NVS (`flashring`/`state2`) only if it matches
   the scanned tail sector's seq — a stale or corrupted NVS entry is ignored
   and the cursor falls back to the start of the oldest sector. For a
   non-empty ring the validated cursor is saved back to NVS at the end of the
   scan.
7. Record counts are cached from this scan; nothing rescans flash at runtime.

A blank partition boots with **zero flash writes** — the first sector is
opened lazily by the first flush.

## Safe power-down

Send the `X` serial/WebSerial command (which calls
`telemetryPipeline.prepareForShutdown()`):

1. Quiesces telemetry: new head-block commits and tail pulls are refused from
   this point, so nothing mutates storage behind the safety report.
2. Migrates any blocks still in the volatile PSRAM pipeline to flash and
   verifies PSRAM is empty.
3. Flushes the RAM assembly buffer to the head sector and saves the tail
   cursor and counters to NVS.
4. Only if EVERY step verified successful: locks further flash writes and
   logs `SAFE TO POWER OFF`. New telemetry is dropped (logged) until the next
   power cycle.
5. On any failure it logs `NOT SAFE TO POWER OFF`, resumes normal capture,
   and the command can be retried. If flash is unavailable (init failure,
   PSRAM-only fallback) the command always reports NOT SAFE — volatile
   storage cannot be a safe persistent shutdown.

After `SAFE TO POWER OFF`, cutting power loses nothing at all. Without it,
the exposure is only the ≤15s of records still in RAM.

One deliberate failure mode: once a committed repair journal has been
observed, normal operation resumes only after its disposition is fully
resolved — replayed and verifiably cleared, or (when its data cannot be
trusted) discarded and verifiably cleared with the target untouched. Any
unresolved disposition, including a failed erase in a discard branch, refuses
to continue: resuming flash writes ahead of a stale committed journal would
let a later boot replay the old snapshot over newly appended records. At boot
this fails `init()` and the manager falls back to PSRAM. At runtime it enters
the **fatal** state: bookkeeping is untrusted, every flash data operation
refuses, nothing further is written (including the NVS cursor at teardown),
and records still in the RAM assembly transfer to PSRAM with source removal
only after the destination commit succeeds. A full PSRAM destination retains
the assembly source and causes an explicit capture refusal rather than an
oldest-record eviction. Recovery is otherwise transactional — a runtime scan that
fails with flash unmodified keeps the previous trusted state intact. A fully
verified POST repair, factory reset, or reboot restores normal operation.

POST repair erases and recovery scanning are one trust transaction. Once any
garbage sector has been erased, a failed recovery or incomplete final proof
cannot resume with the pre-repair map; the ring stays fatal until a complete
verified recovery succeeds.

## Testing the recovery paths

- `RECOVERY` (WebSerial) / `performPowerLossRecoveryTest()`: writes a known
  batch, injects a torn append directly into flash, re-initializes and
  verifies every record survives. **Requires an empty ring** — it will refuse
  to run over a real backlog.
- `TESTING_MODE` builds add `SIMULATE_POWER_LOSS`, `CORRUPT_STATE`,
  `CORRUPT_POINTERS`, `CORRUPT_SECTOR n`, `CORRUPT_CRC n` for exercising each
  detection path with a restart. See `flash-testing.md`.
