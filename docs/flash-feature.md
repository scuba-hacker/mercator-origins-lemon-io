# Flash Ring Buffer — Persistent Telemetry Storage

Design and implementation reference for the flash persistence system.
Companion documents: `Power Loss and Recovery.md` (failure behaviour) and
`flash-testing.md` (commands, diagnostics, bench validation).

## What it does

Telemetry records (combined mako + lemon binary packets) survive power cycles
in a dedicated **10MB raw flash partition** and spool to MQTT when connectivity
returns. The two-dive day works like this:

1. Power on at the dive site, no signal → records persist to flash.
2. Power off after dive 1. **Nothing is lost.**
3. Power on for dive 2, still no signal → new records append after dive 1's.
4. Power off. Travel to harbour. Power on with signal → the whole backlog
   spools to MQTT oldest-first while any new records continue to persist.
5. Once the backlog drains and the connection is stable, flash is left alone.

If the system is online from power-on and stays online, **flash is never
written at all** — telemetry flows through the original PSRAM pipeline exactly
as before this feature existed.

## Components

| Component | File(s) | Role |
|---|---|---|
| `FlashRingBuffer` | `src/FlashRingBuffer.{h,cpp}` + `_part2.cpp`, `_part3.cpp` | Raw-flash ring storage engine |
| `FlashTelemetryManager` | `src/FlashTelemetryManager.{h,cpp}` | Drop-in `TelemetryPipeline` replacement; routes between PSRAM and flash |
| Partition | `partitions/ota_spiffs_16MB.csv` | `flashbuf`, data/0x40, 10MB at 0x600000 (OTA app0/app1 untouched) |

Enable/disable with `#define USE_FLASH_TELEMETRY` in `src/main.cpp` (line ~44).
Commenting it out restores the original battle-tested PSRAM-only build — the
promised instant fallback.

## Storage engine design (FlashRingBuffer)

Geometry: 2560 × 4KB sectors. Records are 16–1008 bytes of payload.

### Golden rules (NOR flash discipline)

- **One erase per sector per ring cycle**, performed when the sector is opened
  as the new head. Sectors holding committed data are never erased or
  rewritten; the only other erase is reclaiming a **fully consumed** tail
  sector after its records were acknowledged by MQTT.
- Programming only ever targets erased (0xFF) bytes — bits only go 1→0.
- Records accumulate in a 4KB RAM assembly buffer and are appended to the open
  head sector in chunks (typically one program op per sector fill at 1Hz).

### On-flash format

```
Sector (4096B): [SectorHeader 24B][record slots ...][0x55AA sentinel fill on close]

SectorHeader:
  magic   u32  0x42474F4C "LOGB"        \  written once when the
  seq     u32  monotonic sequence        > sector is opened
  hdr_crc u32  CRC32(magic,seq)         /  (12 bytes)
  closed_used  u16  \  written once, in a single 8-byte program op,
  closed_count u16   > when the sector is closed (fields are 0xFF while open)
  closed_crc   u32  /
  reserved     u32  (erased)

Record slot (4-byte aligned):
  len u16 | crc16 u16 | meta u16 | rsvd u16 | payload | pad
```

- `seq` is stamped **once** and never rewritten, so boot recovery orders the
  ring unambiguously: head = highest seq, tail = lowest seq.
- `crc16` (CCITT) covers `meta` + payload.
- `meta` preserves the `BlockHeader` **roundedUpPayloadSize** through storage —
  without it the combined mako/lemon payload cannot be split for upload.

### Write path

`appendRecord()` → RAM assembly buffer → `flush()` programs the chunk into the
open head sector. Flush triggers:

- the assembly buffer fills the head sector's remaining space (≈17 records /
  ~17s at 1Hz), or
- the oldest unflushed record is ~15s old (bounds RAM loss on a power cut), or
- explicit `flush()` / `prepareForShutdown()`.

When a sector can't fit the next record it is **closed** (sentinel fill + the
8-byte close marker) and the next sector is opened lazily on the next flush.

### Read path (spooling) — delete-on-ack

- `peekOldestRecord()` returns the oldest record **without removing it** and is
  stable across repeated calls.
- `consumeOldestRecord()` advances the cursor — called only from
  `tailBlockCommitted()`, i.e. after the MQTT publish succeeded. A failed
  publish loses nothing and is retried.
- A fully consumed sector is erased and reclaimed immediately.
- The cursor can chase the open head sector while it is still being appended
  to: capture and upload run simultaneously.
- Ring full → the oldest sector is dropped (continuous capture preferred).

### Power-loss recovery (boot)

Read-only unless a torn write is found:

1. Scan all 2560 sector headers (24B each — fast).
2. Closed sectors are trusted via `closed_crc` (records still CRC-checked
   individually on read).
3. The open head sector is scanned record-by-record. Programmed bytes after
   the last valid record = **torn append** → valid records are rescued to RAM,
   the sector is erased and rebuilt with the same seq. Only the torn final
   record is lost — exactly the required behaviour.
4. Tail cursor restored from NVS (`flashring/state2`) if it matches the
   scanned tail sector (validated by seq); otherwise it falls back to the
   start of the oldest sector. Worst case is harmless re-upload, never loss.

A completely fresh partition results in **zero writes** at boot — the first
sector is opened lazily by the first flush.

### Safe power-down

Call `prepareForShutdown()` (via `FlashTelemetryManager`): flushes the assembly
buffer, saves the cursor, locks further writes, then logs `SAFE TO POWER OFF`.
An unplanned power cut loses at most ~15s of RAM-buffered records; flash
content is never corrupted (torn appends are rescued at next boot).

## Routing layer (FlashTelemetryManager)

Same API as `TelemetryPipeline` plus:

- `setUplinkAvailable(bool)` — fed each cycle from
  `privateMQTT.isUplinkUsable()` (connectivity **without** the upload
  duty-cycle throttle, so a momentary throttle doesn't divert data to flash).
- `getMaximumDepth()`, `getMaximumPipelineLength()`, `getHeadBlockIndex()`,
  `getTailBlockIndex()` — pass-throughs that the main loop's logging needs.

Routing rules:

| Uplink | Flash backlog | New commits go to | Pulls served from |
|---|---|---|---|
| available | empty | PSRAM (flash untouched) | PSRAM |
| available | present | flash (behind the backlog) | flash, oldest first |
| unavailable | any | flash | — (no pulls happen) |

When routing switches to flash and blocks are still sitting in the PSRAM
pipeline, they are **migrated to flash first** so ordering and persistence are
preserved. When the flash backlog fully drains, commits return to the pure
PSRAM path automatically.

`getPipelineLength()` etc. are O(1) — counters are cached, never derived by
scanning flash (the old implementation scanned up to 10MB per call).

## Diagnostics and testing

All boot-time checks are **read-only**. Destructive tests refuse to run unless
the ring is empty — a dive backlog is never used as test material.

USB serial single-key commands (`src/main_part2.cpp`):

- `S` — status: mode, pipeline length, flash records/space, routing stats
- `F` — show compile-time flash setting
- `R` — factory reset the flash buffer
- `D` / `C` — simulate WiFi loss / return
- `H`/`?` — help

WebSerial commands: `POST` (read-only structural scan), `DEEP` (full 10MB
record-level CRC scan), `STRESS`, `RECOVERY` (both require an empty ring).

Failure injection (`TESTING_MODE` builds only): corrupt sector headers/CRCs,
torn-append simulation, NVS state corruption, wear testing — see
`flash-testing.md`.

## Capacity and wear

- 10MB / ~244B per stored record (236B payload + 8B header, 4B-aligned)
  ≈ **41,000 records ≈ 11.4 h at 1Hz** before the ring wraps.
- Per sector cycle: 1 erase + ~3–4 program ops (header, data chunk(s),
  sentinel+close). At continuous 1Hz offline logging that is one sector per
  ~17s → each individual sector is erased roughly once every 12 hours of
  continuous offline use. At 10k–100k rated cycles, wear is a non-issue.
- NVS cursor writes happen only on sector transitions, not per record.
