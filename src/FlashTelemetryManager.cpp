#include "FlashTelemetryManager.h"
#include "SerialConfig.h"
#include <Arduino.h>
#include <cstring>

FlashTelemetryManager::FlashTelemetryManager() :
    m_storage_mode(PSRAM_ONLY),
    m_initialized(false),
    m_enable_flash_buffer(false),
    m_flash_writes(0),
    m_flash_reads(0),
    m_psram_fallbacks(0),
    m_fn_millis(nullptr),
    m_has_pending_head_block(false),
    m_temp_buffer(nullptr) {
}

FlashTelemetryManager::~FlashTelemetryManager() {
    teardown();
}

bool FlashTelemetryManager::init(long unsigned int (*fn_millis)(void), 
                                const uint16_t maxBlockBufferMemoryUsageKB,
                                const uint16_t maxBlockBufferMemoryUsageBytesRemainder) {
    
    USB_SERIAL_PRINTLN("FlashTelemetryManager::init() - Starting initialization");
    
    if (m_initialized) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager already initialized");
        return true;
    }
    
    m_fn_millis = fn_millis;
    
    // Allocate temporary buffer
    m_temp_buffer = (uint8_t*)malloc(TEMP_BUFFER_SIZE);
    if (!m_temp_buffer) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::init() - Failed to allocate temp buffer");
        return false;
    }
    
    // Always initialize PSRAM pipeline as fallback
    if (!m_psram_pipeline.init(fn_millis, maxBlockBufferMemoryUsageKB, maxBlockBufferMemoryUsageBytesRemainder)) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::init() - Failed to initialize PSRAM pipeline");
        teardown();
        return false;
    }
    
    // Try to initialize flash buffer if enabled
    if (m_enable_flash_buffer) {
        if (m_flash_buffer.init(fn_millis)) {
            USB_SERIAL_PRINTLN("FlashTelemetryManager::init() - Flash buffer initialized successfully");
            m_storage_mode = FLASH_ONLY;
            
            // Run self-test
            if (!m_flash_buffer.performSelfTest()) {
                USB_SERIAL_PRINTLN("FlashTelemetryManager::init() - Flash buffer self-test failed, falling back to PSRAM");
                m_storage_mode = PSRAM_ONLY;
                m_psram_fallbacks++;
            }
        } else {
            USB_SERIAL_PRINTLN("FlashTelemetryManager::init() - Flash buffer initialization failed, using PSRAM only");
            m_storage_mode = PSRAM_ONLY;
            m_psram_fallbacks++;
        }
    } else {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::init() - Flash buffer disabled, using PSRAM only");
        m_storage_mode = PSRAM_ONLY;
    }
    
    m_initialized = true;
    
    USB_SERIAL_PRINTF("FlashTelemetryManager::init() - Initialized in %s mode\n", 
              (m_storage_mode == FLASH_ONLY) ? "FLASH" : 
              (m_storage_mode == PSRAM_ONLY) ? "PSRAM" : "HYBRID");
    
    printStatus();
    return true;
}

void FlashTelemetryManager::teardown() {
    if (m_initialized) {
        prepareForShutdown();
    }
    
    m_flash_buffer.teardown();
    m_psram_pipeline.teardown();
    
    if (m_temp_buffer) {
        free(m_temp_buffer);
        m_temp_buffer = nullptr;
    }
    
    m_initialized = false;
}

void FlashTelemetryManager::setStorageMode(StorageMode mode) {
    USB_SERIAL_PRINTF("FlashTelemetryManager::setStorageMode() - Switching to mode %d\n", mode);
    m_storage_mode = mode;
}

void FlashTelemetryManager::enableFlashBuffer(bool enable) {
    USB_SERIAL_PRINTF("FlashTelemetryManager::enableFlashBuffer() - %s flash buffer\n", 
              enable ? "Enabling" : "Disabling");
    m_enable_flash_buffer = enable;
    
    if (!enable && m_storage_mode != PSRAM_ONLY) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::enableFlashBuffer() - Switching to PSRAM mode");
        m_storage_mode = PSRAM_ONLY;
    }
}

// TelemetryPipeline-compatible API implementation
BlockHeader FlashTelemetryManager::getHeadBlockForPopulating() {
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::getHeadBlockForPopulating() - Not initialized");
        return BlockHeader(); // Return invalid block
    }
    
    // Always get block from PSRAM pipeline for compatibility
    // We'll convert it to flash record during commit if needed
    m_current_head_block = m_psram_pipeline.getHeadBlockForPopulating();
    m_has_pending_head_block = true;
    
    return m_current_head_block;
}

bool FlashTelemetryManager::commitPopulatedHeadBlock(BlockHeader head, bool& pipelineFull) {
    if (!m_initialized || !m_has_pending_head_block) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::commitPopulatedHeadBlock() - Invalid state");
        return false;
    }
    
    pipelineFull = false;
    bool success = false;
    
    if (m_storage_mode == FLASH_ONLY && m_flash_buffer.isInitialized()) {
        // Convert BlockHeader to flash record
        success = convertBlockToFlashRecord(head);
        if (success) {
            m_flash_writes++;
            USB_SERIAL_PRINTF("FlashTelemetryManager::commitPopulatedHeadBlock() - Committed %u bytes to flash\n", 
                      head.getPayloadSize());
        } else {
            USB_SERIAL_PRINTLN("FlashTelemetryManager::commitPopulatedHeadBlock() - Flash write failed, trying PSRAM fallback");
            m_psram_fallbacks++;
            success = m_psram_pipeline.commitPopulatedHeadBlock(head, pipelineFull);
        }
    } else {
        // Use PSRAM pipeline
        success = m_psram_pipeline.commitPopulatedHeadBlock(head, pipelineFull);
    }
    
    m_has_pending_head_block = false;
    return success;
}

bool FlashTelemetryManager::pullTailBlock(BlockHeader& header) {
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::pullTailBlock() - Not initialized");
        return false;
    }
    
    bool success = false;
    
    if (m_storage_mode == FLASH_ONLY && m_flash_buffer.isInitialized()) {
        // Try to read from flash buffer first
        success = convertFlashRecordToBlock(header);
        if (success) {
            m_flash_reads++;
            USB_SERIAL_PRINTF("FlashTelemetryManager::pullTailBlock() - Read %u bytes from flash\n", 
                      header.getPayloadSize());
            return true;
        }
    }
    
    // Fall back to PSRAM pipeline
    success = m_psram_pipeline.pullTailBlock(header);
    if (!success && m_storage_mode == FLASH_ONLY) {
        // No data in either buffer
        return false;
    }
    
    return success;
}

void FlashTelemetryManager::tailBlockCommitted() {
    // The block has been successfully uploaded via MQTT
    // In flash mode, we already deleted the record during pullTailBlock
    // In PSRAM mode, we need to commit the tail block
    
    if (m_storage_mode == PSRAM_ONLY || shouldUsePsramFallback()) {
        m_psram_pipeline.tailBlockCommitted();
    }
}

// Status methods - delegate to appropriate backend
bool FlashTelemetryManager::pipelineEmpty() const {
    if (!m_initialized) {
        return true;
    }
    
    if (m_storage_mode == FLASH_ONLY && m_flash_buffer.isInitialized()) {
        return m_flash_buffer.isEmpty() && m_psram_pipeline.pipelineEmpty();
    }
    
    return m_psram_pipeline.pipelineEmpty();
}

bool FlashTelemetryManager::pipelineFull() const {
    if (!m_initialized) {
        return false;
    }
    
    if (m_storage_mode == FLASH_ONLY && m_flash_buffer.isInitialized()) {
        return m_flash_buffer.isFull();
    }
    
    return m_psram_pipeline.pipelineFull();
}

uint16_t FlashTelemetryManager::getPipelineLength() const {
    if (!m_initialized) {
        return 0;
    }
    
    if (m_storage_mode == FLASH_ONLY && m_flash_buffer.isInitialized()) {
        // Return flash record count, clamped to uint16_t range
        uint32_t count = m_flash_buffer.getRecordCount();
        return (count > UINT16_MAX) ? UINT16_MAX : (uint16_t)count;
    }
    
    return m_psram_pipeline.getPipelineLength();
}

bool FlashTelemetryManager::isPipelineDraining() const {
    if (!m_initialized) {
        return true;
    }
    
    if (m_storage_mode == FLASH_ONLY && m_flash_buffer.isInitialized()) {
        // Flash buffer is "draining" if it's not full and we're actively reading
        return !m_flash_buffer.isFull();
    }
    
    return m_psram_pipeline.isPipelineDraining();
}

// Flash-specific methods
uint32_t FlashTelemetryManager::getFlashRecordCount() const {
    if (m_flash_buffer.isInitialized()) {
        return m_flash_buffer.getRecordCount();
    }
    return 0;
}

uint32_t FlashTelemetryManager::getFlashUsedSpace() const {
    if (m_flash_buffer.isInitialized()) {
        return m_flash_buffer.getUsedSpace();
    }
    return 0;
}

uint32_t FlashTelemetryManager::getFlashFreeSpace() const {
    if (m_flash_buffer.isInitialized()) {
        return m_flash_buffer.getFreeSpace();
    }
    return 0;
}

uint32_t FlashTelemetryManager::getFlashWriteCount() const {
    if (m_flash_buffer.isInitialized()) {
        return m_flash_buffer.getWriteCount();
    }
    return 0;
}

void FlashTelemetryManager::printStatus() const {
    USB_SERIAL_PRINTLN("=== FlashTelemetryManager Status ===");
    USB_SERIAL_PRINTF("Storage Mode: %s\n", 
              (m_storage_mode == FLASH_ONLY) ? "FLASH_ONLY" : 
              (m_storage_mode == PSRAM_ONLY) ? "PSRAM_ONLY" : "HYBRID");
    USB_SERIAL_PRINTF("Flash Buffer Enabled: %s\n", m_enable_flash_buffer ? "Yes" : "No");
    USB_SERIAL_PRINTF("Flash Writes: %u\n", m_flash_writes);
    USB_SERIAL_PRINTF("Flash Reads: %u\n", m_flash_reads);
    USB_SERIAL_PRINTF("PSRAM Fallbacks: %u\n", m_psram_fallbacks);
    
    if (m_flash_buffer.isInitialized()) {
        USB_SERIAL_PRINTLN("--- Flash Buffer Status ---");
        m_flash_buffer.printStatus();
    }
    
    USB_SERIAL_PRINTLN("--- PSRAM Pipeline Status ---");
    USB_SERIAL_PRINTF("PSRAM Pipeline Length: %u\n", m_psram_pipeline.getPipelineLength());
    USB_SERIAL_PRINTF("PSRAM Pipeline Empty: %s\n", m_psram_pipeline.pipelineEmpty() ? "Yes" : "No");
    USB_SERIAL_PRINTF("PSRAM Pipeline Full: %s\n", m_psram_pipeline.pipelineFull() ? "Yes" : "No");
    USB_SERIAL_PRINTLN("================================");
}

bool FlashTelemetryManager::performSelfTest() {
    USB_SERIAL_PRINTLN("FlashTelemetryManager::performSelfTest() - Running self-test");
    
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::performSelfTest() - Not initialized");
        return false;
    }
    
    bool flash_test_passed = true;
    if (m_flash_buffer.isInitialized()) {
        flash_test_passed = m_flash_buffer.performSelfTest();
        USB_SERIAL_PRINTF("FlashTelemetryManager::performSelfTest() - Flash test: %s\n", 
                  flash_test_passed ? "PASSED" : "FAILED");
    }
    
    USB_SERIAL_PRINTF("FlashTelemetryManager::performSelfTest() - Overall result: %s\n", 
              flash_test_passed ? "PASSED" : "FAILED");
    
    return flash_test_passed;
}

void FlashTelemetryManager::prepareForShutdown() {
    USB_SERIAL_PRINTLN("FlashTelemetryManager::prepareForShutdown() - Preparing for shutdown");
    
    if (m_flash_buffer.isInitialized()) {
        m_flash_buffer.prepareForShutdown();
    }
}

// Helper methods
bool FlashTelemetryManager::convertBlockToFlashRecord(const BlockHeader& block) {
    if (!block.isBlockValid() || !m_flash_buffer.isInitialized()) {
        return false;
    }
    
    uint16_t payload_size = block.getPayloadSize();
    if (payload_size == 0) {
        return false;
    }
    
    uint16_t max_payload_size;
    uint8_t* block_buffer = const_cast<BlockHeader&>(block).getBuffer(max_payload_size);
    
    return m_flash_buffer.appendRecord(block_buffer, payload_size);
}

bool FlashTelemetryManager::convertFlashRecordToBlock(BlockHeader& block) {
    if (!m_flash_buffer.isInitialized()) {
        return false;
    }
    
    uint16_t actual_length;
    if (!m_flash_buffer.readOldestRecord(m_temp_buffer, TEMP_BUFFER_SIZE, actual_length)) {
        return false;
    }
    
    // Delete the record we just read
    if (!m_flash_buffer.deleteOldestRecord()) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::convertFlashRecordToBlock() - Warning: failed to delete record");
    }
    
    // Create a BlockHeader and populate it
    block = m_psram_pipeline.getHeadBlockForPopulating();
    uint16_t max_payload_size;
    uint8_t* block_buffer = const_cast<BlockHeader&>(block).getBuffer(max_payload_size);
    
    if (actual_length > max_payload_size) {
        USB_SERIAL_PRINTF("FlashTelemetryManager::convertFlashRecordToBlock() - Record too large: %u > %u\n", 
                  actual_length, max_payload_size);
        return false;
    }
    
    memcpy(block_buffer, m_temp_buffer, actual_length);
    block.setPayloadSize(actual_length);
    
    return true;
}

bool FlashTelemetryManager::shouldUsePsramFallback() const {
    return (m_storage_mode == PSRAM_ONLY || !m_flash_buffer.isInitialized());
}

bool FlashTelemetryManager::factoryReset() {
    USB_SERIAL_PRINTLN("FlashTelemetryManager::factoryReset() - Starting factory reset");
    
    // Reset flash buffer
    bool flash_reset_success = true;
    if (m_flash_buffer.isInitialized()) {
        flash_reset_success = m_flash_buffer.factoryReset();
    }
    
    // Clear statistics
    m_flash_writes = 0;
    m_flash_reads = 0;
    m_psram_fallbacks = 0;
    
    // Reset state
    m_has_pending_head_block = false;
    
    USB_SERIAL_PRINTF("FlashTelemetryManager::factoryReset() - Factory reset %s\n", 
              flash_reset_success ? "successful" : "failed");
    
    return flash_reset_success;
}

bool FlashTelemetryManager::clearAllFlashData() {
    USB_SERIAL_PRINTLN("FlashTelemetryManager::clearAllFlashData() - Clearing all flash data");
    
    if (!m_flash_buffer.isInitialized()) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::clearAllFlashData() - Flash buffer not initialized");
        return false;
    }
    
    return m_flash_buffer.clearAllData();
}

bool FlashTelemetryManager::repairFlashCorruption() {
    USB_SERIAL_PRINTLN("FlashTelemetryManager::repairFlashCorruption() - Repairing flash corruption");
    
    if (!m_flash_buffer.isInitialized()) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::repairFlashCorruption() - Flash buffer not initialized");
        return false;
    }
    
    return m_flash_buffer.repairCorruption();
}

// Extended diagnostic methods - delegate to FlashRingBuffer
bool FlashTelemetryManager::performPowerOnSelfTest(bool auto_repair) {
    if (m_storage_mode != FLASH_ONLY) {
        USB_SERIAL_PRINTF("FlashTelemetryManager::performPowerOnSelfTest() - Not in flash mode (mode: %d)\n", (int)m_storage_mode);
        return false;
    }
    
    if (!m_flash_buffer.isInitialized()) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::performPowerOnSelfTest() - Flash buffer not initialized");
        return false;
    }
    
    return m_flash_buffer.performPowerOnSelfTest(auto_repair);
}

bool FlashTelemetryManager::performDeepSectorValidation() {
    if (m_storage_mode != FLASH_ONLY) {
        USB_SERIAL_PRINTF("FlashTelemetryManager::performDeepSectorValidation() - Not in flash mode (mode: %d)\n", (int)m_storage_mode);
        return false;
    }
    
    if (!m_flash_buffer.isInitialized()) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::performDeepSectorValidation() - Flash buffer not initialized");
        return false;
    }
    
    return m_flash_buffer.performDeepSectorValidation();
}

bool FlashTelemetryManager::performPowerLossRecoveryTest() {
    if (m_storage_mode != FLASH_ONLY) {
        USB_SERIAL_PRINTF("FlashTelemetryManager::performPowerLossRecoveryTest() - Not in flash mode (mode: %d)\n", (int)m_storage_mode);
        return false;
    }
    
    if (!m_flash_buffer.isInitialized()) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::performPowerLossRecoveryTest() - Flash buffer not initialized");
        return false;
    }
    
    return m_flash_buffer.performPowerLossRecoveryTest();
}

bool FlashTelemetryManager::performStressTest(uint32_t num_records) {
    if (m_storage_mode != FLASH_ONLY) {
        USB_SERIAL_PRINTF("FlashTelemetryManager::performStressTest() - Not in flash mode (mode: %d)\n", (int)m_storage_mode);
        return false;
    }
    
    if (!m_flash_buffer.isInitialized()) {
        USB_SERIAL_PRINTLN("FlashTelemetryManager::performStressTest() - Flash buffer not initialized");
        return false;
    }
    
    return m_flash_buffer.performStressTest(num_records);
}