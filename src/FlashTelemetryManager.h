/**
 * @file FlashTelemetryManager.h
 * @brief Telemetry storage manager: flash persistence with PSRAM fallback.
 *
 * Drop-in replacement for TelemetryPipeline (same API plus a few extras).
 * Routes telemetry blocks between the battle-tested PSRAM pipeline and the
 * power-safe FlashRingBuffer according to connectivity:
 *
 *  - Uplink available and no flash backlog:
 *      blocks go through the PSRAM pipeline exactly as before and upload
 *      within the second. FLASH IS NOT WRITTEN AT ALL in this state
 *      (requirement: a session with continuous signal never touches flash).
 *
 *  - Uplink unavailable, or a flash backlog exists:
 *      blocks are persisted to the flash ring buffer (via its RAM assembly
 *      buffer). Any blocks still sitting in the PSRAM pipeline are migrated
 *      to flash first so ordering and persistence are preserved.
 *
 *  - Spooling: pullTailBlock() serves the OLDEST flash record; the record is
 *      deleted from flash only when tailBlockCommitted() confirms the MQTT
 *      upload (delete-on-ack). A failed publish loses nothing. New records
 *      keep appending to the flash head while the tail spools - simultaneous
 *      capture and upload.
 *
 *  - Once the flash backlog fully drains and the uplink is stable, commits
 *      return to the pure PSRAM path automatically.
 *
 * The BlockHeader roundedUpPayloadSize (needed to split the combined
 * mako+lemon payload during upload) is preserved through flash storage in the
 * record's meta field.
 *
 * Storage modes:
 *  - PSRAM_ONLY: original TelemetryPipeline behaviour (fallback / safety)
 *  - FLASH_ONLY: connectivity-aware routing described above
 *  - HYBRID:     reserved for future use (currently behaves as PSRAM_ONLY)
 *
 * Thread safety: NOT thread-safe - call from the main loop task only.
 */

#ifndef FLASH_TELEMETRY_MANAGER_H
#define FLASH_TELEMETRY_MANAGER_H

#include "FlashRingBuffer.h"
#include <TelemetryPipeline.h>

class FlashTelemetryManager {
public:
    enum StorageMode {
        PSRAM_ONLY,      // TelemetryPipeline only (battle-tested fallback)
        FLASH_ONLY,      // flash persistence with connectivity-aware routing
        HYBRID           // reserved (currently == PSRAM_ONLY)
    };

private:
    enum PullSource { SOURCE_NONE, SOURCE_FLASH, SOURCE_PSRAM };

    // === Storage systems ===
    FlashRingBuffer   m_flash_buffer;
    TelemetryPipeline m_psram_pipeline;

    // === Configuration and state ===
    StorageMode m_storage_mode;
    bool        m_initialized;
    bool        m_enable_flash_buffer;
    bool        m_uplink_available;    // set from the MQTT upload path
    bool        m_shutdown_prepared;   // quiesced by prepareForShutdown()
    PullSource  m_last_pull_source;    // routes tailBlockCommitted correctly

    // === Scratch block for serving flash records as BlockHeaders ===
    uint8_t*    m_scratch_buffer;
    uint8_t*    m_fallback_buffer;      // preserves current block during salvage
    uint16_t    m_scratch_size;
    BlockHeader m_scratch_block;
    uint32_t    m_next_flash_payload_id;
    bool        m_flash_pull_pending;  // a flash record is peeked, not yet acked

    // === Statistics ===
    uint32_t m_flash_writes;
    uint32_t m_flash_reads;
    uint32_t m_psram_fallbacks;
    uint32_t m_migrated_blocks;        // PSRAM -> flash migrations
    uint32_t m_max_flash_records;      // high-water mark
    uint32_t m_last_drain_ms;          // last successful tail commit (any source)

    long unsigned int (*m_fn_millis)(void);

    // === Helpers ===
    bool flashActive() const {
        // A fatal ring (failed runtime recovery - bookkeeping untrusted) is
        // treated as absent: commits and pulls route to PSRAM until a
        // verified repair, factory reset, or reboot restores trust.
        return m_storage_mode == FLASH_ONLY && m_flash_buffer.isInitialized() &&
               !m_flash_buffer.isFatal();
    }
    bool commitBlockToFlash(BlockHeader& block);   // payload+meta -> flash record
    bool commitBlockToPsram(BlockHeader& block, bool& pipelineFull);
    bool migratePsramBacklogToFlash();             // preserve ordering going offline
    // Invariant: records accepted into the flash RAM assembly were reported
    // as persisted. Transfer is peek -> destination commit -> source consume,
    // so a full or failed PSRAM commit leaves the source record intact.
    bool salvageAssemblyToPsram();
    bool preserveCurrentAndFallback(BlockHeader& block, bool& pipelineFull);

    #ifdef TESTING_MODE
    struct ManagerFaultSeams {
        uint32_t fail_psram_commit_countdown;
        uint32_t fail_migration_countdown;
    } m_manager_seams;
    #endif

public:
    FlashTelemetryManager();
    ~FlashTelemetryManager();

    // === TelemetryPipeline-compatible lifecycle ===
    bool init(long unsigned int (*fn_millis)(void),
              const uint16_t maxBlockBufferMemoryUsageKB = 2048,
              const uint16_t maxBlockBufferMemoryUsageBytesRemainder = 0);
    void teardown();
    bool isInitialized() const { return m_initialized; }

    // === Configuration ===
    void setStorageMode(StorageMode mode);
    StorageMode getStorageMode() const { return m_storage_mode; }
    void enableFlashBuffer(bool enable);
    bool isFlashBufferEnabled() const { return m_enable_flash_buffer; }

    /**
     * @brief Tell the manager whether the MQTT uplink can currently publish.
     * Call once per upload cycle (e.g. with privateMQTT.canUpload()).
     * Drives the flash-bypass: online with no backlog -> flash untouched.
     */
    void setUplinkAvailable(bool available);
    bool isUplinkAvailable() const { return m_uplink_available; }

    // === TelemetryPipeline-compatible data path ===
    BlockHeader getHeadBlockForPopulating();
    bool commitPopulatedHeadBlock(BlockHeader head, bool& pipelineFull);
    bool pullTailBlock(BlockHeader& header);
    void tailBlockCommitted();

    // === TelemetryPipeline-compatible status ===
    bool pipelineEmpty() const;
    bool pipelineFull() const;
    uint16_t getPipelineLength() const;
    bool isPipelineDraining() const;
    uint16_t getMaximumDepth() const;
    uint16_t getMaximumPipelineLength() const;
    uint16_t getHeadBlockIndex() const;
    uint16_t getTailBlockIndex() const;

    // === Flash-specific status ===
    uint32_t getFlashRecordCount() const;
    uint32_t getFlashUsedSpace() const;
    uint32_t getFlashFreeSpace() const;
    uint32_t getFlashWriteCount() const;

    // === Statistics ===
    uint32_t getTotalFlashWrites() const { return m_flash_writes; }
    uint32_t getTotalFlashReads() const { return m_flash_reads; }
    uint32_t getPsramFallbacks() const { return m_psram_fallbacks; }
    uint32_t getMigratedBlocks() const { return m_migrated_blocks; }

    // === Debug and maintenance ===
    void printStatus() const;
    bool performSelfTest();            // safe: read-only when data is stored
    /** @brief System-level safe shutdown: quiesces telemetry (new commits and
     *  tail pulls are refused from here on), migrates any pending PSRAM blocks
     *  to flash, verifies PSRAM is empty, then flushes and locks the flash
     *  ring. Returns true - and stays quiesced - only when every step verified
     *  successful; on failure normal operation resumes so it can be retried.
     *  Returns false when flash is unavailable (volatile-only operation can
     *  never be a safe persistent shutdown). */
    bool prepareForShutdown();

    // === Diagnostics (available whenever the flash buffer initialized) ===
    bool performPowerOnSelfTest(bool auto_repair = true);
    bool performDeepSectorValidation();
    bool performPowerLossRecoveryTest();
    bool performStressTest(uint32_t num_records = 1000);

    // === Reset and recovery ===
    bool factoryReset();
    bool clearAllFlashData();
    bool repairFlashCorruption();

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

    /** @brief Deterministic review-test matrix: manager-level cases (assembly
     *  salvage to PSRAM when the ring goes fatal, routing after demotion)
     *  followed by the full ring-level matrix. Requires empty pipelines. */
    bool runReviewTestMatrix();
    #endif
};

#endif // FLASH_TELEMETRY_MANAGER_H
