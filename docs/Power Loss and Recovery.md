# Power Loss and Recovery

How the flash persistence system behaves through power interruptions.
Companion documents: `flash-feature.md` (design) and `flash-testing.md`
(how to exercise each recovery path).

## What power loss can hit, and what happens

| Power lost during... | Result after next boot |
| --- | --- |
| Idle / between flushes | Nothing lost from flash. Up to ~15s of records in the RAM assembly buffer are lost (bounded by the age-based flush). |
| A record append (flash program op) | Torn append detected: valid records rescued, sector rebuilt in place with the same seq. Only the torn final record lost. |
| Sector close (sentinel or marker write) | Close marker CRC invalid → sector treated as open and record-scanned. No data loss. |
| Sector open (header write) | Header CRC invalid → sector treated as erased; it is re-erased when next used. No committed data involved. |
| Tail sector reclaim (erase) | Records in that sector were already MQTT-acknowledged. NVS cursor may be stale → worst case some already-uploaded records are re-uploaded. Never loss. |
| NVS cursor save | Old cursor wins → re-upload of some acknowledged records. Never loss. |

There is no window in which previously committed flash records can be
destroyed: sectors holding committed data are never erased or rewritten (the
single exception is the torn-append rescue, which rewrites only the sector
that was already damaged, after copying its valid records to RAM).

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

1. Read all 2560 sector headers (24 bytes each — sub-second).
2. Sectors with valid `hdr_crc` are in use; order them by `seq`
   (stamped once, never rewritten → head = max, tail = min, no ambiguity).
3. Closed sectors: trust `closed_used`/`closed_count` via `closed_crc`.
4. Head sector, if open: record-by-record scan for the true append point;
   rescue if torn (see above).
5. Restore the tail cursor from NVS (`flashring`/`state2`) only if it matches
   the scanned tail sector's seq — a stale or corrupted NVS entry is ignored
   and the cursor falls back to the start of the oldest sector.
6. Record counts are cached from this scan; nothing rescans flash at runtime.

A blank partition boots with **zero flash writes** — the first sector is
opened lazily by the first flush.

## Safe power-down

Call `telemetryPipeline.prepareForShutdown()`:

1. Flushes the RAM assembly buffer to the head sector.
2. Saves the tail cursor and counters to NVS.
3. Locks further writes.
4. Logs `SAFE TO POWER OFF` on USB serial.

After that, cutting power loses nothing at all. Without it, the exposure is
only the ≤15s of records still in RAM.

## Testing the recovery paths

- `RECOVERY` (WebSerial) / `performPowerLossRecoveryTest()`: writes a known
  batch, injects a torn append directly into flash, re-initializes and
  verifies every record survives. **Requires an empty ring** — it will refuse
  to run over a real backlog.
- `TESTING_MODE` builds add `SIMULATE_POWER_LOSS`, `CORRUPT_STATE`,
  `CORRUPT_POINTERS`, `CORRUPT_SECTOR n`, `CORRUPT_CRC n` for exercising each
  detection path with a restart. See `flash-testing.md`.
