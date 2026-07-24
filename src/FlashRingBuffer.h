/**
 * @file FlashRingBuffer.h
 * @brief Power-safe, append-only flash ring buffer for persistent telemetry storage
 *
 * Stores variable-length telemetry records (16-1008 bytes) in a dedicated 10MB
 * raw flash partition ("flashbuf", data/0x40) arranged as a ring of 4KB sectors.
 *
 * Core rules the implementation follows (NOR flash constraints):
 *  - A sector is erased exactly once per ring cycle, immediately before it is
 *    opened as the new head sector. Sectors holding committed data are NEVER
 *    erased or rewritten, except to reclaim a fully-consumed tail sector.
 *  - All flash programming only clears bits (1 -> 0) into previously erased
 *    (0xFF) space. Nothing is ever overwritten.
 *  - Records are accumulated in a RAM assembly buffer and appended to the open
 *    head sector in chunks, so the write count per sector stays low.
 *
 * On-flash layout:
 *
 *   Sector (4096 bytes):
 *     [SectorHeader 24B][record slots ...][0x55AA sentinel fill when closed]
 *
 *   SectorHeader:
 *     magic/seq/hdr_crc are written ONCE when the sector is opened (12 bytes).
 *     closed_used/closed_count/closed_crc stay 0xFF while the sector is open
 *     and are programmed in a single 8-byte write when the sector is closed.
 *     The seq number is stamped once and never rewritten, so boot recovery can
 *     order sectors reliably (head = highest seq, tail = lowest seq).
 *
 *   Record slot (4-byte aligned):
 *     [len u16][crc16 u16][meta u16][rsvd u16][payload ...][pad to 4B]
 *     crc16 (CCITT) covers meta + payload. meta carries caller metadata
 *     (the BlockHeader roundedUpPayloadSize needed to split mako/lemon data).
 *
 * Consumption (MQTT spooling) is cursor-based:
 *  - peekOldestRecord() returns the oldest record WITHOUT removing it. Repeated
 *    peeks return the same record.
 *  - consumeOldestRecord() advances the cursor - call it only after the MQTT
 *    broker has acknowledged the upload. A failed publish loses nothing.
 *  - When the cursor passes the last record of a closed sector, that sector is
 *    erased (reclaimed) and the cursor moves to the next in-use sector.
 *  - The cursor can chase the open head sector while new data is still being
 *    appended to it: simultaneous spool-to-MQTT and persist-new-data.
 *
 * Power-loss recovery (all read-only unless a torn write is found):
 *  - Boot scans all sector headers (24B each), orders them by seq.
 *  - Closed sectors are trusted via closed_crc; records are still CRC-checked
 *    individually when read.
 *  - The open head sector is scanned record-by-record. If programmed bytes are
 *    found after the last valid record (a torn append), the valid records are
 *    rescued to RAM, the sector is erased and rebuilt - only the torn final
 *    record is lost.
 *  - The tail cursor is refined from NVS if it matches the scanned tail sector;
 *    otherwise it falls back to the start of the oldest sector (worst case:
 *    some already-uploaded records are re-uploaded, never lost).
 *
 * =========================================================================
 * STATE MODEL
 * =========================================================================
 * The ring is always in exactly one of these states:
 *
 *  UNINITIALIZED      m_initialized == false
 *  TRUSTED            m_initialized && !m_fatal && !m_writes_disabled
 *  SHUTDOWN_LOCKED    m_initialized && !m_fatal && m_writes_disabled
 *  FATAL              m_initialized && m_fatal
 *
 * (Recovery-in-progress is transient inside init()/POST and never observable
 * by callers: scanAndRecover() builds a complete candidate state and commits
 * it atomically - see RecoveredState.)
 *
 * Operation admission by state:
 *
 *  operation             TRUSTED  SHUTDOWN_LOCKED  FATAL  UNINITIALIZED
 *  appendRecord            yes         no            no        no
 *  flush                   yes         yes*          no        no
 *  peek/consume            yes         yes           no        no
 *  peek/consume assembly   yes         yes           yes       no
 *  POST / DEEP             yes         yes           yes**     no
 *  repairCorruption        yes         yes           yes       no
 *  factoryReset/clearAll   yes         yes           yes       no
 *  prepareForShutdown      yes         yes (noop)    no***     no
 *  teardown                yes         yes           yes****   yes
 *
 *  *    flush of an empty assembly is a no-op success; with content it obeys
 *       the same admission as the write path that filled it.
 *  **   POST on a FATAL ring always takes the full repair+verify path; a
 *       fully verified repair is the only runtime transition FATAL->TRUSTED
 *       (besides factoryReset and re-init).
 *  ***  fails: a fatal ring cannot verify persistence.
 *  **** persists nothing (untrusted cursor is never written to NVS).
 *
 * FATAL is entered ONLY through enterFatalState(), the single central
 * transition for: a critical POST exit (partition missing/unreadable), a
 * failed recovery that modified flash or found structural corruption,
 * unresolved committed-journal disposition, a partition that disappears at
 * runtime, and the PARTITION_FAIL injection. It invalidates the peek and
 * tail-cache state; FlashTelemetryManager observes isFatal() and routes
 * telemetry to PSRAM (salvaging the RAM assembly first - no record accepted
 * as "persisted" is ever silently stranded).
 *
 * Thread safety: NOT thread-safe. Call from a single task.
 * RAM usage: two 4KB buffers (assembly + tail read cache) + ~400B state.
 */

#ifndef FLASH_RING_BUFFER_H
#define FLASH_RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <Preferences.h>

extern "C" {
#include "esp_partition.h"
#include "esp_flash.h"
}

/**
 * @brief Sector header - 24 bytes at the start of every 4KB sector.
 *
 * Written in two stages:
 *  - open:  magic, seq, hdr_crc (12 bytes, one write into erased flash)
 *  - close: closed_used, closed_count, closed_crc (8 bytes at offset 12,
 *           one write into bytes that were left erased at open time)
 */
struct SectorHeader {
    uint32_t magic;         // 0x42474F4C "LOGB"
    uint32_t seq;           // monotonic sector sequence, stamped once at open
    uint32_t hdr_crc;       // CRC32 over magic+seq
    uint16_t closed_used;   // record bytes in sector; 0xFFFF while sector open
    uint16_t closed_count;  // records in sector;      0xFFFF while sector open
    uint32_t closed_crc;    // CRC32 over closed_used+closed_count; 0xFFFFFFFF while open
    uint32_t reserved;      // erased (0xFFFFFFFF)
} __attribute__((packed));

/**
 * @brief Record slot header - 8 bytes preceding each payload.
 * crc16 (CCITT) covers the meta field followed by the payload bytes.
 */
struct RecordHeader {
    uint16_t len;           // payload length (16-1008)
    uint16_t crc16;         // CRC16-CCITT over meta (2 bytes LE) + payload
    uint16_t meta;          // caller metadata (BlockHeader roundedUpPayloadSize)
    uint16_t reserved;      // written as 0
} __attribute__((packed));

/**
 * @brief Header of the repair-journal sector (the last physical sector,
 * excluded from the ring). Used to make torn-head repair a durable
 * copy-on-write transaction:
 *
 *   1. journal erased, rescued record bytes written at JOURNAL_HEADER_SIZE
 *   2. this header written LAST (= the commit point)
 *   3. damaged ring sector erased and rebuilt from the journal
 *   4. journal erased (transaction complete)
 *
 * Boot recovery replays any committed journal before scanning, so a power cut
 * at any point during repair is recoverable and the replay is idempotent.
 */
struct JournalHeader {
    uint32_t magic;         // 0x4C4E524A "JRNL"
    uint32_t target_sector; // ring sector being rebuilt
    uint32_t seq;           // that sector's original seq (restamped verbatim)
    uint32_t data_len;      // rescued record bytes stored after this header
    uint32_t data_crc;      // CRC32 over the rescued record bytes
    uint32_t hdr_crc;       // CRC32 over the 20 bytes above
} __attribute__((packed));
// Exactly the SectorHeader size, so the journal's data area always holds the
// largest possible rescued extent (SECTOR_SIZE - SECTOR_HEADER_SIZE) and a
// rescue never has to drop a valid trailing record to fit.
static_assert(sizeof(JournalHeader) == sizeof(SectorHeader),
              "journal data area must cover the full rescued record extent");

/**
 * @brief State persisted in ESP32 NVS ("flashring"/"state2").
 *
 * Only the tail cursor and counters live here - the sector structure itself is
 * always rebuilt from the flash scan at boot, which is authoritative. The NVS
 * tail cursor is applied only if it matches the scanned tail sector (validated
 * by seq), so a stale NVS entry can only cause harmless re-uploads.
 */
struct FlashRingPersistedState {
    uint32_t version;        // structure version (2)
    uint32_t tail_sector;    // sector index of the read cursor
    uint32_t tail_seq;       // seq of that sector when saved (validity check)
    uint32_t tail_offset;    // byte offset of next unconsumed record
    uint32_t tail_consumed;  // records already consumed in the tail sector
    uint32_t next_seq;       // next sector sequence number to stamp
    uint32_t write_count;    // lifetime sector program operations (wear stat)
    uint32_t crc;            // CRC32 of the fields above
} __attribute__((packed));

/**
 * @brief Results of a diagnostic scan (POST / deep validation).
 */
struct FlashDiagnostics {
    bool     partition_found;
    bool     partition_accessible;
    uint32_t total_sectors;
    uint32_t in_use_sectors;        // sectors with a valid header
    uint32_t closed_sectors;        // in-use sectors with a valid close marker
    uint32_t open_sectors;          // in-use sectors without close marker (head)
    uint32_t erased_sectors;        // sectors with no valid header
    uint32_t total_records;
    uint32_t corrupted_records;     // records failing CRC/length checks
    uint32_t torn_head_repairs;     // head sector rescues performed
    bool     nvs_state_applied;     // tail cursor restored from NVS
    uint32_t diagnostic_duration_ms;
};

class FlashRingBuffer {
public:
    // === Flash geometry ===
    static const uint32_t SECTOR_SIZE        = 4096;
    static const uint32_t RING_BUFFER_SIZE   = 10 * 1024 * 1024;            // 10MB partition
    static const uint32_t TOTAL_SECTORS      = RING_BUFFER_SIZE / SECTOR_SIZE; // 2560
    // Last physical sector is the repair journal; the ring uses the rest.
    static const uint32_t RING_SECTORS       = TOTAL_SECTORS - 1;           // 2559
    static const uint32_t JOURNAL_SECTOR     = TOTAL_SECTORS - 1;
    static const uint32_t SECTOR_HEADER_SIZE = sizeof(SectorHeader);        // 24
    static const uint32_t RECORD_HEADER_SIZE = sizeof(RecordHeader);        // 8
    static const uint32_t JOURNAL_HEADER_SIZE = sizeof(JournalHeader);      // 24
    static const uint32_t SECTOR_MAGIC       = 0x42474F4C;                  // "LOGB"
    static const uint32_t JOURNAL_MAGIC      = 0x4C4E524A;                  // "JRNL"
    static const uint32_t USABLE_SECTOR_SIZE = SECTOR_SIZE - SECTOR_HEADER_SIZE; // 4072

    // === Message size constraints ===
    static const uint16_t MIN_MESSAGE_SIZE = 16;
    static const uint16_t MAX_MESSAGE_SIZE = 1008;   // 4 max-size records still fit one sector
    static const uint32_t MIN_SLOT_SIZE    = ((RECORD_HEADER_SIZE + MIN_MESSAGE_SIZE + 3) & ~3u);

    // Flush the assembly buffer if its oldest record has waited this long.
    // Bounds RAM data loss on an unplanned power cut to ~15 seconds.
    static const uint32_t DEFAULT_MAX_UNFLUSHED_MS = 15000;

private:
    // === Hardware / persistence handles ===
    const esp_partition_t* m_partition;
    Preferences            m_preferences;
    bool                   m_initialized;
    bool                   m_writes_disabled;   // set by prepareForShutdown()
    // Set when a runtime recovery fails (e.g. POST rescan or journal clear):
    // the RAM bookkeeping can no longer be trusted to describe the partition,
    // so every read/write entry point refuses until a successful repair,
    // factory reset, or reboot. FlashTelemetryManager routes to PSRAM.
    bool                   m_fatal;
    long unsigned int    (*m_fn_millis)(void);

    // === Head (write) state ===
    uint32_t m_head_sector;        // sector currently (or most recently) open
    uint32_t m_head_seq;           // seq stamped on that sector
    bool     m_head_open;          // header written, close marker not yet written
    uint32_t m_head_write_offset;  // next append offset within head sector
    uint32_t m_head_used;          // record bytes flushed into head sector
    uint32_t m_head_record_count;  // records flushed into head sector
    bool     m_ring_virgin;        // nothing ever written (open in place, no advance)

    // === Tail (read) cursor ===
    uint32_t m_tail_sector;
    uint32_t m_tail_offset;        // offset of next unconsumed record
    uint32_t m_tail_consumed;      // records consumed within the tail sector
    uint32_t m_tail_seq;           // seq of the tail sector (for NVS validation)

    // === Counters ===
    uint32_t m_record_count;       // flushed, unconsumed records in flash
    uint32_t m_next_seq;
    uint32_t m_write_count;        // lifetime program operations (wear stat)

    // === RAM assembly buffer (records waiting to be flushed) ===
    uint8_t* m_assembly;           // 4KB
    uint32_t m_assembly_used;
    uint32_t m_assembly_count;
    uint32_t m_first_unflushed_ms; // millis() of oldest unflushed record, 0 = none
    uint32_t m_max_unflushed_ms;

    // === Tail read cache ===
    uint8_t* m_tail_cache;         // 4KB copy of the tail sector
    uint32_t m_tail_cache_sector;
    uint32_t m_tail_cache_extent;  // bytes of the cached sector known valid
    bool     m_tail_cache_valid;

    // === Peek state (stable until consumeOldestRecord) ===
    bool     m_peek_valid;
    uint16_t m_peek_len;
    uint16_t m_peek_meta;
    uint32_t m_peek_offset;        // offset of the peeked record in tail sector

    // === In-use sector bitmap (1 bit per sector, 320 bytes) ===
    uint8_t  m_sector_map[TOTAL_SECTORS / 8];

    // === Known-erased bitmap (RAM only, 320 bytes) ===
    // Set when a sector is erased this boot, cleared by any program op into
    // it. Lets openNextHeadSector() skip re-erasing a sector that
    // advanceTailSector() already reclaimed (one erase per reuse cycle).
    // Starts all-clear at boot, so the first cycle erases conservatively.
    uint8_t  m_erased_map[TOTAL_SECTORS / 8];

    // === Low-level flash helpers ===
    bool      findPartition();
    esp_err_t eraseSector(uint32_t sector_index);
    esp_err_t writeBytes(uint32_t offset, const void* src, size_t len);
    esp_err_t readBytes(uint32_t offset, void* dst, size_t len) const;

    // === CRC helpers ===
    uint16_t crc16Update(uint16_t crc, const uint8_t* data, size_t length) const;
    uint16_t recordCRC(uint16_t meta, const uint8_t* payload, uint16_t length) const;
    uint32_t calculateCRC32(const uint8_t* data, size_t length) const;

    // === Sector map helpers ===
    void mapSet(uint32_t sector, bool in_use);
    bool mapGet(uint32_t sector) const;
    uint32_t mapCountInUse() const;
    // next in-use sector strictly after 'from' (wraps); returns RING_SECTORS if none
    uint32_t mapNextInUse(uint32_t from) const;
    void mapSetErased(uint32_t sector, bool erased);
    bool mapGetErased(uint32_t sector) const;

    // POST/repair helper: counts ring sectors whose header is unreadable or
    // invalid-but-nonblank (their records are unreadable and excluded from
    // the ring). With deep_blank_check the full sector body is read, so a
    // sector with an erased header but programmed body is also counted.
    // Returns false when the requested verification level could NOT be
    // completed (e.g. buffer allocation failure) - the count is then not a
    // proof and the caller must treat verification as failed.
    bool countNonblankInvalidHeaderSectors(bool deep_blank_check,
                                           uint32_t& anomalies) const;

    // Recheck of the structural conditions in POST's initial health result:
    // head/tail bounds, offsets, and RAM/flash agreement on the open sector
    // (if any open sector exists it must be exactly the RAM head, and
    // m_head_open must match).
    bool verifyStructuralState() const;

    // THE single transition into the FATAL state (see the state model at the
    // top of this file). Invalidates peek and tail-cache state.
    void enterFatalState(const char* context);

    // === Header helpers ===
    bool readSectorHeader(uint32_t sector, SectorHeader& header) const;
    bool headerValid(const SectorHeader& header) const;      // magic + hdr_crc
    bool headerClosed(const SectorHeader& header) const;     // close marker valid
    static uint32_t alignSlot(uint32_t bytes) { return (bytes + 3u) & ~3u; }

    // === Head management ===
    bool openNextHeadSector();     // erase + stamp header on the next free sector
    bool closeHeadSector();        // sentinel-fill + program close marker
    bool dropOldestSector();       // ring full: sacrifice the tail sector

    // === Tail management ===
    bool loadTailCache();          // read tail sector into cache, derive extent
    // scan records in a cached sector image; returns end offset of last valid record
    uint32_t scanRecordExtent(const uint8_t* sector_image, uint32_t limit,
                              uint32_t* record_count_out) const;
    bool advanceTailSector();      // reclaim (erase) tail sector, move to next
    uint32_t tailSectorDataEnd() const; // valid data end offset of tail sector

    // === Recovery (transactional) ===
    /**
     * Complete candidate runtime state built by a recovery scan. The live
     * members are replaced only by applyRecoveredState() after the whole scan
     * and every verification succeeded - a scan that fails halfway can never
     * leave partial or stale bookkeeping live.
     */
    struct RecoveredState {
        uint8_t  sector_map[TOTAL_SECTORS / 8];
        uint32_t head_sector;
        uint32_t head_seq;
        bool     head_open;
        uint32_t head_write_offset;
        uint32_t head_used;
        uint32_t head_record_count;
        bool     ring_virgin;
        uint32_t tail_sector;
        uint32_t tail_seq;
        uint32_t tail_offset;
        uint32_t tail_consumed;
        uint32_t record_count;
        uint32_t next_seq;
    };
    // one canonical empty-ring state (preserves seq monotonicity)
    void canonicalEmptyState(RecoveredState& s) const;
    // atomically publish a verified candidate as the live state
    void applyRecoveredState(const RecoveredState& s);
    /**
     * Build a complete candidate state from flash. Returns false on failure;
     * out-params report whether flash was modified (journal replay / torn
     * rescue) and whether the previous live state may be kept (only when
     * flash was NOT modified, journal disposition is resolved, and no
     * structural corruption was found - otherwise the caller must go FATAL
     * at runtime / fail init at boot).
     */
    bool buildRecoveredState(RecoveredState& out, FlashDiagnostics* diag,
                             bool& flash_modified, bool& keep_old_state_ok);
    bool scanAndRecover(FlashDiagnostics* diag, bool flash_already_modified = false);
    bool rescueTornHeadSector(uint32_t sector, uint32_t valid_end,
                              uint32_t valid_count);
    // journal transaction pieces (see JournalHeader)
    bool writeRepairJournal(uint32_t target_sector, uint32_t seq,
                            const uint8_t* records, uint32_t data_len);
    bool rebuildSectorFromRecords(uint32_t target_sector, uint32_t seq,
                                  const uint8_t* records, uint32_t data_len);
    // Resolves any committed journal before scanning: replay-and-clear, or
    // discard-and-clear. Returns false whenever a committed journal's
    // disposition could not be fully resolved (INCLUDING a failed erase in a
    // discard branch) - normal operation must then not resume.
    bool replayRepairJournal(bool& flash_modified);

    // Destructive half of POST repair. The caller must pass the resulting
    // flash_modified flag into scanAndRecover(), so a failed recovery can
    // never retain bookkeeping from before an erase.
    bool eraseCorruptSectors(bool& flash_modified);

    // === Persistent state ===
    bool loadPersistedState(FlashRingPersistedState& out) const;
    bool savePersistedState();

    void invalidatePeek() { m_peek_valid = false; }
    void resetRuntimeState();

public:
    FlashRingBuffer();
    ~FlashRingBuffer();

    /**
     * @brief Initialize: find partition, run read-only recovery scan, restore cursor.
     * Writes to flash ONLY if a torn head sector must be rescued.
     */
    bool init(long unsigned int (*fn_millis)(void));
    void teardown();
    bool isInitialized() const { return m_initialized; }
    /** @brief True after a failed runtime recovery: bookkeeping is untrusted,
     *  all data operations refuse, and the manager must route to PSRAM.
     *  Cleared by a fully successful POST auto-repair, factory reset, or
     *  reinitialization. */
    bool isFatal() const { return m_fatal; }

    // === Write path ===
    /**
     * @brief Queue a record for storage. Data lands in the RAM assembly buffer
     * and is flushed to flash when the buffer fills the remaining space of the
     * head sector, when it has been waiting MAX_UNFLUSHED_MS, or on flush().
     * @param meta caller metadata stored with the record (u16)
     */
    bool appendRecord(const uint8_t* payload, uint16_t length, uint16_t meta = 0);

    /** @brief Force the assembly buffer into flash now. */
    bool flush();

    // === Read path (peek / consume) ===
    /**
     * @brief Copy the oldest flushed record without removing it. Stable across
     * repeated calls until consumeOldestRecord(). Returns false when no flushed
     * record is available (records still in the assembly buffer are not served
     * - call flush() first if they are needed immediately).
     */
    bool peekOldestRecord(uint8_t* payload, uint16_t max_length,
                          uint16_t& actual_length, uint16_t& meta);

    /** @brief Remove the record returned by the last peek. Call after MQTT ack. */
    bool consumeOldestRecord();

    /** @brief Copy the oldest RAM assembly record without removing it. */
    bool peekOldestAssemblyRecord(uint8_t* payload, uint16_t max_length,
                                  uint16_t& length, uint16_t& meta) const;

    /** @brief Remove the record returned by peekOldestAssemblyRecord(). */
    bool consumeOldestAssemblyRecord();

    /** @brief Compatibility wrapper that peeks then consumes atomically from
     *  the caller's perspective. New transfer code should use the two-phase
     *  API so source removal happens only after the destination commits. */
    bool drainOldestAssemblyRecord(uint8_t* payload, uint16_t max_length,
                                   uint16_t& length, uint16_t& meta);

    // === Status (all O(1) except getUsedSpace which counts the bitmap) ===
    uint32_t getRecordCount() const { return m_record_count + m_assembly_count; }
    uint32_t getFlushedRecordCount() const { return m_record_count; }
    uint32_t getAssemblyRecordCount() const { return m_assembly_count; }
    uint32_t getUsedSpace() const;
    uint32_t getFreeSpace() const;
    bool     isEmpty() const { return getRecordCount() == 0; }
    bool     isFull() const;
    uint32_t getWriteCount() const { return m_write_count; }
    void     setMaxUnflushedMs(uint32_t ms) { m_max_unflushed_ms = ms; }

    // === Debug and diagnostics ===
    void printStatus() const;

    /**
     * @brief Functional self-test (write/read/consume across record sizes).
     * DESTRUCTIVE ONLY WHEN THE RING IS EMPTY - refuses the write test and
     * falls back to a read-only structure check if any data is stored.
     */
    bool performSelfTest();

    /**
     * @brief Structural health scan of every ring sector header plus a state
     * consistency check. The scan itself is read-only; with auto_repair it
     * MAY write on anomalies: erasing garbage-header sectors
     * (repairCorruption), a torn-head journal rescue, and the NVS cursor
     * save inside the recovery rescan. A failed rescan marks the ring fatal
     * (see isFatal()); a fully verified repair clears an existing fatal state.
     */
    bool performPowerOnSelfTest(bool auto_repair = true);

    /** @brief Full 10MB scan: every record of every in-use sector CRC-checked. */
    bool performExtendedDiagnostics();
    bool performDeepSectorValidation();

    /** @brief Simulated power-loss + recovery cycle. Requires an empty ring. */
    bool performPowerLossRecoveryTest();

    /** @brief Write/read/verify load test. Requires an empty ring. */
    bool performStressTest(uint32_t num_records = 1000);

    // === Shutdown ===
    /** @brief Flush assembly buffer, save cursor, block further writes.
     *  Returns true only when both persistence steps verified successful -
     *  power may be cut safely ONLY then. On failure writes stay enabled so
     *  the operation can be retried. */
    bool prepareForShutdown();
    bool emergencyFlush();

    // === Reset and repair ===
    bool factoryReset();     // erase everything + clear NVS + reinit
    bool clearAllData();     // erase all sectors (keeps NVS namespace)
    bool repairCorruption(); // full repair + recovery + verification transaction

    // === Failure injection (TESTING_MODE builds only) ===
    #ifdef TESTING_MODE
    bool injectSectorCorruption(uint32_t sector_index);
    bool corruptPersistedState();
    bool simulateIncompleteWrite();
    bool corruptRingPointers();
    bool acceleratedWearTest(uint32_t cycles);
    bool injectRandomCorruption(uint32_t num_sectors);
    bool simulatePartitionFailure();
    bool injectCRCCorruption(uint32_t sector_index);
    void enableFailureInjection();

    // --- Deterministic fault seams ---------------------------------------
    // All zero/false = inert (and the struct does not exist outside
    // TESTING_MODE builds). A countdown of N makes the N-th subsequent
    // guarded operation fail once; the seam then re-arms to inert.
    struct FaultSeams {
        uint32_t fail_read_countdown;          // partition reads
        uint32_t fail_program_countdown;       // partition program ops
        uint32_t fail_erase_countdown;         // ring-sector erases
        uint32_t fail_journal_erase_countdown; // erases of JOURNAL_SECTOR
        uint32_t fail_nvs_save_countdown;      // savePersistedState()
        bool     fail_diag_alloc;              // diagnostic buffer allocation
        bool     fail_recovery_before_sector_scan; // after journal/NVS, before headers
    };
    FaultSeams& faultSeams() { return m_seams; }
    void clearFaultSeams() { memset(&m_seams, 0, sizeof(m_seams)); }

    /**
     * @brief Deterministic review-test matrix (sol-code-review-5.md): runs
     * the ring-level cases with explicit post-state assertions, printing
     * PASS/FAIL per case. Requires an empty ring and leaves it reset.
     */
    bool runReviewTestMatrix();

    /** @brief Test hook: enter the fatal state directly (manager-level
     *  salvage tests need a deterministic fatal transition). */
    void testForceFatal() { enterFatalState("test-forced"); }
    bool testRestorePartitionAccess();
    bool testResetForMatrix() { return matrixReset(); }
    #endif

private:
    #ifdef TESTING_MODE
    mutable FaultSeams m_seams;
    static bool seamFire(uint32_t& countdown) {
        if (countdown == 0) return false;
        return --countdown == 0;
    }
    // one review-test case: prints and accumulates the result
    void matrixCase(const char* name, bool passed, bool& all_passed);
    // between-case cleanup: erase the sectors the cases touch (cheap, not the
    // whole partition), clear seams/NVS, reinitialize to a virgin ring
    bool matrixReset();
    #endif
};

#endif // FLASH_RING_BUFFER_H
