# Flash Buffer System — 2026 Re-evaluation and Rewrite Record

**July 2026.** The original (2025) content of this file was a superficial
review that missed every real defect. This replaces it with the findings of
the full re-evaluation and records how each was resolved in the rewrite. The
old implementation was never deployed; no field data was ever at risk.

## Critical defects found in the 2025 implementation — all resolved

1. **POST destroyed sector 0 on every boot.** The partition write-test
   programmed 16 bytes at offset 0 — over the live sector-0 header — then
   "auto-repair" erased the sector.
   *Now:* POST is entirely read-only; accessibility is proven by reads.

2. **`deleteOldestRecord()` rewrote a programmed sector without erasing**
   (silent bit-AND corruption), and rewrote the whole 4KB sector once per
   record uploaded.
   *Now:* cursor-based consumption (`peekOldestRecord` / `consumeOldestRecord`);
   a sector is erased once, only after every record in it is MQTT-acknowledged.

3. **Records were deleted from flash before the MQTT publish was confirmed** —
   every failed publish permanently lost a message.
   *Now:* delete-on-ack. The record is consumed only in `tailBlockCommitted()`.
   Failed publishes retry the same record.

4. **Boot self-test wrote test records into a live backlog and read real dive
   data as test data** — with a backlog present it always failed, fell back to
   PSRAM (so the backlog never spooled), and left garbage test records to be
   uploaded as telemetry.
   *Now:* destructive tests refuse to run unless the ring is empty; boot runs
   read-only checks only.

5. **Flushing into a partially used sector erased committed data first**
   (read-merge-erase-rewrite) — power loss in that window destroyed
   already-durable records.
   *Now:* strictly append-only. Flushes program only erased space; committed
   data is never erased or rewritten.

6. **Sector sequence numbers were re-stamped on every flush and could
   duplicate across sectors**, making head/tail recovery ambiguous after power
   loss.
   *Now:* `seq` is stamped once at sector open and never touched again.

7. **A freshly opened head sector always failed "completion" validation**
   (trailing 0xFF read as corruption) and was erased by auto-repair.
   *Now:* open-vs-closed is explicit in the header; open sectors are
   record-scanned, trailing 0xFF is the expected state.

8. **`getRecordCount()` scanned the whole ring (up to 10MB of reads) and was
   called multiple times per second** via `getPipelineLength()`.
   *Now:* counters are cached and O(1); flash is scanned once at boot.

9. **The flash build did not compile.** `FlashTelemetryManager` lacked
   `getMaximumDepth()`, `getMaximumPipelineLength()`, `getHeadBlockIndex()`,
   `getTailBlockIndex()`, and `NetworkManager::setTelemetryPipeline()` only
   accepted `TelemetryPipeline*`.
   *Now:* all four methods exist; NetworkManager takes either type via a
   template setter. Both build configurations compile clean.

10. **`BlockHeader::roundedUpPayloadSize` was silently dropped** through flash
    storage — records read back could never be split into their mako/lemon
    parts for upload.
    *Now:* preserved in the record's `meta` field, covered by the record CRC.

11. **Flash was written even with perfect connectivity**, violating the
    requirement that an always-online session never touches flash.
    *Now:* connectivity-aware routing (`setUplinkAvailable`); online with no
    backlog = pure PSRAM path, zero flash writes. (The first seconds after
    power-on route to flash until the broker connection is confirmed — the
    safe choice while connectivity is unknown; those records spool out
    immediately once online.)

12. **NVS state was saved constantly yet ignored** (recovery always rebuilt
    from a broken scan).
    *Now:* the flash scan is authoritative for structure; NVS stores only the
    tail cursor (validated by seq before use) and is written on sector
    transitions only.

## Remaining suggestions (nice-to-have)

- **HYBRID mode** (PSRAM write cache in front of flash) — enum reserved,
  unimplemented; current routing already covers the operational need.
- **Lantern-triggered automatic `prepareForShutdown()`** — planned by design;
  the manager call is ready.
- **Per-sector erase-count tracking** for wear statistics (current lifetime
  program-op counter is coarse). Wear analysis says this is cosmetic: at 1Hz
  continuous offline logging each sector is erased about twice per day.
- **On-device unit test command** that runs `performSelfTest`,
  `performPowerLossRecoveryTest` and `performStressTest` in sequence on an
  empty ring before a season's first deployment.
