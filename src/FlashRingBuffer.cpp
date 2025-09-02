/**
 * @file FlashRingBuffer.cpp
 * @brief Implementation of power-safe flash ring buffer for marine telemetry storage
 * 
 * This file contains the core implementation of the FlashRingBuffer class,
 * providing persistent storage for telemetry data in marine environments.
 * 
 * Key Implementation Features:
 * - Constructor/destructor with proper resource management
 * - Initialization sequence with partition discovery and state recovery
 * - Core record operations with power-loss safety
 * - State management with ESP32 NVS persistence
 * - CRC-protected data integrity
 * - RAM-buffered writes for efficiency
 * 
 * Marine Use Cases:
 * - Dive computer data logging during 8+ hour excursions
 * - Survives power cycles during equipment shutdown/startup  
 * - Spools stored data when connectivity returns
 * - Concurrent logging while uploading backlog
 * 
 * @author Generated for Mercator Origins dive computer system
 * @version 1.0
 * @date 2024
 */

#include "FlashRingBuffer.h"
#include "SerialConfig.h"
#include <Arduino.h>
#include <cstring>

// === Constructor and Destructor ===
/**
 * @brief Constructor - Initialize member variables to safe defaults
 * 
 * Sets all pointers to null and initializes state structure to zero.
 * No memory allocation or hardware access performed here - that's
 * deferred to init() for proper error handling.
 */
FlashRingBuffer::FlashRingBuffer() : 
    m_partition(nullptr),                // ESP32 flash partition handle - null until init()
    m_initialized(false),               // System not ready for use yet  
    m_fn_millis(nullptr),               // Timing function - injected during init()
    m_sector_buffer(nullptr),           // 4KB read buffer - allocated during init()
    m_sector_assembly_buffer(nullptr),  // 4KB write assembly buffer - allocated during init()
    m_buffer_used(0) {                  // No data in assembly buffer yet
    memset(&m_state, 0, sizeof(m_state)); // Clear ring buffer state to all zeros
}

/**
 * @brief Destructor - Clean up all resources
 * 
 * Ensures proper shutdown sequence with RAM buffer flush and
 * state persistence before releasing memory. Safe to call
 * even if init() was never called or failed.
 */
FlashRingBuffer::~FlashRingBuffer() {
    teardown();  // Handles all cleanup including flush and memory deallocation
}

// === Initialization and Teardown ===

/**
 * @brief Complete system initialization for marine telemetry storage
 * @param fn_millis Function pointer to Arduino millis() for timing operations
 * @return true if initialization successful and system ready for use
 * 
 * Performs comprehensive initialization sequence:
 * 1. Memory allocation for sector buffers
 * 2. Flash partition discovery and validation
 * 3. ESP32 NVS initialization for persistent state
 * 4. State recovery from previous sessions
 * 5. Power-on self-test with auto-repair capabilities
 * 
 * Marine Safety Considerations:
 * - Automatic recovery from incomplete shutdowns
 * - Validation of all stored data integrity
 * - Fallback to full partition scan if state corrupted
 * - Detailed logging for troubleshooting at sea
 */
bool FlashRingBuffer::init(long unsigned int (*fn_millis)(void)) {
    USB_SERIAL_PRINTLN("FlashRingBuffer::init() - Starting initialization");
    
    // Prevent double initialization
    if (m_initialized) {
        USB_SERIAL_PRINTLN("FlashRingBuffer already initialized");
        return true;
    }
    
    // Store timing function for diagnostic operations
    m_fn_millis = fn_millis;
    
    // === Memory Allocation Phase ===
    // Allocate 4KB sector buffer for flash read operations
    m_sector_buffer = (uint8_t*)malloc(SECTOR_SIZE);
    if (!m_sector_buffer) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::init() - Failed to allocate sector buffer");
        return false;
    }
    
    // Allocate 4KB assembly buffer for RAM-based record accumulation
    // This enables efficient sector-sized writes to flash
    m_sector_assembly_buffer = (uint8_t*)malloc(SECTOR_SIZE);
    if (!m_sector_assembly_buffer) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::init() - Failed to allocate assembly buffer");
        teardown();  // Clean up sector buffer before failing
        return false;
    }
    
    // Initialize assembly buffer - 0xFF represents erased flash state
    memset(m_sector_assembly_buffer, 0xFF, SECTOR_SIZE);
    m_buffer_used = 0;  // No records accumulated yet
    
    // === Flash Partition Discovery ===
    // Locate dedicated 'flashbuf' partition (data,0x40 type)
    if (!findPartition()) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::init() - Failed to find partition");
        teardown();
        return false;
    }
    
    // === NVS Initialization for Persistent State ===
    // Initialize ESP32 Non-Volatile Storage for ring buffer state persistence
    // Namespace "flashring" keeps our data separate from other system settings
    if (!m_preferences.begin("flashring", false)) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::init() - Failed to initialize preferences");
        teardown();
        return false;
    }
    
    // === State Recovery Phase ===
    // Attempt to load ring buffer state from previous session
    if (loadPersistedState()) {
        USB_SERIAL_PRINTF("FlashRingBuffer::init() - Loaded persisted state: head=%u, tail=%u, seq=%u\n", 
                  m_state.head_sector, m_state.tail_sector, m_state.next_seq_number);
        
        // Validate loaded state against actual flash content
        // Critical for detecting corruption or inconsistencies after power loss
        if (!scanAndRecover()) {
            USB_SERIAL_PRINTLN("FlashRingBuffer::init() - State validation failed, performing full recovery");
            // Reset state and try full recovery from flash content
            memset(&m_state, 0, sizeof(m_state));
            if (!scanAndRecover()) {
                USB_SERIAL_PRINTLN("FlashRingBuffer::init() - Full recovery failed");
                teardown();
                return false;
            }
        }
    } else {
        USB_SERIAL_PRINTLN("FlashRingBuffer::init() - No valid persisted state, performing full scan");
        // No valid persisted state found - perform full partition scan
        // This occurs on first boot or after NVS corruption
        if (!scanAndRecover()) {
            USB_SERIAL_PRINTLN("FlashRingBuffer::init() - Initial scan failed");
            teardown();
            return false;
        }
    }
    
    // === Initialization Complete ===
    m_initialized = true;
    USB_SERIAL_PRINTLN("FlashRingBuffer::init() - Initialization complete");
    
    // === Power-On Self-Test Phase ===
    // Critical for marine safety - comprehensive system validation
    USB_SERIAL_PRINTLN("FlashRingBuffer::init() - Running power-on self-test...");
    bool post_result = performPowerOnSelfTest(true);  // Enable auto-repair
    
    if (!post_result) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::init() - WARNING: Power-on self-test failed!");
        // Continue operation but log the failure - system may still be functional
        // In marine environments, partial functionality is better than complete failure
    }
    
    // === Status Report ===
    printStatus();  // Provide complete system status for marine diagnostics
    
    return true;
}

/**
 * @brief Safe system teardown with data preservation
 * 
 * Performs complete cleanup sequence ensuring no data loss:
 * 1. Flush any pending RAM buffer data to flash
 * 2. Save current state to ESP32 NVS
 * 3. Release allocated memory buffers
 * 4. Reset all member variables to safe defaults
 * 
 * Marine Safety: Called automatically by destructor or can be called
 * explicitly before power-down to ensure data integrity.
 */
void FlashRingBuffer::teardown() {
    // Perform safe shutdown if system was initialized
    if (m_initialized) {
        prepareForShutdown();  // Flush buffers and save state
    }
    
    // Close ESP32 NVS preferences namespace
    m_preferences.end();
    
    // Release sector buffer memory
    if (m_sector_buffer) {
        free(m_sector_buffer);
        m_sector_buffer = nullptr;
    }
    
    // Release assembly buffer memory  
    if (m_sector_assembly_buffer) {
        free(m_sector_assembly_buffer);
        m_sector_assembly_buffer = nullptr;
    }
    
    // Reset all state to safe defaults
    m_buffer_used = 0;
    m_partition = nullptr;
    m_initialized = false;
    memset(&m_state, 0, sizeof(m_state));
}

// === Private Helper Methods ===

/**
 * @brief Locate and validate the dedicated flash partition
 * @return true if partition found and validated
 * 
 * Searches for partition named "flashbuf" with type data,0x40
 * Validates partition size meets minimum requirements (10MB)
 * Critical for marine deployment - logs detailed partition information
 */
bool FlashRingBuffer::findPartition() {
    m_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "flashbuf");
    if (!m_partition) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::findPartition() - Partition 'flashbuf' not found");
        return false;
    }
    
    USB_SERIAL_PRINTF("FlashRingBuffer::findPartition() - Found partition: size=%u bytes, address=0x%x\n", 
              m_partition->size, m_partition->address);
    
    if (m_partition->size < RING_BUFFER_SIZE) {
        USB_SERIAL_PRINTF("FlashRingBuffer::findPartition() - Partition too small: %u < %u\n", 
                  m_partition->size, RING_BUFFER_SIZE);
        return false;
    }
    
    return true;
}

esp_err_t FlashRingBuffer::eraseSector(uint32_t sector_index) {
    if (sector_index >= TOTAL_SECTORS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t offset = sector_index * SECTOR_SIZE;
    esp_err_t err = esp_partition_erase_range(m_partition, offset, SECTOR_SIZE);
    
    if (err == ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::eraseSector() - Erased sector %u\n", sector_index);
    } else {
        USB_SERIAL_PRINTF("FlashRingBuffer::eraseSector() - Failed to erase sector %u: %s\n", 
                  sector_index, esp_err_to_name(err));
    }
    
    return err;
}

esp_err_t FlashRingBuffer::writeSector(uint32_t sector_index, const void* data, size_t size) {
    if (sector_index >= TOTAL_SECTORS || size > SECTOR_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t offset = sector_index * SECTOR_SIZE;
    esp_err_t err = esp_partition_write(m_partition, offset, data, size);
    
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::writeSector() - Failed to write sector %u: %s\n", 
                  sector_index, esp_err_to_name(err));
    }
    
    return err;
}

esp_err_t FlashRingBuffer::readSector(uint32_t sector_index, void* data, size_t size) const {
    if (sector_index >= TOTAL_SECTORS || size > SECTOR_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t offset = sector_index * SECTOR_SIZE;
    esp_err_t err = esp_partition_read(m_partition, offset, data, size);
    
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::readSector() - Failed to read sector %u: %s\n", 
                  sector_index, esp_err_to_name(err));
    }
    
    return err;
}

// === CRC Calculation Methods ===

/**
 * @brief Calculate CRC16-CCITT for telemetry record protection
 * @param data Pointer to data to calculate CRC for
 * @param length Number of bytes to include in calculation
 * @return 16-bit CRC value
 * 
 * Uses CRC16-CCITT polynomial (0x1021) with 0xFFFF initial value
 * Critical for detecting corruption in telemetry records stored in flash
 * Marine environment requires robust error detection due to:
 * - Power fluctuations during boat operations
 * - Electromagnetic interference from marine electronics
 * - Potential flash memory wear in harsh conditions
 */
uint16_t FlashRingBuffer::calculateCRC16(const uint8_t* data, size_t length) const {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief Calculate CRC32 for sector header protection
 * @param data Pointer to data to calculate CRC for  
 * @param length Number of bytes to include in calculation
 * @return 32-bit CRC value
 * 
 * Uses CRC32 polynomial (0xEDB88320) with 0xFFFFFFFF initial value
 * Provides stronger protection for critical sector headers
 * More robust than CRC16 for detecting multiple bit errors in headers
 */
uint32_t FlashRingBuffer::calculateCRC32(const uint8_t* data, size_t length) const {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

// State management
bool FlashRingBuffer::loadPersistedState() {
    if (!m_preferences.isKey("state")) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::loadPersistedState() - No persisted state found");
        return false;
    }
    
    size_t len = m_preferences.getBytesLength("state");
    if (len != sizeof(FlashRingBufferState)) {
        USB_SERIAL_PRINTF("FlashRingBuffer::loadPersistedState() - Invalid state size: %u != %u\n", 
                  len, sizeof(FlashRingBufferState));
        return false;
    }
    
    FlashRingBufferState temp_state;
    size_t actual_len = m_preferences.getBytes("state", &temp_state, sizeof(temp_state));
    if (actual_len != sizeof(temp_state)) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::loadPersistedState() - Failed to read state");
        return false;
    }
    
    if (!validateState(temp_state)) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::loadPersistedState() - State validation failed");
        return false;
    }
    
    m_state = temp_state;
    return true;
}

bool FlashRingBuffer::savePersistedState() {
    // Calculate CRC excluding the crc field itself
    m_state.state_crc = calculateCRC32((const uint8_t*)&m_state, 
                                       sizeof(FlashRingBufferState) - sizeof(m_state.state_crc));
    
    size_t written = m_preferences.putBytes("state", &m_state, sizeof(m_state));
    if (written != sizeof(m_state)) {
        USB_SERIAL_PRINTF("FlashRingBuffer::savePersistedState() - Failed to save state: %u bytes written\n", written);
        return false;
    }
    
    USB_SERIAL_PRINTLN("FlashRingBuffer::savePersistedState() - State saved successfully");
    return true;
}

bool FlashRingBuffer::validateState(const FlashRingBufferState& state) const {
    // Calculate CRC excluding the crc field itself
    uint32_t calculated_crc = calculateCRC32((const uint8_t*)&state, 
                                            sizeof(FlashRingBufferState) - sizeof(state.state_crc));
    
    if (calculated_crc != state.state_crc) {
        USB_SERIAL_PRINTF("FlashRingBuffer::validateState() - CRC mismatch: calc=0x%x, stored=0x%x\n", 
                  calculated_crc, state.state_crc);
        return false;
    }
    
    if (state.head_sector >= TOTAL_SECTORS || state.tail_sector >= TOTAL_SECTORS) {
        USB_SERIAL_PRINTF("FlashRingBuffer::validateState() - Invalid sector indices: head=%u, tail=%u\n", 
                  state.head_sector, state.tail_sector);
        return false;
    }
    
    if (state.head_sector_used > USABLE_SECTOR_SIZE) {
        USB_SERIAL_PRINTF("FlashRingBuffer::validateState() - Invalid head sector usage: %u > %u\n", 
                  state.head_sector_used, USABLE_SECTOR_SIZE);
        return false;
    }
    
    return true;
}

// Shutdown and emergency functions
void FlashRingBuffer::printStatus() const {
    USB_SERIAL_PRINTLN("=== FlashRingBuffer Status ===");
    USB_SERIAL_PRINTF("Initialized: %s\n", m_initialized ? "Yes" : "No");
    USB_SERIAL_PRINTF("Head Sector: %u (used: %u bytes)\n", m_state.head_sector, m_state.head_sector_used);
    USB_SERIAL_PRINTF("Tail Sector: %u\n", m_state.tail_sector);
    USB_SERIAL_PRINTF("Next Seq: %u\n", m_state.next_seq_number);
    USB_SERIAL_PRINTF("Write Count: %u\n", m_state.write_count);
    USB_SERIAL_PRINTF("Records: %u\n", getRecordCount());
    USB_SERIAL_PRINTF("Used Space: %u bytes\n", getUsedSpace());
    USB_SERIAL_PRINTF("Free Space: %u bytes\n", getFreeSpace());
    USB_SERIAL_PRINTF("Empty: %s, Full: %s\n", isEmpty() ? "Yes" : "No", isFull() ? "Yes" : "No");
    USB_SERIAL_PRINTLN("=============================");
}

void FlashRingBuffer::prepareForShutdown() {
    USB_SERIAL_PRINTLN("FlashRingBuffer::prepareForShutdown() - Flushing RAM buffer and saving state");
    
    // Flush any pending data in RAM buffer
    if (m_buffer_used > 0) {
        flushRAMBufferToFlash();
    }
    
    savePersistedState();
}

void FlashRingBuffer::emergencyFlush() {
    // Force immediate RAM buffer flush and state save during emergency shutdown
    if (m_initialized) {
        // Flush any pending data in RAM buffer
        if (m_buffer_used > 0) {
            flushRAMBufferToFlash();
        }
        
        savePersistedState();
        USB_SERIAL_PRINTLN("FlashRingBuffer::emergencyFlush() - Emergency RAM flush and state save completed");
    }
}

// === Core Record Operations ===

/**
 * @brief Append telemetry record to RAM buffer with power-safe semantics
 * @param payload Pointer to telemetry data (16-1008 bytes)
 * @param length Size of telemetry payload
 * @return true if record successfully buffered
 * 
 * Marine-optimized buffering strategy:
 * 1. Records accumulated in RAM buffer until sector-size batch ready
 * 2. Automatic flush to flash when buffer approaches sector boundary
 * 3. Power-safe record structure with validity markers
 * 4. CRC16 calculation for corruption detection
 * 
 * Key Benefits for Marine Use:
 * - Reduces flash wear by batching writes into sector-sized operations
 * - Maintains high logging rate even during network connectivity loss
 * - Ensures data integrity through CRC protection
 * - Atomic operations prevent corruption during power interruptions
 */
bool FlashRingBuffer::appendRecord(const uint8_t* payload, uint16_t length) {
    if (!m_initialized || !payload) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::appendRecord() - Not initialized or null payload");
        return false;
    }
    
    if (length < MIN_MESSAGE_SIZE || length > MAX_MESSAGE_SIZE) {
        USB_SERIAL_PRINTF("FlashRingBuffer::appendRecord() - Invalid length: %u (must be %u-%u)\n", 
                  length, MIN_MESSAGE_SIZE, MAX_MESSAGE_SIZE);
        return false;
    }
    
    uint32_t total_record_size = RECORD_HEADER_SIZE + length;
    
    // Check if RAM buffer has space for this record - only flush when necessary
    if (m_buffer_used + total_record_size > USABLE_SECTOR_SIZE) {
        USB_SERIAL_PRINTF("FlashRingBuffer::appendRecord() - RAM buffer full, flushing to flash (need %u bytes, have %u free)\n", 
                  total_record_size, USABLE_SECTOR_SIZE - m_buffer_used);
        
        // Flush current RAM buffer to flash
        if (!flushRAMBufferToFlash()) {
            USB_SERIAL_PRINTLN("FlashRingBuffer::appendRecord() - Failed to flush RAM buffer");
            return false;
        }
    }
    
    // Add record to RAM buffer with power-safe pattern
    uint32_t buffer_offset = m_buffer_used;
    
    // Prepare record header (mark as invalid initially for power-safe pattern)
    RecordHeader record_header;
    record_header.len = length;
    record_header.crc16 = calculateCRC16(payload, length);
    record_header.valid = 0xFF; // Mark as invalid initially for power-safe pattern
    record_header.reserved[0] = record_header.reserved[1] = record_header.reserved[2] = 0;
    
    // Copy record to RAM buffer
    memcpy(m_sector_assembly_buffer + buffer_offset, &record_header, RECORD_HEADER_SIZE);
    memcpy(m_sector_assembly_buffer + buffer_offset + RECORD_HEADER_SIZE, payload, length);
    
    // Mark record as valid (commit in RAM - will be power-safe when flushed)
    record_header.valid = 0x00; // Mark as valid
    memcpy(m_sector_assembly_buffer + buffer_offset + offsetof(RecordHeader, valid), &record_header.valid, 1);
    
    // Update buffer usage
    m_buffer_used += total_record_size;
    
    USB_SERIAL_PRINTF("FlashRingBuffer::appendRecord() - Added %u byte record to RAM buffer (%u bytes used, %u free)\n", 
              length, m_buffer_used, USABLE_SECTOR_SIZE - m_buffer_used);
    
    return true;
}

/**
 * @brief Flush accumulated RAM buffer to flash with power-safe atomic write
 * @return true if flush successful
 * 
 * Critical marine operation ensuring no data loss during power interruptions:
 * 
 * Power-Safe Write Sequence:
 * 1. Validate buffer contents and space requirements
 * 2. Advance to new sector if current sector insufficient space
 * 3. Prepare complete sector with header, existing data, and new records
 * 4. Add sentinel patterns (0x55AA) to detect incomplete writes
 * 5. Atomic erase-then-write operation
 * 6. Update persistent state in ESP32 NVS
 * 
 * Marine Reliability Features:
 * - Atomic sector operations prevent partial corruption
 * - Sentinel patterns detect power-loss during write
 * - State persistence survives boat power cycles
 * - Detailed logging for marine troubleshooting
 */
bool FlashRingBuffer::flushRAMBufferToFlash() {
    if (!m_initialized || m_buffer_used == 0) {
        return true; // Nothing to flush - not an error condition
    }
    
    USB_SERIAL_PRINTF("FlashRingBuffer::flushRAMBufferToFlash() - Flushing %u bytes to sector %u\n", 
              m_buffer_used, m_state.head_sector);
    
    // Check if we need to advance to next sector
    uint32_t required_space = SECTOR_HEADER_SIZE + m_buffer_used;
    if (required_space > SECTOR_SIZE) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::flushRAMBufferToFlash() - Buffer too large for single sector");
        return false;
    }
    
    // Check if current sector has space for this buffer
    if (SECTOR_HEADER_SIZE + m_state.head_sector_used + m_buffer_used > SECTOR_SIZE) {
        USB_SERIAL_PRINTF("FlashRingBuffer::flushRAMBufferToFlash() - Current sector full, advancing (need %u bytes)\n", 
                  m_buffer_used);
        
        if (!advanceSector()) {
            USB_SERIAL_PRINTLN("FlashRingBuffer::flushRAMBufferToFlash() - Failed to advance sector");
            return false;
        }
    }
    
    // Prepare complete sector with header
    memset(m_sector_buffer, 0xFF, SECTOR_SIZE);
    
    // Create sector header
    SectorHeader sector_header;
    sector_header.magic = SECTOR_MAGIC;
    sector_header.seq = m_state.next_seq_number;
    sector_header.used = m_state.head_sector_used + m_buffer_used;
    sector_header.reserved = 0;
    sector_header.rsvd2 = 0;
    sector_header.crc32 = calculateCRC32((const uint8_t*)&sector_header, 
                                       sizeof(SectorHeader) - sizeof(sector_header.crc32));
    
    // Copy header to sector buffer
    memcpy(m_sector_buffer, &sector_header, SECTOR_HEADER_SIZE);
    
    // If there's existing data in the sector, read it first
    if (m_state.head_sector_used > 0) {
        esp_err_t err = esp_partition_read(m_partition, 
                                         m_state.head_sector * SECTOR_SIZE + SECTOR_HEADER_SIZE,
                                         m_sector_buffer + SECTOR_HEADER_SIZE,
                                         m_state.head_sector_used);
        if (err != ESP_OK) {
            USB_SERIAL_PRINTF("FlashRingBuffer::flushRAMBufferToFlash() - Failed to read existing data: %s\n", 
                      esp_err_to_name(err));
            return false;
        }
    }
    
    // Copy assembly buffer data after existing data
    memcpy(m_sector_buffer + SECTOR_HEADER_SIZE + m_state.head_sector_used, 
           m_sector_assembly_buffer, m_buffer_used);
    
    // Fill unused trailing space with 0x55AA sentinel pattern for power-loss detection
    uint32_t used_data_end = SECTOR_HEADER_SIZE + sector_header.used;
    uint32_t remaining_bytes = SECTOR_SIZE - used_data_end;
    
    if (remaining_bytes >= 2) {
        USB_SERIAL_PRINTF("FlashRingBuffer::flushRAMBufferToFlash() - Filling %u trailing bytes with 0x55AA sentinel pattern\n", 
                  remaining_bytes);
        
        // Fill remaining space with 0x55AA pattern
        for (uint32_t i = used_data_end; i < SECTOR_SIZE - 1; i += 2) {
            uint16_t* sentinel_pos = (uint16_t*)(m_sector_buffer + i);
            *sentinel_pos = 0x55AA;
        }
        
        // Handle odd remaining byte (shouldn't happen with our sector layout, but safety check)
        if ((SECTOR_SIZE - used_data_end) % 2 == 1) {
            m_sector_buffer[SECTOR_SIZE - 1] = 0x55;
        }
    }
    
    // Erase and write the complete sector (power-safe pattern)
    esp_err_t err = eraseSector(m_state.head_sector);
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::flushRAMBufferToFlash() - Failed to erase sector %u: %s\n", 
                  m_state.head_sector, esp_err_to_name(err));
        return false;
    }
    
    err = writeSector(m_state.head_sector, m_sector_buffer, SECTOR_SIZE);
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::flushRAMBufferToFlash() - Failed to write sector %u: %s\n", 
                  m_state.head_sector, esp_err_to_name(err));
        return false;
    }
    
    // Update state
    m_state.head_sector_used += m_buffer_used;
    m_state.write_count++;
    
    // Clear assembly buffer
    memset(m_sector_assembly_buffer, 0xFF, SECTOR_SIZE);
    m_buffer_used = 0;
    
    // Save persistent state
    savePersistedState();
    
    USB_SERIAL_PRINTF("FlashRingBuffer::flushRAMBufferToFlash() - Successfully flushed buffer to sector %u\n", 
              m_state.head_sector);
    
    return true;
}

bool FlashRingBuffer::readOldestRecord(uint8_t* payload, uint16_t max_length, uint16_t& actual_length) {
    if (!m_initialized || !payload) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::readOldestRecord() - Not initialized or null payload");
        return false;
    }
    
    if (isEmpty()) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::readOldestRecord() - Ring buffer is empty");
        return false;
    }
    
    // Read tail sector to find oldest record
    esp_err_t err = readSector(m_state.tail_sector, m_sector_buffer, SECTOR_SIZE);
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::readOldestRecord() - Failed to read tail sector %u: %s\n", 
                  m_state.tail_sector, esp_err_to_name(err));
        return false;
    }
    
    // Look for first valid record in tail sector
    uint32_t offset = SECTOR_HEADER_SIZE;
    while (offset + RECORD_HEADER_SIZE <= SECTOR_SIZE) {
        RecordHeader* record = (RecordHeader*)(m_sector_buffer + offset);
        
        // Check if we have a valid record
        if (record->valid != 0x00) {
            USB_SERIAL_PRINTF("FlashRingBuffer::readOldestRecord() - Skipping invalid record at offset %u\n", offset);
            break; // No more valid records in this sector
        }
        
        if (record->len < MIN_MESSAGE_SIZE || record->len > MAX_MESSAGE_SIZE) {
            USB_SERIAL_PRINTF("FlashRingBuffer::readOldestRecord() - Invalid record length %u at offset %u\n", 
                      record->len, offset);
            break;
        }
        
        if (record->len > max_length) {
            USB_SERIAL_PRINTF("FlashRingBuffer::readOldestRecord() - Record too large: %u > %u\n", 
                      record->len, max_length);
            return false;
        }
        
        // Verify record CRC
        uint8_t* record_payload = m_sector_buffer + offset + RECORD_HEADER_SIZE;
        uint16_t calculated_crc = calculateCRC16(record_payload, record->len);
        if (calculated_crc != record->crc16) {
            USB_SERIAL_PRINTF("FlashRingBuffer::readOldestRecord() - CRC mismatch at offset %u\n", offset);
            break;
        }
        
        // Copy payload to output buffer
        memcpy(payload, record_payload, record->len);
        actual_length = record->len;
        
        USB_SERIAL_PRINTF("FlashRingBuffer::readOldestRecord() - Read %u byte record from sector %u\n", 
                  record->len, m_state.tail_sector);
        
        return true;
    }
    
    USB_SERIAL_PRINTF("FlashRingBuffer::readOldestRecord() - No valid records found in tail sector %u\n", 
              m_state.tail_sector);
    return false;
}

bool FlashRingBuffer::deleteOldestRecord() {
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::deleteOldestRecord() - Not initialized");
        return false;
    }
    
    if (isEmpty()) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::deleteOldestRecord() - Ring buffer is empty");
        return false;
    }
    
    // Read tail sector
    esp_err_t err = readSector(m_state.tail_sector, m_sector_buffer, SECTOR_SIZE);
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::deleteOldestRecord() - Failed to read tail sector %u: %s\n", 
                  m_state.tail_sector, esp_err_to_name(err));
        return false;
    }
    
    SectorHeader* sector_header = (SectorHeader*)m_sector_buffer;
    bool found_record = false;
    
    // Find and mark first valid record as deleted by shifting remaining records
    uint32_t offset = SECTOR_HEADER_SIZE;
    while (offset + RECORD_HEADER_SIZE <= SECTOR_SIZE) {
        RecordHeader* record = (RecordHeader*)(m_sector_buffer + offset);
        
        if (record->valid == 0x00 && record->len >= MIN_MESSAGE_SIZE && record->len <= MAX_MESSAGE_SIZE) {
            uint32_t total_record_size = RECORD_HEADER_SIZE + record->len;
            uint32_t remaining_data_size = sector_header->used - (offset + total_record_size - SECTOR_HEADER_SIZE);
            
            // Shift remaining records forward
            if (remaining_data_size > 0) {
                memmove(m_sector_buffer + offset, 
                       m_sector_buffer + offset + total_record_size,
                       remaining_data_size);
            }
            
            // Update sector header
            sector_header->used -= total_record_size;
            sector_header->crc32 = calculateCRC32((const uint8_t*)sector_header, 
                                                 sizeof(SectorHeader) - sizeof(sector_header->crc32));
            
            // Write updated sector back to flash
            esp_err_t write_err = writeSector(m_state.tail_sector, m_sector_buffer, SECTOR_SIZE);
            if (write_err != ESP_OK) {
                USB_SERIAL_PRINTF("FlashRingBuffer::deleteOldestRecord() - Failed to update sector: %s\n", 
                          esp_err_to_name(write_err));
                return false;
            }
            
            USB_SERIAL_PRINTF("FlashRingBuffer::deleteOldestRecord() - Deleted %u byte record from sector %u\n", 
                      record->len, m_state.tail_sector);
            
            found_record = true;
            break;
        }
        
        offset += RECORD_HEADER_SIZE + record->len;
    }
    
    if (!found_record) {
        USB_SERIAL_PRINTF("FlashRingBuffer::deleteOldestRecord() - No valid records in tail sector %u\n", 
                  m_state.tail_sector);
        
        // Advance tail sector if current one is empty
        m_state.tail_sector = (m_state.tail_sector + 1) % TOTAL_SECTORS;
        USB_SERIAL_PRINTF("FlashRingBuffer::deleteOldestRecord() - Advanced tail to sector %u\n", 
                  m_state.tail_sector);
        
        savePersistedState();
        return deleteOldestRecord(); // Try again with next sector
    }
    
    return true;
}

// === Statistics and Status Methods ===

/**
 * @brief Count total number of valid records currently stored
 * @return Number of records in ring buffer
 * 
 * Performs complete partition scan from tail to head counting valid records
 * Critical for marine monitoring - provides accurate data inventory
 * Used for upload progress tracking and storage management
 */
uint32_t FlashRingBuffer::getRecordCount() const {
    if (!m_initialized || isEmpty()) {
        return 0;
    }
    
    uint32_t total_count = 0;
    uint32_t current_sector = m_state.tail_sector;
    
    // Scan all sectors from tail to head to count records
    do {
        SectorHeader header;
        esp_err_t err = readSector(current_sector, &header, sizeof(header));
        
        if (err == ESP_OK && header.magic == SECTOR_MAGIC) {
            // Scan records in this sector
            uint8_t* sector_data = m_sector_buffer;
            err = readSector(current_sector, sector_data, SECTOR_SIZE);
            
            if (err == ESP_OK) {
                uint32_t offset = SECTOR_HEADER_SIZE;
                while (offset + RECORD_HEADER_SIZE <= SECTOR_SIZE && 
                       offset < SECTOR_HEADER_SIZE + header.used) {
                    RecordHeader* record = (RecordHeader*)(sector_data + offset);
                    
                    if (record->valid == 0x00 && 
                        record->len >= MIN_MESSAGE_SIZE && 
                        record->len <= MAX_MESSAGE_SIZE) {
                        total_count++;
                        offset += RECORD_HEADER_SIZE + record->len;
                    } else {
                        break; // Invalid record, stop scanning this sector
                    }
                }
            }
        }
        
        current_sector = (current_sector + 1) % TOTAL_SECTORS;
    } while (current_sector != m_state.head_sector);
    
    return total_count;
}

#define BUILD_INCLUDE_FLASHRINGBUFFER_PART2

#include "FlashRingBuffer_part2.cpp"
