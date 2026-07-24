# Flash Buffer — Testing and Diagnostics Guide

Companion to `flash-feature.md` (design) and `Power Loss and Recovery.md`
(power-loss behaviour). Everything here describes the current implementation.

## Safety rules (enforced in code)

- Boot-time checks (recovery scan, POST) never touch stored records. They are
  not strictly write-free: a torn head is repaired through the journal
  transaction, POST auto-repair erases garbage-header sectors (which hold no
  readable records), and a non-empty ring saves its validated NVS cursor.
- Destructive tests (`STRESS`, `RECOVERY`, the self-test write cycle,
  `WEAR_TEST`) **refuse to run unless the flash ring is empty**. A stored
  backlog is never used as test material. Use `R` (factory reset) first on a
  bench device whose data you don't need.
- Corruption injections (`CORRUPT_SECTOR`, `CORRUPT_CRC`, `RANDOM_CORRUPT`)
  **refuse in-use sectors** — they can only target free sectors, so live
  telemetry is never destroyed.
- All WebSerial commands are queued and executed by the **main-loop task**;
  the async web server task never touches the flash classes directly.
- Stored records are CRC-checked on every read regardless of any diagnostic.

## Command access

Build prerequisites for the browser console: `#define USE_WEBSERIAL` in
`src/SerialConfig.h` (commented out in the default build — without it `/logs`
loads but cannot connect) and `writeLogToSerial = true` in `src/main.cpp`
(otherwise command output is suppressed even though the command runs).

Two paths into the same handlers:

- **USB serial** (cable): single-key commands, no Enter needed. Works in every
  build; this is also the recovery path after a WebSerial `D`.
- **WebSerial** (`http://[lemon-ip]/logs`): the same single-key commands plus
  multi-word diagnostic commands. NetworkManager enqueues single characters
  for `processSerialCommand()` and everything else for
  `processExtendedCommand()` (both in `src/main_part2.cpp`, executed on the
  main loop); `%`-prefixed strings are relayed to Mako.

## Single-key commands

| Key | Action |
| --- | --- |
| `S` | System status: mode, pipeline length, flash records/space, uplink routing state, write/read/fallback/migration counters |
| `F` | Show whether `USE_FLASH_TELEMETRY` was compiled in |
| `R` | Factory reset the flash buffer (erase partition + NVS cursor) |
| `X` | Prepare safe shutdown: quiesce telemetry, migrate PSRAM to flash, flush + save cursor + lock writes; prints `SAFE TO POWER OFF` only on verified success (on failure normal capture resumes — retry). After success, new telemetry is dropped until power cycle |
| `D` | Disconnect WiFi and block reconnect (simulate going offline). **Sent over WebSerial this severs the console, and the blocked state persists across reboots — recover with `C` over USB serial** |
| `C` | Re-enable WiFi (simulate coverage returning) |
| `H` / `?` | Help |

## Diagnostic commands (any build)

In a PSRAM build (`USE_FLASH_TELEMETRY` not defined) these report
`NOT AVAILABLE IN PSRAM BUILD` — there is no flash ring to test.

| Command | What it does | Duration | Writes? |
| --- | --- | --- | --- |
| `POST` | Header scan of all 2559 ring sectors (blank vs garbage-header distinguished), state-consistency check, report. After a repair it verifies anomalies are gone (full-sector blank proof) and structure is consistent; a failed rescan marks the ring FATAL (data ops refuse, telemetry routes to PSRAM) and a later verified POST repair clears it | < 1 s (longer when repairing) | Auto-repair may erase garbage-header sectors, journal-repair a torn head, and save NVS |
| `DEEP` | Full scan: every record of every in-use sector CRC-verified, close markers and sentinel fill checked, garbage-header sectors reported | seconds | No |
| `STRESS` | Write/read/verify load test (default 100 records, mixed sizes 16–1008B) | seconds | Yes — empty ring only |
| `RECOVERY` | Writes a known batch, injects a torn append, re-initializes, verifies all records survive | seconds | Yes — empty ring only |

## Failure injection (TESTING_MODE builds only)

Compile with `-D TESTING_MODE` to enable. Each command targets one recovery
mechanism; the listed check is what a following restart (or `POST`) must show.

Commands take an exact token plus at most one strictly numeric argument;
malformed or out-of-range arguments are rejected with a parameter error.

| Command | Injects | Recovery expectation |
| --- | --- | --- |
| `CORRUPT_SECTOR [n]` | Programs garbage over free sector n's magic (**refuses in-use sectors**) | POST/DEEP report the nonblank invalid-header sector; POST auto-repair erases it back to the free pool |
| `CORRUPT_CRC [n]` | Same, targeting the header CRC (**refuses in-use sectors**) | Same as above |
| `SIMULATE_POWER_LOSS` | Torn record (bad CRC, partial payload) at the head append point (rotates to a fresh sector if the injected bytes would cross the 4KB boundary) | Restart: boot journal-rescue — valid records preserved, only the torn record gone |
| `CORRUPT_STATE` | Garbage in the NVS cursor blob | Restart: CRC check rejects it; cursor falls back to start of oldest sector (re-uploads, no loss) |
| `CORRUPT_POINTERS` | CRC-valid but absurd NVS cursor | Restart: seq validation rejects it; same fallback |
| `WEAR_TEST [cycles]` | Fills and drains whole sectors repeatedly (1–10000 cycles) | Completes without errors; program-op counter advances (empty ring only) |
| `RANDOM_CORRUPT [n]` | Header corruption across n **unique free** sectors; reports injected/failed counts and returns success only if all requested succeeded | `POST`/`DEEP` report each; unaffected sectors still readable |
| `PARTITION_FAIL` | Disables partition access until restart/test reset | Enters FATAL immediately: appends refuse, assembly records transfer to PSRAM commit-before-consume, and telemetry keeps flowing via PSRAM while capacity remains; the flash backlog is unreadable until recovery |
| `ENABLE_FAIL_INJECT` | Nothing — status print only; there is no runtime arm/disarm gate | — |
| `TEST_MATRIX` | Runs 12 ring cases + 4 manager cases + MQTT ack-ownership cases, all with explicit post-state assertions | Covers pre-scan recovery failure, repair-then-recovery failure, multiple-open corruption, partition loss between get/commit, no-drop salvage retry, shutdown migration refusal, and delayed same-ID ACK quarantine; requires empty storage and leaves it reset |

`TEST_MATRIX` is implemented and compiles in `USE_FLASH_TELEMETRY` +
`TESTING_MODE`, but has not yet run on hardware. Findings remain pending until
a complete passing serial log is retained.

## Bench validation plan

Run once against real hardware before relying on the system at sea.
Prerequisites: serial cable attached, bench WiFi with MQTT broker reachable,
`R` to start from an empty ring.

1. **Deterministic matrix** — run `TEST_MATRIX` and retain the complete serial
   log. Every manager, ring, and ACK case must print `PASS` and the command
   must end with `ALL PASSED`.
2. **Baseline online** — power on with WiFi up. Expect: mode FLASH, records
   upload within seconds, and after the broker connects `S` shows flash
   records 0 and flash writes stop increasing (online bypass working; only
   the pre-connection seconds touch flash).
3. **Engine checks** — `POST`, `DEEP` (both pass, no writes), then `STRESS`
   and `RECOVERY` on the empty ring.
4. **Offline capture** — `D`, let it log 10+ minutes. Expect: flush messages
   every ~15 s, `S` shows flash records climbing, PSRAM length ~0.
5. **Power-cycle persistence** — cut power mid-capture (no shutdown call).
   Repower with WiFi still blocked. Expect: boot log reports the recovered
   record count (≤15 s of records lost at most, possibly a torn-append rescue
   message), count keeps climbing.
6. **Spool** — `C`. Expect: backlog uploads oldest-first while new records
   keep arriving; `S` shows records falling to 0; commits then return to the
   PSRAM path (flash writes stop).
7. **Interrupted spool** — repeat 4–6 but `D` again mid-spool. Expect: no
   gap and no duplicate loss around the interruption; the in-flight record is
   re-sent after `C` (delete-on-ack).
8. **Safe shutdown** — send `X`, wait for `SAFE TO POWER OFF` (if it reports
   `NOT SAFE`, retry), power off, repower, confirm zero loss.
9. **Post-shutdown capture refusal** — after successful `X`, allow another
   capture interval. Expect an explicit `TELEMETRY CAPTURE REFUSED` log and no
   change to persisted counts; reboot re-arms capture.
10. **Sustained PUBACK loss** — keep TCP connected but withhold PUBACKs across
   repeated sends. Expect bounded waits, forced disconnect plus explicit queue
   clear, no flash-tail consume, no memory growth, and normal drain when ACKs
   resume.
11. **Journal erase failure** — fail journal clear after replay and after a
   corrupt-journal discard. Expect FATAL and append refusal until a later
   verified clear/recovery; never resume writes beside a committed journal.
12. **OTA concurrency** — begin OTA during capture, flash migration, flash
   pull, and PUBACK wait. Expect upload to remain blocked until the main task
   completes preparation, with no cross-task storage access.
13. **Two-dive rehearsal** — the full scenario from `flash-feature.md`:
   offline capture → power off → offline capture → power off → power on
   online → complete backlog arrives at the broker in order.

What to watch in the MQTT data afterwards: continuous timestamps across every
power cycle, no records with garbled mako/lemon split (would indicate a meta
field problem), duplicates only immediately after an interrupted spool or
power loss (harmless, expected).
