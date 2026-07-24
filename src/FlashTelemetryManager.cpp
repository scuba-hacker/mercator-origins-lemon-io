/**
 * @file FlashTelemetryManager.cpp
 * @brief Connectivity-aware routing between the PSRAM pipeline and flash ring.
 *
 * See FlashTelemetryManager.h for the routing rules. The invariants enforced
 * here:
 *  - flash records are removed ONLY from tailBlockCommitted() (delete-on-ack)
 *  - when both stores hold data, the flash backlog (older) drains first
 *  - the roundedUpPayloadSize block metadata survives the flash round-trip
 *  - a session with continuous uplink and no backlog never writes flash
 */

#include "FlashTelemetryManager.h"
#include "SerialConfig.h"
#include <Arduino.h>
#include <cstring>

#ifdef TESTING_MODE
static bool managerSeamFire(uint32_t& countdown) {
    if (countdown == 0) {
        return false;
    }
    countdown--;
    return countdown == 0;
}
#endif

FlashTelemetryManager::FlashTelemetryManager() :
    m_storage_mode(PSRAM_ONLY),
    m_initialized(false),
    m_enable_flash_buffer(false),
    m_uplink_available(false),
    m_shutdown_prepared(false),
    m_last_pull_source(SOURCE_NONE),
    m_scratch_buffer(nullptr),
    m_fallback_buffer(nullptr),
    m_scratch_size(0),
    m_next_flash_payload_id(1),
    m_flash_pull_pending(false),
    m_flash_writes(0),
    m_flash_reads(0),
    m_psram_fallbacks(0),
    m_migrated_blocks(0),
    m_max_flash_records(0),
    m_last_drain_ms(0),
    m_fn_millis(nullptr) {
#ifdef TESTING_MODE
    memset(&m_manager_seams, 0, sizeof(m_manager_seams));
#endif
}

FlashTelemetryManager::~FlashTelemetryManager() {
    teardown();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool FlashTelemetryManager::init(long unsigned int (*fn_millis)(void),
                                 const uint16_t maxBlockBufferMemoryUsageKB,
                                 const uint16_t maxBlockBufferMemoryUsageBytesRemainder) {
    USB_SERIAL_PRINTLN("FlashTelemetryManager::init() - starting");

    if (m_initialized) {
        return true;
    }

    m_fn_millis = fn_millis;

    // PSRAM pipeline first - it is the guaranteed fallback.
    if (!m_psram_pipeline.init(fn_millis, maxBlockBufferMemoryUsageKB,
                               maxBlockBufferMemoryUsageBytesRemainder)) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::init() - PSRAM pipeline init failed");
        return false;
    }

    // Scratch block used to serve flash records through the BlockHeader API.
    // Sized to the configured block payload (s_overrideMaxPayloadSize must have
    // been called by now), with headroom for any stored record.
    uint16_t block_max = BlockHeader::s_getMaxPayloadSize();
    m_scratch_size = (block_max > FlashRingBuffer::MAX_MESSAGE_SIZE)
                         ? block_max : FlashRingBuffer::MAX_MESSAGE_SIZE;
    m_scratch_buffer = (uint8_t*)malloc(m_scratch_size);
    m_fallback_buffer = (uint8_t*)malloc(m_scratch_size);
    if (!m_scratch_buffer || !m_fallback_buffer) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::init() - scratch/fallback allocation failed");
        teardown();
        return false;
    }
    m_scratch_block = BlockHeader(m_scratch_buffer);

    if (m_enable_flash_buffer) {
        if (m_flash_buffer.init(fn_millis)) {
            m_storage_mode = FLASH_ONLY;
            USB_SERIAL_PRINTF("FlashTelemetryManager::init() - flash ready, %u persisted records waiting\n",
                              m_flash_buffer.getRecordCount());
            // Structural check with auto-repair: never touches readable
            // records, but may erase garbage-header sectors, journal-repair a
            // torn head, and save the NVS cursor.
            if (!m_flash_buffer.performPowerOnSelfTest(true)) {
                if (m_flash_buffer.isFatal()) {
                    USB_SERIAL_PRINTLN("FlashTelemetryManager::init() - POST recovery FAILED, flash marked fatal - routing telemetry to PSRAM");
                    m_psram_fallbacks++;
                } else {
                    USB_SERIAL_PRINTLN("FlashTelemetryManager::init() - POST reported issues; continuing (data is CRC-checked on read)");
                }
            }
        } else {
            USB_SERIAL_PRINTLN("FlashTelemetryManager::init() - flash init FAILED, falling back to PSRAM");
            m_storage_mode = PSRAM_ONLY;
            m_psram_fallbacks++;
        }
    } else {
        m_storage_mode = PSRAM_ONLY;
    }

    m_initialized = true;

    USB_SERIAL_PRINTF("FlashTelemetryManager::init() - mode: %s\n",
                      (m_storage_mode == FLASH_ONLY) ? "FLASH (connectivity-aware)" : "PSRAM only");
    printStatus();
    return true;
}

void FlashTelemetryManager::teardown() {
    if (m_initialized) {
        prepareForShutdown();
    }
    m_flash_buffer.teardown();
    m_psram_pipeline.teardown();
    if (m_scratch_buffer) {
        free(m_scratch_buffer);
        m_scratch_buffer = nullptr;
    }
    if (m_fallback_buffer) {
        free(m_fallback_buffer);
        m_fallback_buffer = nullptr;
    }
    m_initialized = false;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void FlashTelemetryManager::setStorageMode(StorageMode mode) {
    USB_SERIAL_PRINTF("FlashTelemetryManager::setStorageMode() - mode %d\n", (int)mode);
    m_storage_mode = mode;
}

void FlashTelemetryManager::enableFlashBuffer(bool enable) {
    USB_SERIAL_PRINTF("FlashTelemetryManager::enableFlashBuffer() - %s\n",
                      enable ? "enable" : "disable");
    m_enable_flash_buffer = enable;
    if (!enable && m_storage_mode != PSRAM_ONLY) {
        m_storage_mode = PSRAM_ONLY;
    }
}

void FlashTelemetryManager::setUplinkAvailable(bool available) {
    if (available != m_uplink_available) {
        USB_SERIAL_PRINTF("FlashTelemetryManager::setUplinkAvailable() - uplink %s (flash backlog: %u records)\n",
                          available ? "AVAILABLE" : "LOST",
                          m_flash_buffer.isInitialized() ? m_flash_buffer.getRecordCount() : 0);
    }
    m_uplink_available = available;
}

// ---------------------------------------------------------------------------
// Data path
// ---------------------------------------------------------------------------

BlockHeader FlashTelemetryManager::getHeadBlockForPopulating() {
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::getHeadBlockForPopulating() - not initialized");
        return BlockHeader();
    }
    // Salvage runs HERE, at cycle start, because the PSRAM head block
    // returned below is the pipeline's own slot buffer: salvaging later
    // (during commit) would overwrite a populated-but-uncommitted block.
    // Ordering is also preserved - salvaged (older) records commit to PSRAM
    // before the caller's new record does.
    if (m_flash_buffer.isFatal() && !salvageAssemblyToPsram()) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::getHeadBlockForPopulating() - assembly salvage pending; new commit may be refused");
    }
    // The PSRAM head block always serves as the population scratch area.
    // In flash routing it is simply never committed to the PSRAM pipeline,
    // so the same block is reused each cycle.
    return m_psram_pipeline.getHeadBlockForPopulating();
}

bool FlashTelemetryManager::commitBlockToFlash(BlockHeader& block) {
    uint16_t payload_size = block.getPayloadSize();
    if (payload_size < FlashRingBuffer::MIN_MESSAGE_SIZE ||
        payload_size > FlashRingBuffer::MAX_MESSAGE_SIZE) {
        return false;
    }
    uint16_t max_payload = 0;
    uint8_t* buffer = block.getBuffer(max_payload);
    if (!buffer) {
        return false;
    }
    uint16_t meta = block.getRoundedUpPayloadSize();
    if (!m_flash_buffer.appendRecord(buffer, payload_size, meta)) {
        return false;
    }
    m_flash_writes++;
    uint32_t count = m_flash_buffer.getRecordCount();
    if (count > m_max_flash_records) {
        m_max_flash_records = count;
    }
    return true;
}

bool FlashTelemetryManager::commitBlockToPsram(BlockHeader& block, bool& pipelineFull) {
    pipelineFull = m_psram_pipeline.pipelineFull();
    if (pipelineFull) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::commitBlockToPsram() - REFUSED: PSRAM full (oldest record retained)");
        return false;
    }
#ifdef TESTING_MODE
    if (managerSeamFire(m_manager_seams.fail_psram_commit_countdown)) {
        USB_SERIAL_PRINTLN("SEAM: injected PSRAM commit refusal");
        return false;
    }
#endif
    bool library_full = false;
    bool committed = m_psram_pipeline.commitPopulatedHeadBlock(block, library_full);
    pipelineFull = library_full || m_psram_pipeline.pipelineFull();
    return committed;
}

bool FlashTelemetryManager::salvageAssemblyToPsram() {
    if (!m_scratch_buffer) {
        return false;
    }

    // Source removal is the commit point: never consume an assembly record
    // until the no-drop PSRAM commit has succeeded.
    uint32_t salvaged = 0;
    uint16_t len = 0, meta = 0;
    while (m_flash_buffer.getAssemblyRecordCount() > 0) {
        if (!m_flash_buffer.peekOldestAssemblyRecord(m_scratch_buffer, m_scratch_size,
                                                     len, meta)) {
            USB_SERIAL_PRINTLN("FlashTelemetryManager::salvageAssemblyToPsram() - assembly record unreadable; source retained");
            return false;
        }
        BlockHeader block = m_psram_pipeline.getHeadBlockForPopulating();
        uint16_t max_payload = 0;
        uint8_t* buffer = block.getBuffer(max_payload);
        if (!buffer || len > max_payload) {
            USB_SERIAL_PRINTF("FlashTelemetryManager::salvageAssemblyToPsram() - PSRAM block cannot take %u bytes; source retained\n",
                              len);
            return false;
        }
        memcpy(buffer, m_scratch_buffer, len);
        block.setPayloadSize(len);
        block.setRoundedUpPayloadSize(meta);
        bool full = false;
        if (!commitBlockToPsram(block, full)) {
            USB_SERIAL_PRINTLN("FlashTelemetryManager::salvageAssemblyToPsram() - PSRAM commit failed; source retained");
            return false;
        }
        if (!m_flash_buffer.consumeOldestAssemblyRecord()) {
            USB_SERIAL_PRINTLN("FlashTelemetryManager::salvageAssemblyToPsram() - source consume failed after destination commit (duplicate retained)");
            return false;
        }
        salvaged++;
    }
    if (salvaged > 0) {
        USB_SERIAL_PRINTF("FlashTelemetryManager::salvageAssemblyToPsram() - %u assembly records committed to PSRAM\n",
                          salvaged);
    }
    return true;
}

bool FlashTelemetryManager::preserveCurrentAndFallback(BlockHeader& block, bool& pipelineFull) {
    if (!m_fallback_buffer) {
        return false;
    }
    uint16_t max_payload = 0;
    uint8_t* source = block.getBuffer(max_payload);
    uint16_t payload_size = block.getPayloadSize();
    if (!source || payload_size == 0 || payload_size > m_scratch_size) {
        return false;
    }

    memcpy(m_fallback_buffer, source, payload_size);
    BlockHeader preserved(m_fallback_buffer);
    preserved.setPayloadSize(payload_size);
    preserved.setRoundedUpPayloadSize(block.getRoundedUpPayloadSize());

    if (!salvageAssemblyToPsram()) {
        pipelineFull = m_psram_pipeline.pipelineFull();
        USB_SERIAL_PRINTLN("FlashTelemetryManager::preserveCurrentAndFallback() - REFUSED: older assembly records not yet safe");
        return false;
    }
    if (!commitBlockToPsram(preserved, pipelineFull)) {
        return false;
    }
    m_psram_fallbacks++;
    return true;
}

bool FlashTelemetryManager::migratePsramBacklogToFlash() {
    // Blocks committed to PSRAM while the uplink was up but not yet uploaded
    // are older than the block being committed now - move them to flash first
    // so ordering and persistence are preserved. Migration's commit point is
    // a successful flash flush, not acceptance into the RAM assembly.
    if (m_flash_buffer.getAssemblyRecordCount() > 0 && !m_flash_buffer.flush()) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::migratePsramBacklogToFlash() - existing assembly could not be made durable");
        return false;
    }

    BlockHeader block;
    while (m_psram_pipeline.pullTailBlock(block)) {
#ifdef TESTING_MODE
        if (managerSeamFire(m_manager_seams.fail_migration_countdown)) {
            USB_SERIAL_PRINTLN("SEAM: injected PSRAM-to-flash migration refusal");
            return false;
        }
#endif
        if (!commitBlockToFlash(block)) {
            // Leave the block in PSRAM rather than lose it.
            USB_SERIAL_PRINTLN("FlashTelemetryManager::migratePsramBacklogToFlash() - migration halted (flash append failed)");
            return false;
        }
        if (!m_flash_buffer.flush()) {
            // The PSRAM tail is still authoritative. Remove the RAM duplicate
            // so fatal fallback cannot append it behind newer PSRAM records.
            if (m_flash_buffer.getAssemblyRecordCount() == 1) {
                m_flash_buffer.consumeOldestAssemblyRecord();
            }
            USB_SERIAL_PRINTLN("FlashTelemetryManager::migratePsramBacklogToFlash() - flash flush failed; PSRAM source retained");
            return false;
        }
        m_psram_pipeline.tailBlockCommitted();
        m_migrated_blocks++;
    }
    return true;
}

bool FlashTelemetryManager::commitPopulatedHeadBlock(BlockHeader head, bool& pipelineFull) {
    if (!m_initialized) {
        return false;
    }
    if (m_shutdown_prepared) {
        // Quiesced: accepting the record into volatile PSRAM would silently
        // invalidate the SAFE TO POWER OFF report already given.
        USB_SERIAL_PRINTLN("FlashTelemetryManager::commitPopulatedHeadBlock() - REFUSED: shutdown prepared, record dropped");
        return false;
    }

    pipelineFull = false;

    if (m_flash_buffer.isFatal()) {
        return preserveCurrentAndFallback(head, pipelineFull);
    }

    bool route_to_flash = flashActive() &&
                          (!m_uplink_available || !m_flash_buffer.isEmpty());

    if (route_to_flash) {
        if (!migratePsramBacklogToFlash()) {
            if (m_flash_buffer.isFatal()) {
                return preserveCurrentAndFallback(head, pipelineFull);
            }
            USB_SERIAL_PRINTLN("FlashTelemetryManager::commitPopulatedHeadBlock() - migration incomplete, current block remains in PSRAM order");
            return commitBlockToPsram(head, pipelineFull);
        }

        if (commitBlockToFlash(head)) {
            return true;
        }
        USB_SERIAL_PRINTLN("FlashTelemetryManager::commitPopulatedHeadBlock() - flash commit failed, preserving assembly order before fallback");
        return preserveCurrentAndFallback(head, pipelineFull);
    }

    return commitBlockToPsram(head, pipelineFull);
}

bool FlashTelemetryManager::pullTailBlock(BlockHeader& header) {
    if (!m_initialized) {
        return false;
    }
    if (m_shutdown_prepared) {
        // Quiesced: spooling consumes records and reclaims (erases) tail
        // sectors, which must not happen after SAFE TO POWER OFF.
        return false;
    }

    if (m_flash_buffer.isFatal() && !salvageAssemblyToPsram()) {
        // If PSRAM is full, serving its oldest record is the only operation
        // that can make room for the retained assembly record.
        USB_SERIAL_PRINTLN("FlashTelemetryManager::pullTailBlock() - assembly salvage pending");
    }

    if (flashActive() && !m_flash_buffer.isEmpty()) {
        // The spooler only pulls when it can upload. If everything flushed has
        // been served but records are still waiting in the RAM assembly
        // buffer, push them to flash now so the backlog can finish draining.
        if (m_flash_buffer.getFlushedRecordCount() == 0 &&
            m_flash_buffer.getAssemblyRecordCount() > 0) {
            if (!m_flash_buffer.flush() && m_flash_buffer.isFatal()) {
                salvageAssemblyToPsram();
            }
        }

        while (true) {
            uint16_t len = 0, meta = 0;
            if (!m_flash_buffer.peekOldestRecord(m_scratch_buffer, m_scratch_size, len, meta)) {
                break;  // nothing flushed and readable - fall through to PSRAM
            }
            if (len > BlockHeader::s_getMaxPayloadSize()) {
                // Cannot be represented as a block - discard rather than wedge.
                USB_SERIAL_PRINTF("FlashTelemetryManager::pullTailBlock() - dropping %u byte record (exceeds block payload %u)\n",
                                  len, BlockHeader::s_getMaxPayloadSize());
                m_flash_buffer.consumeOldestRecord();
                continue;
            }

            m_scratch_block.setPayloadId(m_next_flash_payload_id);
            m_scratch_block.setPayloadSize(len);
            m_scratch_block.setRoundedUpPayloadSize(meta);
            header = m_scratch_block;

            m_last_pull_source = SOURCE_FLASH;
            if (!m_flash_pull_pending) {
                m_flash_pull_pending = true;
                m_flash_reads++;
            }
            return true;
        }
    }

    if (m_psram_pipeline.pullTailBlock(header)) {
        m_last_pull_source = SOURCE_PSRAM;
        return true;
    }

    m_last_pull_source = SOURCE_NONE;
    return false;
}

void FlashTelemetryManager::tailBlockCommitted() {
    // The upload of the last pulled block has been acknowledged - only now is
    // the record removed from storage (delete-on-ack).
    if (m_last_pull_source == SOURCE_FLASH) {
        m_flash_buffer.consumeOldestRecord();
        m_flash_pull_pending = false;
        m_next_flash_payload_id++;
    } else if (m_last_pull_source == SOURCE_PSRAM) {
        m_psram_pipeline.tailBlockCommitted();
    }
    m_last_pull_source = SOURCE_NONE;
    m_last_drain_ms = m_fn_millis ? m_fn_millis() : 0;
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

bool FlashTelemetryManager::pipelineEmpty() const {
    if (!m_initialized) {
        return true;
    }
    bool flash_empty = !m_flash_buffer.isInitialized() || m_flash_buffer.isEmpty();
    return flash_empty && m_psram_pipeline.pipelineEmpty();
}

bool FlashTelemetryManager::pipelineFull() const {
    if (!m_initialized) {
        return false;
    }
    // PSRAM is always a possible route (online bypass or flash fallback), so
    // its no-drop capacity limit must remain visible even in FLASH_ONLY mode.
    return m_psram_pipeline.pipelineFull() ||
           (flashActive() && m_flash_buffer.isFull());
}

uint16_t FlashTelemetryManager::getPipelineLength() const {
    if (!m_initialized) {
        return 0;
    }
    uint32_t total = m_psram_pipeline.getPipelineLength();
    if (m_flash_buffer.isInitialized()) {
        total += m_flash_buffer.getRecordCount();  // O(1), cached counters
    }
    return (total > UINT16_MAX) ? UINT16_MAX : (uint16_t)total;
}

bool FlashTelemetryManager::isPipelineDraining() const {
    if (!m_initialized) {
        return true;
    }
    if (getPipelineLength() == 0) {
        return true;
    }
    // Same semantics as TelemetryPipeline: draining while tail commits have
    // happened within the last 10 seconds.
    return m_fn_millis && (m_fn_millis() < m_last_drain_ms + 10000);
}

uint16_t FlashTelemetryManager::getMaximumDepth() const {
    uint32_t flash_high = (m_max_flash_records > UINT16_MAX)
                              ? UINT16_MAX : m_max_flash_records;
    uint16_t psram_high = m_psram_pipeline.getMaximumDepth();
    return (flash_high > psram_high) ? (uint16_t)flash_high : psram_high;
}

uint16_t FlashTelemetryManager::getMaximumPipelineLength() const {
    if (flashActive()) {
        // Estimate capacity in records of the CONFIGURED block payload size
        // (not the theoretical 1008-byte maximum, which understated the real
        // capacity by 4x for the 256-byte blocks actually stored).
        uint32_t slot = (FlashRingBuffer::RECORD_HEADER_SIZE +
                         BlockHeader::s_getMaxPayloadSize() + 3u) & ~3u;
        uint32_t per_sector = FlashRingBuffer::USABLE_SECTOR_SIZE / slot;
        uint32_t capacity = FlashRingBuffer::RING_SECTORS * per_sector;
        return (capacity > UINT16_MAX) ? UINT16_MAX : (uint16_t)capacity;
    }
    return m_psram_pipeline.getMaximumPipelineLength();
}

uint16_t FlashTelemetryManager::getHeadBlockIndex() const {
    // Diagnostic only: in flash mode the sector indices are the closest analogue.
    return flashActive() ? (uint16_t)(getFlashUsedSpace() / FlashRingBuffer::SECTOR_SIZE)
                         : m_psram_pipeline.getHeadBlockIndex();
}

uint16_t FlashTelemetryManager::getTailBlockIndex() const {
    return flashActive() ? 0 : m_psram_pipeline.getTailBlockIndex();
}

uint32_t FlashTelemetryManager::getFlashRecordCount() const {
    return m_flash_buffer.isInitialized() ? m_flash_buffer.getRecordCount() : 0;
}

uint32_t FlashTelemetryManager::getFlashUsedSpace() const {
    return m_flash_buffer.isInitialized() ? m_flash_buffer.getUsedSpace() : 0;
}

uint32_t FlashTelemetryManager::getFlashFreeSpace() const {
    return m_flash_buffer.isInitialized() ? m_flash_buffer.getFreeSpace() : 0;
}

uint32_t FlashTelemetryManager::getFlashWriteCount() const {
    return m_flash_buffer.isInitialized() ? m_flash_buffer.getWriteCount() : 0;
}

// ---------------------------------------------------------------------------
// Debug and maintenance
// ---------------------------------------------------------------------------

void FlashTelemetryManager::printStatus() const {
    USB_SERIAL_PRINTLN("=== FlashTelemetryManager Status ===");
    USB_SERIAL_PRINTF("Mode: %s, flash enabled: %s, uplink: %s%s\n",
                      (m_storage_mode == FLASH_ONLY) ? "FLASH_ONLY" :
                      (m_storage_mode == PSRAM_ONLY) ? "PSRAM_ONLY" : "HYBRID",
                      m_enable_flash_buffer ? "yes" : "no",
                      m_uplink_available ? "available" : "unavailable",
                      m_shutdown_prepared ? " (QUIESCED - shutdown prepared)" : "");
    USB_SERIAL_PRINTF("Stats: %u flash writes, %u flash reads, %u PSRAM fallbacks, %u migrated blocks\n",
                      m_flash_writes, m_flash_reads, m_psram_fallbacks, m_migrated_blocks);
    USB_SERIAL_PRINTF("High-water: %u flash records\n", m_max_flash_records);

    if (m_flash_buffer.isInitialized()) {
        m_flash_buffer.printStatus();
    } else {
        USB_SERIAL_PRINTLN("Flash buffer: not initialized");
    }

    USB_SERIAL_PRINTF("PSRAM pipeline: %u blocks, empty=%s, full=%s\n",
                      m_psram_pipeline.getPipelineLength(),
                      m_psram_pipeline.pipelineEmpty() ? "yes" : "no",
                      m_psram_pipeline.pipelineFull() ? "yes" : "no");
    USB_SERIAL_PRINTLN("====================================");
}

bool FlashTelemetryManager::performSelfTest() {
    if (!m_initialized) {
        return false;
    }
    if (!m_flash_buffer.isInitialized()) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::performSelfTest() - flash not initialized, nothing to test");
        return true;
    }
    // Safe by design: destructive parts only run on an empty ring.
    return m_flash_buffer.performSelfTest();
}

bool FlashTelemetryManager::prepareForShutdown() {
    USB_SERIAL_PRINTLN("FlashTelemetryManager::prepareForShutdown()");
    if (!m_initialized) {
        return true;  // nothing running, nothing buffered
    }
    if (m_shutdown_prepared) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::prepareForShutdown() - already prepared");
        return true;
    }

    if (!flashActive()) {
        // Volatile-only operation (flash disabled or its init failed): the
        // PSRAM pipeline cannot survive power-off, so a safe persistent
        // shutdown cannot be reported.
        USB_SERIAL_PRINTF("FlashTelemetryManager::prepareForShutdown() - NOT SAFE: flash unavailable, "
                          "%u volatile PSRAM records would be lost at power off\n",
                          m_psram_pipeline.getPipelineLength());
        return false;
    }

    // Quiesce FIRST: from here commitPopulatedHeadBlock() and pullTailBlock()
    // refuse, so no record can slip in between migration and the flash lock,
    // and nothing keeps mutating flash after success is reported.
    m_shutdown_prepared = true;

    // Move anything still in volatile PSRAM into flash and verify.
    bool migration_ok = migratePsramBacklogToFlash();
    uint32_t psram_left = m_psram_pipeline.getPipelineLength();
    if (!migration_ok || psram_left != 0) {
        USB_SERIAL_PRINTF("FlashTelemetryManager::prepareForShutdown() - NOT SAFE: %u PSRAM records "
                          "could not be migrated to flash\n", psram_left);
        m_shutdown_prepared = false;  // resume normal operation; retry allowed
        return false;
    }

    if (!m_flash_buffer.prepareForShutdown()) {
        m_shutdown_prepared = false;  // resume normal operation; retry allowed
        return false;
    }

    USB_SERIAL_PRINTLN("FlashTelemetryManager::prepareForShutdown() - telemetry quiesced, PSRAM empty, flash flushed and locked");
    return true;
}

// ---------------------------------------------------------------------------
// Diagnostics - available whenever the flash buffer initialized
// ---------------------------------------------------------------------------

#define REQUIRE_FLASH_INITIALIZED(retval)                                          \
    if (!m_flash_buffer.isInitialized()) {                                         \
        USB_SERIAL_PRINTLN("FlashTelemetryManager - flash buffer not initialized"); \
        return retval;                                                             \
    }

bool FlashTelemetryManager::performPowerOnSelfTest(bool auto_repair) {
    REQUIRE_FLASH_INITIALIZED(false);
    return m_flash_buffer.performPowerOnSelfTest(auto_repair);
}

bool FlashTelemetryManager::performDeepSectorValidation() {
    REQUIRE_FLASH_INITIALIZED(false);
    return m_flash_buffer.performDeepSectorValidation();
}

bool FlashTelemetryManager::performPowerLossRecoveryTest() {
    REQUIRE_FLASH_INITIALIZED(false);
    return m_flash_buffer.performPowerLossRecoveryTest();
}

bool FlashTelemetryManager::performStressTest(uint32_t num_records) {
    REQUIRE_FLASH_INITIALIZED(false);
    return m_flash_buffer.performStressTest(num_records);
}

bool FlashTelemetryManager::factoryReset() {
    USB_SERIAL_PRINTLN("FlashTelemetryManager::factoryReset()");
    bool ok = true;
    if (m_flash_buffer.isInitialized()) {
        ok = m_flash_buffer.factoryReset();
    }
    m_flash_writes = 0;
    m_flash_reads = 0;
    m_psram_fallbacks = 0;
    m_migrated_blocks = 0;
    m_max_flash_records = 0;
    m_flash_pull_pending = false;
    m_last_pull_source = SOURCE_NONE;
    m_shutdown_prepared = false;  // factory reset re-arms normal operation
    return ok;
}

bool FlashTelemetryManager::clearAllFlashData() {
    REQUIRE_FLASH_INITIALIZED(false);
    m_flash_pull_pending = false;
    m_last_pull_source = SOURCE_NONE;
    return m_flash_buffer.clearAllData();
}

bool FlashTelemetryManager::repairFlashCorruption() {
    REQUIRE_FLASH_INITIALIZED(false);
    return m_flash_buffer.repairCorruption();
}

// ---------------------------------------------------------------------------
// Failure injection pass-throughs (TESTING_MODE builds only)
// ---------------------------------------------------------------------------

#ifdef TESTING_MODE

bool FlashTelemetryManager::injectSectorCorruption(uint32_t sector_index) {
    REQUIRE_FLASH_INITIALIZED(false);
    return m_flash_buffer.injectSectorCorruption(sector_index);
}

bool FlashTelemetryManager::corruptPersistedState() {
    REQUIRE_FLASH_INITIALIZED(false);
    return m_flash_buffer.corruptPersistedState();
}

bool FlashTelemetryManager::simulateIncompleteWrite() {
    REQUIRE_FLASH_INITIALIZED(false);
    return m_flash_buffer.simulateIncompleteWrite();
}

bool FlashTelemetryManager::corruptRingPointers() {
    REQUIRE_FLASH_INITIALIZED(false);
    return m_flash_buffer.corruptRingPointers();
}

bool FlashTelemetryManager::acceleratedWearTest(uint32_t cycles) {
    REQUIRE_FLASH_INITIALIZED(false);
    return m_flash_buffer.acceleratedWearTest(cycles);
}

bool FlashTelemetryManager::injectRandomCorruption(uint32_t num_sectors) {
    REQUIRE_FLASH_INITIALIZED(false);
    return m_flash_buffer.injectRandomCorruption(num_sectors);
}

bool FlashTelemetryManager::simulatePartitionFailure() {
    REQUIRE_FLASH_INITIALIZED(false);
    return m_flash_buffer.simulatePartitionFailure();
}

bool FlashTelemetryManager::injectCRCCorruption(uint32_t sector_index) {
    REQUIRE_FLASH_INITIALIZED(false);
    return m_flash_buffer.injectCRCCorruption(sector_index);
}

void FlashTelemetryManager::enableFailureInjection() {
    if (m_flash_buffer.isInitialized()) {
        m_flash_buffer.enableFailureInjection();
    }
}

bool FlashTelemetryManager::runReviewTestMatrix() {
    REQUIRE_FLASH_INITIALIZED(false);
    if (m_storage_mode != FLASH_ONLY) {
        USB_SERIAL_PRINTLN("MATRIX: refused - manager not in FLASH_ONLY mode");
        return false;
    }
    if (getPipelineLength() != 0 || m_flash_buffer.isFatal()) {
        USB_SERIAL_PRINTLN("MATRIX: refused - pipelines must be empty and the ring trusted");
        return false;
    }

    bool all = true;
    bool previous_uplink = m_uplink_available;
    memset(&m_manager_seams, 0, sizeof(m_manager_seams));

    auto report = [&](const char* name, bool passed) {
        USB_SERIAL_PRINTF("MATRIX %-64s %s\n", name, passed ? "PASS" : "FAIL");
        if (!passed) all = false;
    };
    auto commitMarked = [&](uint8_t marker) {
        BlockHeader block = getHeadBlockForPopulating();
        uint16_t maximum = 0;
        uint8_t* buffer = block.getBuffer(maximum);
        if (!buffer || maximum < 64) return false;
        memset(buffer, marker, 64);
        block.setPayloadSize(64);
        block.setRoundedUpPayloadSize(marker);
        bool full = false;
        return commitPopulatedHeadBlock(block, full);
    };
    auto drainExpected = [&](const uint8_t* expected, uint8_t count) {
        for (uint8_t i = 0; i < count; i++) {
            BlockHeader tail;
            if (!pullTailBlock(tail) || tail.getPayloadSize() != 64) return false;
            uint16_t maximum = 0;
            uint8_t* buffer = tail.getBuffer(maximum);
            if (!buffer || buffer[0] != expected[i] ||
                tail.getRoundedUpPayloadSize() != expected[i]) return false;
            tailBlockCommitted();
        }
        BlockHeader extra;
        return !pullTailBlock(extra) && pipelineEmpty();
    };

    // M1: a real failed recovery while assembly records exist leaves flash
    // non-writable. The subsequent fallback preserves oldest-first ordering.
    {
        setUplinkAvailable(false);
        bool staged = commitMarked(0xA1) && commitMarked(0xA2) &&
                      m_flash_buffer.getAssemblyRecordCount() == 2;
        m_flash_buffer.testForceFatal();
        m_flash_buffer.faultSeams().fail_recovery_before_sector_scan = true;
        bool recovery_failed = !m_flash_buffer.performPowerOnSelfTest(true) &&
                               m_flash_buffer.isFatal() &&
                               m_flash_buffer.getAssemblyRecordCount() == 2;
        bool current_stored = commitMarked(0xA3);
        bool salvaged = m_flash_buffer.getAssemblyRecordCount() == 0 &&
                        m_psram_pipeline.getPipelineLength() == 3;
        bool recovered = m_flash_buffer.performPowerOnSelfTest(true) &&
                         !m_flash_buffer.isFatal();
        const uint8_t expected[] = {0xA1, 0xA2, 0xA3};
        bool ordered = drainExpected(expected, 3);
        report("M1 failed recovery: non-writable, loss-safe ordered fallback",
               staged && recovery_failed && current_stored && salvaged && recovered && ordered);
    }

    // M2: partition loss after getHead() but before commit must immediately
    // demote and preserve the populated block behind the older assembly.
    {
        setUplinkAvailable(false);
        bool staged = commitMarked(0xB1) && commitMarked(0xB2);
        BlockHeader third = getHeadBlockForPopulating();
        uint16_t maximum = 0;
        uint8_t* buffer = third.getBuffer(maximum);
        bool populated = buffer && maximum >= 64;
        if (populated) {
            memset(buffer, 0xB3, 64);
            third.setPayloadSize(64);
            third.setRoundedUpPayloadSize(0xB3);
        }
        bool injected = m_flash_buffer.simulatePartitionFailure() &&
                        m_flash_buffer.isFatal();
        bool full = false;
        bool current_stored = populated && commitPopulatedHeadBlock(third, full);
        bool restored = m_flash_buffer.testRestorePartitionAccess();
        bool recovered = restored && m_flash_buffer.performPowerOnSelfTest(true) &&
                         !m_flash_buffer.isFatal();
        const uint8_t expected[] = {0xB1, 0xB2, 0xB3};
        bool ordered = drainExpected(expected, 3);
        report("M2 partition loss between get/commit: immediate ordered demotion",
               staged && injected && current_stored && recovered && ordered);
    }

    // M3: destination refusal cannot consume the source assembly record.
    {
        setUplinkAvailable(false);
        bool staged = commitMarked(0xC1) && commitMarked(0xC2);
        m_flash_buffer.testForceFatal();
        m_manager_seams.fail_psram_commit_countdown = 1;
        getHeadBlockForPopulating();
        bool retained = m_flash_buffer.getAssemblyRecordCount() == 2 &&
                        m_psram_pipeline.getPipelineLength() == 0;
        memset(&m_manager_seams, 0, sizeof(m_manager_seams));
        bool retry = salvageAssemblyToPsram() &&
                     m_flash_buffer.getAssemblyRecordCount() == 0;
        bool recovered = m_flash_buffer.performPowerOnSelfTest(true) &&
                         !m_flash_buffer.isFatal();
        const uint8_t expected[] = {0xC1, 0xC2};
        bool ordered = drainExpected(expected, 2);
        report("M3 PSRAM refusal: source retained, retry commits exactly in order",
               staged && retained && retry && recovered && ordered);
    }

    // M4: shutdown cannot claim safety after a failed migration. State is
    // re-armed for retry, and a successful retry locks only after persistence.
    {
        setUplinkAvailable(true);
        bool staged = commitMarked(0xD1) && m_psram_pipeline.getPipelineLength() == 1;
        m_manager_seams.fail_migration_countdown = 1;
        bool refused = !prepareForShutdown() && !m_shutdown_prepared &&
                       m_psram_pipeline.getPipelineLength() == 1;
        memset(&m_manager_seams, 0, sizeof(m_manager_seams));
        bool retry_safe = prepareForShutdown() && m_shutdown_prepared &&
                          m_psram_pipeline.pipelineEmpty();
        bool reset = m_flash_buffer.testResetForMatrix();
        m_shutdown_prepared = false;
        report("M4 migration failure: shutdown NOT SAFE; clean retry succeeds",
               staged && refused && retry_safe && reset);
    }

    setUplinkAvailable(previous_uplink);

    // Ring-level matrix (cases 1-12).
    bool ring_ok = m_flash_buffer.runReviewTestMatrix();

    USB_SERIAL_PRINTF("=== FlashTelemetryManager review-test matrix: %s ===\n",
                      (all && ring_ok) ? "ALL PASSED" : "FAILURES (see above)");
    return all && ring_ok;
}

#endif // TESTING_MODE
