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
 * Thread safety: NOT thread-safe. Call from a single task.
 * RAM usage: two 4KB buffers (assembly + tail read cache) + ~400B state.
 */

#ifndef FLASH_RING_BUFFER_H
#define FLASH_RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>
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
    static const uint32_t SECTOR_HEADER_SIZE = sizeof(SectorHeader);        // 24
    static const uint32_t RECORD_HEADER_SIZE = sizeof(RecordHeader);        // 8
    static const uint32_t SECTOR_MAGIC       = 0x42474F4C;                  // "LOGB"
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
    // next in-use sector strictly after 'from' (wraps); returns TOTAL_SECTORS if none
    uint32_t mapNextInUse(uint32_t from) const;

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

    // === Recovery ===
    bool scanAndRecover(FlashDiagnostics* diag);
    bool rescueTornHeadSector(uint32_t sector, uint32_t valid_end,
                              uint32_t valid_count);

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
     * @brief Read-only structural health scan of every sector header plus the
     * head/tail sectors in detail. Never modifies data. auto_repair currently
     * only logs what repairCorruption() would do.
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
     *  After this returns it is safe to cut power. */
    void prepareForShutdown();
    void emergencyFlush();

    // === Reset and repair ===
    bool factoryReset();     // erase everything + clear NVS + reinit
    bool clearAllData();     // erase all sectors (keeps NVS namespace)
    bool repairCorruption(); // erase sectors with invalid headers outside the ring

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
    #endif
};

#endif // FLASH_RING_BUFFER_H
