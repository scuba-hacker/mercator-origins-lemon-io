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

If the system is online from power-on and stays online, telemetry flows
through the original PSRAM pipeline and flash is left alone — with one
qualification: records committed during the first seconds after power-on,
before MQTT connectivity has been observed, route to flash (the uplink state
starts "unavailable" and is updated after the first upload cycle). Once the
connection is seen and that small backlog drains, flash is not written again.

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

Geometry: 2559 × 4KB ring sectors + 1 reserved repair-journal sector (the last
physical sector). Records are 16–1008 bytes of payload.

### Golden rules (NOR flash discipline)

- **One erase per sector per ring cycle.** A sector reclaimed by the read
  path is marked erased in a RAM bitmap, so re-opening it as the head skips
  the redundant second erase (the bitmap is RAM-only, so the first cycle after
  a power-on erases conservatively). Sectors holding committed data are never
  erased or rewritten; the only erases are opening an un-erased head sector
  and reclaiming a **fully consumed** tail sector after its records were
  acknowledged by MQTT.
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

Every flush failure propagates: `appendRecord()` returns `false` when its
space- or age-triggered flush fails, and `FlashTelemetryManager` then routes
the block to the PSRAM pipeline instead (logged as a PSRAM fallback). Older
assembly records move with a two-phase transfer: peek source, commit
destination, then consume source. A full or failed PSRAM commit leaves the
source intact, and a new capture is explicitly refused rather than evicting
the oldest PSRAM record. A flash fault is never silently reported as a
successful commit.

When a sector can't fit the next record it is **closed** (sentinel fill + the
8-byte close marker) and the next sector is opened lazily on the next flush.

### Read path (spooling) — delete-on-ack

- `peekOldestRecord()` returns the oldest record **without removing it** and is
  stable across repeated calls.
- `consumeOldestRecord()` advances the cursor — called only from
  `tailBlockCommitted()`, i.e. after the MQTT publish succeeded. For QoS 1 on
  the TLS (AsyncMqttClient) path, "succeeded" means the broker's **PUBACK for
  the exact packet ID** was received (bounded wait); local enqueue alone never
  deletes a record. The non-TLS PicoMQTT path waits for PUBACK internally. A
  failed or unacknowledged publish loses nothing and is retried — worst case
  is a harmless duplicate at the broker (at-least-once).
- TLS acknowledgment ownership includes the active AsyncMqttClient. Packet IDs
  from timed-out/disconnected attempts are quarantined for that client, so a
  delayed same-ID callback cannot satisfy a later attempt. Timeout handling
  force-closes the transport and explicitly clears its outgoing queue.
- A fully consumed sector is erased and reclaimed immediately.
- The cursor can chase the open head sector while it is still being appended
  to: capture and upload run simultaneously.
- Ring full → the oldest sector is dropped (continuous capture preferred).

### Power-loss recovery (boot)

1. Replay the repair journal if a previous repair was interrupted (see below).
2. Scan all 2559 ring sector headers (24B each — fast).
3. Closed sectors are trusted via `closed_crc` (records still CRC-checked
   individually on read).
4. The open head sector is scanned record-by-record. Programmed bytes after
   the last valid record = **torn append** → repaired via the journal
   transaction below. Only the torn final record is lost — exactly the
   required behaviour.
5. Tail cursor restored from NVS (`flashring/state2`) if it matches the
   scanned tail sector (validated by seq); otherwise it falls back to the
   start of the oldest sector. Worst case is harmless re-upload, never loss.
6. For a non-empty ring the scan finishes by saving the validated cursor back
   to NVS — so boot checks are read-only for flash data but not strictly
   write-free.

A completely fresh partition results in **zero writes** at boot — the first
sector is opened lazily by the first flush.

### Torn-head repair (journal transaction)

Rebuilding a torn sector is a durable copy-on-write transaction through the
reserved journal sector, so committed records can never be destroyed by a
second power cut during the repair itself:

1. Erase the journal sector; write the rescued record bytes into it.
2. Write the journal header **last** (magic, target sector, seq, length, data
   CRC, header CRC) — this is the commit point.
3. Erase the damaged ring sector and rebuild it (same seq, rescued records).
4. Erase the journal.

Power loss before step 2 leaves the damaged sector untouched (the rescue
simply reruns next boot); power loss after step 2 is completed by the boot
replay, which is idempotent. Two invariants keep the transaction lossless:
the journal header is exactly the sector-header size, so the journal's data
area always fits the largest possible rescued extent (no record is ever
dropped to fit), and a failure to erase the committed journal in step 4 is
fatal — initialization fails and the manager falls back to PSRAM, because
resuming writes ahead of a stale journal would let a later replay destroy
newly appended records.

### Safe power-down

Send the `X` command (or call `FlashTelemetryManager::prepareForShutdown()`).
The manager runs a full system-level shutdown sequence:

1. Quiesce telemetry — new head-block commits and tail pulls are refused from
   this point, so nothing can mutate storage behind the safety report.
2. Migrate any blocks still in the volatile PSRAM pipeline to flash, and
   verify PSRAM is empty.
3. Flush the flash assembly buffer, save the cursor to NVS, lock flash writes.
4. Log `SAFE TO POWER OFF` **only after every step verified successful**.

On any failure it reports `NOT SAFE TO POWER OFF`, resumes normal capture,
and can be retried. If flash is unavailable (init failed, PSRAM-only
fallback), the command always reports NOT SAFE — volatile storage can never
be a safe persistent shutdown. After a successful `X`, new telemetry is
dropped (logged) until the next power cycle. An unplanned power cut without
`X` loses at most ~15s of RAM-buffered records; flash content is never
corrupted (torn appends are rescued at next boot).

The OTA begin callback performs no shutdown work itself. It posts an atomic
request and blocks the upload while the main task persists telemetry, updates
the display, disconnects MQTT/WebSocket/WebSerial, and tears down UART tasks.
There is no timeout path that starts the upload before main-task preparation
has completed.

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

Boot-time checks never touch stored records (the only writes are the torn-head
journal repair and the NVS cursor save). Destructive tests refuse to run
unless the ring is empty — a dive backlog is never used as test material.
WebSerial commands are queued and executed by the main-loop task; nothing
touches the flash classes from the async web server task.

USB serial single-key commands (`src/main_part2.cpp`):

- `S` — status: mode, pipeline length, flash records/space, routing stats
- `F` — show compile-time flash setting
- `R` — factory reset the flash buffer
- `X` — prepare safe shutdown (flush + lock; prints SAFE TO POWER OFF on success)
- `D` / `C` — simulate WiFi loss / return (`D` over WebSerial severs the
  console; recover with `C` via USB)
- `H`/`?` — help

WebSerial commands: `POST` (structural scan; auto-repair may erase
garbage-header sectors and rerun recovery, verifying both anomaly counts and
structural consistency afterwards), `DEEP` (full record-level CRC scan,
read-only), `STRESS`, `RECOVERY` (both require an empty ring).

Recovery is **transactional**: the scan builds a complete candidate state
(including the canonical empty state) and publishes it in one step only after
everything verified — the live state is never partially overwritten by a scan
that fails halfway. Failure policy: at **boot** a failed recovery fails
`init()` and the manager starts in PSRAM fallback. At **runtime** (POST
auto-repair) a scan that fails with flash *unmodified* retains the previous
trusted state wholesale and just reports a failed diagnostic; a failure after
flash was modified, with unresolved journal disposition, or with structural
corruption (e.g. an open sector that is not the head) enters the **fatal**
state through one central transition: bookkeeping is untrusted, every flash
data operation refuses, records still in the RAM assembly are salvaged into
PSRAM, and new telemetry routes to PSRAM. The same transition fires when the
partition disappears (POST critical exits, failed erase/program on a missing
partition). Trust is restored only by a fully verified POST repair, a factory
reset (`R`), or a reboot.

POST's destructive invalid-header cleanup is part of the same transaction:
every successful erase is carried into the recovery decision. If repair,
recovery, or final deep verification then fails, the ring remains fatal; it
cannot retain runtime bookkeeping captured before the erase.

Failure injection (`TESTING_MODE` builds only): corrupt sector headers/CRCs,
torn-append simulation, NVS state corruption, wear testing — see
`flash-testing.md`.

## Capacity and wear

- 2559 ring sectors × 16 records (~244B per stored record: 236B payload + 8B
  header, 4B-aligned) ≈ **41,000 records ≈ 11.4 h at 1Hz** before the ring
  wraps. `getMaximumPipelineLength()` reports a conservative ~38,000 derived
  from the configured 256-byte maximum block payload.
- Per sector cycle: 1 erase + ~3–4 program ops (header, data chunk(s),
  sentinel+close). Reclaim and re-open share a single erase (tracked in the
  erased-sector bitmap). At continuous 1Hz offline logging that is one sector
  per ~17s → each individual sector is erased roughly once every 12 hours of
  continuous offline use. At 10k–100k rated cycles, wear is a non-issue.
- The journal sector is erased/written only during torn-head repairs (rare).
- NVS cursor writes happen only on sector transitions, not per record.
