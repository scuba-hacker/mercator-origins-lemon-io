# Flash Buffer — Testing and Diagnostics Guide

Companion to `flash-feature.md` (design) and `Power Loss and Recovery.md`
(power-loss behaviour). Everything here describes the current implementation.

## Safety rules (enforced in code)

- Boot-time checks (recovery scan, POST) are **read-only**.
- Destructive tests (`STRESS`, `RECOVERY`, the self-test write cycle,
  `WEAR_TEST`) **refuse to run unless the flash ring is empty**. A stored
  backlog is never used as test material. Use `R` (factory reset) first on a
  bench device whose data you don't need.
- Stored records are CRC-checked on every read regardless of any diagnostic.

## Command access

Two equivalent paths into the same handlers:

- **USB serial** (cable): single-key commands, no Enter needed.
- **WebSerial** (`http://[lemon-ip]/logs`): the same single-key commands plus
  multi-word diagnostic commands. NetworkManager forwards single characters to
  `processSerialCommand()` and everything else to `processExtendedCommand()`
  (both in `src/main_part2.cpp`); `%`-prefixed strings are relayed to Mako.

## Single-key commands

| Key | Action |
| --- | --- |
| `S` | System status: mode, pipeline length, flash records/space, uplink routing state, write/read/fallback/migration counters |
| `F` | Show whether `USE_FLASH_TELEMETRY` was compiled in |
| `R` | Factory reset the flash buffer (erase partition + NVS cursor) |
| `D` | Disconnect WiFi and block reconnect (simulate going offline) |
| `C` | Re-enable WiFi (simulate coverage returning) |
| `H` / `?` | Help |

## Diagnostic commands (any build)

| Command | What it does | Duration | Writes? |
| --- | --- | --- | --- |
| `POST` | Header scan of all 2560 sectors, state-consistency check, report | < 1 s | No |
| `DEEP` | Full 10MB scan: every record of every in-use sector CRC-verified, close markers and sentinel fill checked | seconds | No |
| `STRESS` | Write/read/verify load test (default 100 records, mixed sizes 16–1008B) | seconds | Yes — empty ring only |
| `RECOVERY` | Writes a known batch, injects a torn append, re-initializes, verifies all records survive | seconds | Yes — empty ring only |

## Failure injection (TESTING_MODE builds only)

Compile with `-D TESTING_MODE` to enable. Each command targets one recovery
mechanism; the listed check is what a following restart (or `POST`) must show.

| Command | Injects | Recovery expectation |
| --- | --- | --- |
| `CORRUPT_SECTOR [n]` | Destroys sector n's magic | Header CRC invalid → sector excluded from the ring; its records are lost, everything else intact |
| `CORRUPT_CRC [n]` | Destroys sector n's header CRC | Same as above |
| `SIMULATE_POWER_LOSS` | Torn record (bad CRC, partial payload) at the head append point | Boot rescue: valid records copied out, sector rebuilt, only the torn record gone |
| `CORRUPT_STATE` | Garbage in the NVS cursor blob | CRC check rejects it; cursor falls back to start of oldest sector (re-uploads, no loss) |
| `CORRUPT_POINTERS` | CRC-valid but absurd NVS cursor | seq validation rejects it; same fallback |
| `WEAR_TEST [cycles]` | Fills and drains whole sectors repeatedly | Completes without errors; program-op counter advances (empty ring only) |
| `RANDOM_CORRUPT [n]` | Random header corruption across n sectors | `POST`/`DEEP` report each; unaffected sectors still readable |
| `PARTITION_FAIL` | Disables partition access until restart | All flash ops fail gracefully; PSRAM fallback keeps telemetry flowing |
| `ENABLE_FAIL_INJECT` | Prints injection availability | — |

## Bench validation plan

Run once against real hardware before relying on the system at sea.
Prerequisites: serial cable attached, bench WiFi with MQTT broker reachable,
`R` to start from an empty ring.

1. **Baseline online** — power on with WiFi up. Expect: mode FLASH, records
   upload within seconds, and after the broker connects `S` shows flash
   records 0 and flash writes stop increasing (online bypass working; only
   the pre-connection seconds touch flash).
2. **Engine checks** — `POST`, `DEEP` (both pass, no writes), then `STRESS`
   and `RECOVERY` on the empty ring.
3. **Offline capture** — `D`, let it log 10+ minutes. Expect: flush messages
   every ~15 s, `S` shows flash records climbing, PSRAM length ~0.
4. **Power-cycle persistence** — cut power mid-capture (no shutdown call).
   Repower with WiFi still blocked. Expect: boot log reports the recovered
   record count (≤15 s of records lost at most, possibly a torn-append rescue
   message), count keeps climbing.
5. **Spool** — `C`. Expect: backlog uploads oldest-first while new records
   keep arriving; `S` shows records falling to 0; commits then return to the
   PSRAM path (flash writes stop).
6. **Interrupted spool** — repeat 3–5 but `D` again mid-spool. Expect: no
   gap and no duplicate loss around the interruption; the in-flight record is
   re-sent after `C` (delete-on-ack).
7. **Safe shutdown** — call `prepareForShutdown()` (or power off after seeing
   the `SAFE TO POWER OFF` log during teardown), repower, confirm zero loss.
8. **Two-dive rehearsal** — the full scenario from `flash-feature.md`:
   offline capture → power off → offline capture → power off → power on
   online → complete backlog arrives at the broker in order.

What to watch in the MQTT data afterwards: continuous timestamps across every
power cycle, no records with garbled mako/lemon split (would indicate a meta
field problem), duplicates only immediately after an interrupted spool or
power loss (harmless, expected).
