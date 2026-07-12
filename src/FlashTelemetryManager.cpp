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

FlashTelemetryManager::FlashTelemetryManager() :
    m_storage_mode(PSRAM_ONLY),
    m_initialized(false),
    m_enable_flash_buffer(false),
    m_uplink_available(false),
    m_last_pull_source(SOURCE_NONE),
    m_scratch_buffer(nullptr),
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
    if (!m_scratch_buffer) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::init() - scratch allocation failed");
        teardown();
        return false;
    }
    m_scratch_block = BlockHeader(m_scratch_buffer);

    if (m_enable_flash_buffer) {
        if (m_flash_buffer.init(fn_millis)) {
            m_storage_mode = FLASH_ONLY;
            USB_SERIAL_PRINTF("FlashTelemetryManager::init() - flash ready, %u persisted records waiting\n",
                              m_flash_buffer.getRecordCount());
            // Read-only structural check; never destroys or writes data.
            if (!m_flash_buffer.performPowerOnSelfTest(true)) {
                USB_SERIAL_PRINTLN("FlashTelemetryManager::init() - POST reported issues; continuing (data is CRC-checked on read)");
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

void FlashTelemetryManager::migratePsramBacklogToFlash() {
    // Blocks committed to PSRAM while the uplink was up but not yet uploaded
    // are older than the block being committed now - move them to flash first
    // so ordering and persistence are preserved.
    BlockHeader block;
    while (m_psram_pipeline.pullTailBlock(block)) {
        if (!commitBlockToFlash(block)) {
            // Leave the block in PSRAM rather than lose it.
            USB_SERIAL_PRINTLN("FlashTelemetryManager::migratePsramBacklogToFlash() - migration halted (flash append failed)");
            break;
        }
        m_psram_pipeline.tailBlockCommitted();
        m_migrated_blocks++;
    }
}

bool FlashTelemetryManager::commitPopulatedHeadBlock(BlockHeader head, bool& pipelineFull) {
    if (!m_initialized) {
        return false;
    }

    pipelineFull = false;

    bool route_to_flash = flashActive() &&
                          (!m_uplink_available || !m_flash_buffer.isEmpty());

    if (route_to_flash) {
        migratePsramBacklogToFlash();

        if (commitBlockToFlash(head)) {
            return true;
        }
        USB_SERIAL_PRINTLN("FlashTelemetryManager::commitPopulatedHeadBlock() - flash commit failed, PSRAM fallback");
        m_psram_fallbacks++;
    }

    return m_psram_pipeline.commitPopulatedHeadBlock(head, pipelineFull);
}

bool FlashTelemetryManager::pullTailBlock(BlockHeader& header) {
    if (!m_initialized) {
        return false;
    }

    if (flashActive() && !m_flash_buffer.isEmpty()) {
        // The spooler only pulls when it can upload. If everything flushed has
        // been served but records are still waiting in the RAM assembly
        // buffer, push them to flash now so the backlog can finish draining.
        if (m_flash_buffer.getFlushedRecordCount() == 0 &&
            m_flash_buffer.getAssemblyRecordCount() > 0) {
            m_flash_buffer.flush();
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
    if (flashActive()) {
        return m_flash_buffer.isFull();  // effectively never (drop-oldest)
    }
    return m_psram_pipeline.pipelineFull();
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
        // Theoretical flash capacity in max-size records fits comfortably in u16.
        uint32_t capacity = FlashRingBuffer::TOTAL_SECTORS * 4;
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
    USB_SERIAL_PRINTF("Mode: %s, flash enabled: %s, uplink: %s\n",
                      (m_storage_mode == FLASH_ONLY) ? "FLASH_ONLY" :
                      (m_storage_mode == PSRAM_ONLY) ? "PSRAM_ONLY" : "HYBRID",
                      m_enable_flash_buffer ? "yes" : "no",
                      m_uplink_available ? "available" : "unavailable");
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

void FlashTelemetryManager::prepareForShutdown() {
    USB_SERIAL_PRINTLN("FlashTelemetryManager::prepareForShutdown()");
    if (m_flash_buffer.isInitialized()) {
        m_flash_buffer.prepareForShutdown();
    }
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

#endif // TESTING_MODE
