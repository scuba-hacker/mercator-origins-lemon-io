/**
 * @file FlashRingBuffer.h
 * @brief Power-safe flash ring buffer for persistent telemetry storage
 * 
 * This class implements a raw flash ring buffer that provides persistent storage
 * for telemetry data in marine environments where power loss is common and network
 * connectivity is intermittent. The buffer survives power cycles and can store
 * data during extended periods without connectivity (up to 8+ hours at sea).
 * 
 * Key Features:
 * - Power-loss safe: Uses atomic write operations with validity markers
 * - Variable message sizes: 16-1008 bytes per message
 * - Raw flash access: No filesystem overhead, deterministic performance
 * - Wear leveling: Ring buffer design distributes writes across sectors
 * - CRC protection: Both sector headers and records are CRC protected
 * - Recovery: Automatic recovery from incomplete writes and corruption
 * - Diagnostics: Comprehensive self-test and diagnostic capabilities
 * 
 * Design Philosophy:
 * - Non-intrusive: Can be enabled/disabled to fall back to PSRAM buffer
 * - Battle-tested: Extensive testing hooks and diagnostic capabilities
 * - Marine-ready: Designed for harsh marine environments with power interruptions
 * 
 * Flash Layout:
 * - 10MB dedicated partition (data,0x40)
 * - 2560 sectors of 4KB each
 * - Each sector: [16-byte header][variable records][unused space]
 * - Each record: [8-byte header][payload][validity marker]
 * 
 * @author Generated for Mercator Origins dive computer system
 * @version 1.0
 * @date 2024
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

// Forward declarations
class FlashRingBuffer;

/**
 * @brief Sector header structure - written at the start of each 4KB flash sector
 * 
 * This structure is written at the beginning of each sector to identify valid
 * sectors and track the ring buffer state. The header is protected by CRC32
 * and uses atomic write semantics for power-loss safety.
 * 
 * Layout: 16 bytes total, packed for direct flash storage
 * Power-loss safety: Written in one atomic operation before any records
 */
struct SectorHeader {
    uint32_t magic;      // 0x42474F4C "LOGB" (little endian) - sector validity marker
    uint32_t seq;        // Monotonically increasing sector sequence number for ring ordering
    uint16_t used;       // Bytes used in this sector (after header) - for recovery scanning
    uint16_t reserved;   // Reserved for future use (alignment/expansion)
    uint32_t crc32;      // CRC32 of sector header (excluding this field) - corruption detection
    uint32_t rsvd2;      // Additional reserved space for future expansion
} __attribute__((packed));

/**
 * @brief Record header structure - prefixes each telemetry message in flash
 * 
 * Each telemetry record consists of this header followed by the payload data.
 * The header provides length information, corruption detection, and power-loss
 * safety through an atomic validity marker.
 * 
 * Write sequence for power-loss safety:
 * 1. Write length and CRC (with valid=0xFF, meaning incomplete)
 * 2. Write payload data
 * 3. Atomically write valid=0x00 to commit the record
 * 
 * Layout: 8 bytes total, packed for efficient flash storage
 */
struct RecordHeader {
    uint16_t len;        // Payload length (16-1008 bytes) - variable message size support
    uint16_t crc16;      // CRC16 of payload data - corruption detection
    uint8_t  valid;      // 0x00 = complete record; 0xFF = incomplete/uncommitted
    uint8_t  reserved[3]; // Padding for alignment and future expansion
} __attribute__((packed));

/**
 * @brief Persistent state structure stored in ESP32's EEPROM/Preferences
 * 
 * This structure maintains the ring buffer's critical state across power cycles.
 * It's stored in ESP32's non-volatile storage (Preferences) and protected by CRC.
 * The state is updated after successful flash operations and loaded on startup.
 * 
 * Recovery strategy:
 * - If state is invalid/corrupted: Perform full partition scan to rebuild state
 * - If state is valid: Use directly and verify consistency with flash content
 * 
 * Layout: 24 bytes total, CRC-protected for integrity
 */
struct FlashRingBufferState {
    uint32_t head_sector;        // Current write sector (where new records are appended)
    uint32_t tail_sector;        // Current read sector (oldest unread records)
    uint32_t head_sector_used;   // Bytes used in current head sector (for append positioning)
    uint32_t next_seq_number;    // Next sequence number for sector headers (monotonic)
    uint32_t write_count;        // Total writes performed (wear leveling tracking)
    uint32_t state_crc;          // CRC32 of this structure (excluding this field)
} __attribute__((packed));

/**
 * @brief Comprehensive diagnostic results structure
 * 
 * This structure contains detailed diagnostic information about the flash
 * ring buffer's health and integrity. Used by self-test routines and
 * diagnostic functions to report system status and identify issues.
 * 
 * Critical for marine environments where system reliability is paramount.
 * Provides actionable information for troubleshooting and maintenance.
 * 
 * Layout: Comprehensive diagnostic data for system health assessment
 */
struct FlashDiagnostics {
    // Partition and hardware health
    bool partition_found;           // Flash partition successfully located
    bool partition_accessible;      // Partition can be read/written
    uint32_t total_sectors;         // Total sectors in partition
    uint32_t valid_sectors;         // Sectors with valid headers and structure
    uint32_t corrupted_sectors;     // Sectors with corruption detected
    uint32_t empty_sectors;         // Unused sectors (all 0xFF)
    
    // Record-level health
    uint32_t total_records;         // Total records found in partition
    uint32_t valid_records;         // Records with valid CRC and structure
    uint32_t corrupted_records;     // Records with detected corruption
    
    // System consistency checks
    uint32_t state_load_success;    // Persistent state loaded successfully
    uint32_t head_tail_consistency; // Head/tail pointers are consistent
    uint32_t sequence_consistency;  // Sector sequence numbers are valid
    
    // Auto-repair capabilities
    bool auto_repair_attempted;     // Automatic repair was attempted
    bool auto_repair_successful;    // Automatic repair succeeded
    
    // Performance metrics
    uint32_t diagnostic_duration_ms; // Time taken for diagnostic scan
} __attribute__((packed));

/**
 * @class FlashRingBuffer
 * @brief Power-safe raw flash ring buffer for marine telemetry storage
 * 
 * This class provides a robust, power-loss safe storage solution for telemetry
 * data in marine environments. It directly manages flash sectors without filesystem
 * overhead, providing deterministic performance and maximum reliability.
 * 
 * Key Design Principles:
 * - Power-loss safety: All operations are atomic with validity markers
 * - Raw flash management: Direct sector control for predictable behavior
 * - Variable record sizes: Efficient storage of 16-1008 byte messages
 * - Ring buffer design: Automatic wear leveling across flash sectors
 * - Recovery capabilities: Comprehensive error detection and repair
 * 
 * Marine Use Case:
 * - Store dive telemetry when network unavailable (8+ hours)
 * - Survive power cycles during equipment shutdown/startup
 * - Spool stored data when connectivity returns
 * - Continue logging while uploading backlog
 * 
 * Thread Safety: This class is NOT thread-safe. External synchronization required.
 * Memory Usage: ~8KB RAM for sector buffers plus minimal state
 */
class FlashRingBuffer {
public:
    // === Flash Hardware Constants ===
    static const uint32_t SECTOR_SIZE = 4096;                              // ESP32 flash erase unit (4KB)
    static const uint32_t RING_BUFFER_SIZE = 10 * 1024 * 1024;            // 10MB total partition size
    static const uint32_t TOTAL_SECTORS = RING_BUFFER_SIZE / SECTOR_SIZE;  // 2560 sectors total
    static const uint32_t SECTOR_HEADER_SIZE = sizeof(SectorHeader);       // 16 bytes per sector header
    static const uint32_t RECORD_HEADER_SIZE = sizeof(RecordHeader);       // 8 bytes per record header
    static const uint32_t SECTOR_MAGIC = 0x42474F4C;                       // "LOGB" magic number
    
    // === Message Size Constraints ===
    // Based on marine telemetry requirements and flash sector efficiency
    static const uint16_t MIN_MESSAGE_SIZE = 16;                           // Minimum telemetry message size
    static const uint16_t MAX_MESSAGE_SIZE = 1008;                         // Maximum message size (allows 4 messages per sector)
    static const uint32_t MAX_RECORD_SIZE = MAX_MESSAGE_SIZE + RECORD_HEADER_SIZE; // Total record size including header
    static const uint32_t USABLE_SECTOR_SIZE = SECTOR_SIZE - SECTOR_HEADER_SIZE;   // 4080 bytes usable per sector

private:
    // === Core Flash Management ===
    const esp_partition_t* m_partition;    // ESP32 flash partition handle (data,0x40 type)
    FlashRingBufferState m_state;          // Ring buffer state (head/tail pointers, counters)
    bool m_initialized;                    // Initialization status flag
    Preferences m_preferences;             // ESP32 NVS for persistent state storage
    
    // === Debug and Timing Support ===
    long unsigned int (*m_fn_millis)(void); // Function pointer for millis() timing (injected dependency)

    // === RAM Buffers for Flash Operations ===
    // ESP32 flash requires sector-aligned operations (4KB erase units)
    uint8_t* m_sector_buffer;              // 4KB buffer for sector read operations
    uint8_t* m_sector_assembly_buffer;     // 4KB buffer for assembling new sectors before flash write
    uint32_t m_buffer_used;                // Bytes currently used in assembly buffer

    // === Private Methods ===
    
    // --- Flash Partition Management ---
    bool findPartition();                                                   // Locate dedicated flash partition by name
    esp_err_t eraseSector(uint32_t sector_index);                         // Erase 4KB sector (all bits set to 1)
    esp_err_t writeSector(uint32_t sector_index, const void* data, size_t size); // Write data to sector
    esp_err_t readSector(uint32_t sector_index, void* data, size_t size) const;  // Read data from sector
    
    // --- Persistent State Management ---
    bool loadPersistedState();                                            // Load ring state from ESP32 NVS
    bool savePersistedState();                                            // Save ring state to ESP32 NVS  
    bool validateState(const FlashRingBufferState& state) const;          // Verify state CRC and consistency
    
    // --- Recovery and Validation ---
    bool scanAndRecover();                                                 // Full partition scan to rebuild state
    uint32_t scanSectorRecords(uint32_t sector_index, uint32_t claimed_used); // Count valid records in sector
    bool validateSectorHeader(const SectorHeader& header) const;           // Check sector header CRC and magic
    bool validateSectorComplete(uint32_t sector_index) const;              // Verify all records in sector are valid
    uint16_t calculateCRC16(const uint8_t* data, size_t length) const;     // CRC16-CCITT for record protection
    uint32_t calculateCRC32(const uint8_t* data, size_t length) const;     // CRC32 for sector header protection
    
    // --- Ring Buffer Management ---
    bool advanceSector();                                                  // Move to next sector when current full
    bool sectorHasSpace(uint32_t required_bytes) const;                    // Check if current sector has space
    uint32_t getRingDistance() const;                                      // Calculate sectors used in ring
    void printDiagnosticReport(const FlashDiagnostics& diag) const;       // Print detailed diagnostic results
    
    // --- Buffered Flash Operations ---
    bool flushRAMBufferToFlash();                                          // Write assembled RAM buffer to flash sector

public:
    // === Construction and Destruction ===
    FlashRingBuffer();                                                      // Constructor - initializes member variables
    ~FlashRingBuffer();                                                     // Destructor - cleans up allocated buffers
    
    // === Initialization and Teardown ===
    /**
     * @brief Initialize the flash ring buffer system
     * @param fn_millis Function pointer to millis() for timing operations
     * @return true if initialization successful, false on failure
     * 
     * Performs complete system initialization:
     * - Locates flash partition
     * - Allocates RAM buffers  
     * - Loads persistent state from NVS
     * - Performs power-on self-test with optional auto-repair
     * - Validates ring buffer consistency
     */
    bool init(long unsigned int (*fn_millis)(void));
    
    /**
     * @brief Clean shutdown of flash ring buffer
     * 
     * Performs safe shutdown sequence:
     * - Flushes any pending RAM buffer writes
     * - Saves current state to NVS
     * - Releases allocated memory
     * - Marks system as uninitialized
     */
    void teardown();
    
    /**
     * @brief Check if ring buffer is properly initialized
     * @return true if initialized and ready for use
     */
    bool isInitialized() const { return m_initialized; }
    
    // === Core Storage Operations ===
    /**
     * @brief Append new telemetry record to ring buffer
     * @param payload Pointer to telemetry data (16-1008 bytes)
     * @param length Length of payload data
     * @return true if record successfully stored, false on failure
     * 
     * Power-safe append operation:
     * - Validates payload size and content
     * - Calculates CRC16 for data integrity
     * - Uses atomic write with validity marker
     * - Advances to next sector if current sector full
     * - Updates persistent state after successful write
     */
    bool appendRecord(const uint8_t* payload, uint16_t length);
    
    /**
     * @brief Read oldest unread record from ring buffer
     * @param payload Buffer to receive record data
     * @param max_length Maximum size of receive buffer
     * @param actual_length Returns actual record length
     * @return true if record read successfully, false if empty or error
     * 
     * Non-destructive read - record remains in buffer until deleteOldestRecord()
     */
    bool readOldestRecord(uint8_t* payload, uint16_t max_length, uint16_t& actual_length);
    
    /**
     * @brief Remove oldest record from ring buffer
     * @return true if record deleted, false if buffer empty or error
     * 
     * Used after successful MQTT upload to free space.
     * Advances tail pointer and reclaims sectors when empty.
     */
    bool deleteOldestRecord();
    
    // === Status and Statistics ===
    uint32_t getRecordCount() const;                                        // Total records currently stored
    uint32_t getUsedSpace() const;                                          // Bytes of flash currently used
    uint32_t getFreeSpace() const;                                          // Bytes of flash available
    bool isEmpty() const;                                                   // True if no records stored
    bool isFull() const;                                                    // True if no space for new records
    uint32_t getWriteCount() const { return m_state.write_count; }          // Total writes performed (wear tracking)
    
    // === Debug and Diagnostics ===
    /**
     * @brief Print comprehensive ring buffer status to USB serial
     * 
     * Outputs current state including:
     * - Head/tail positions and usage
     * - Record counts and space utilization  
     * - Write counts and wear statistics
     * - Health and error indicators
     */
    void printStatus() const;
    
    /**
     * @brief Perform basic self-test operations
     * @return true if all tests pass, false on failure
     * 
     * Quick validation of core functionality:
     * - Partition accessibility
     * - State consistency
     * - Basic read/write operations
     */
    bool performSelfTest();
    
    /**
     * @brief Comprehensive power-on diagnostics with auto-repair
     * @param auto_repair Enable automatic repair of detected issues
     * @return true if system healthy or successfully repaired
     * 
     * Thorough startup validation:
     * - Full partition scan
     * - Corruption detection
     * - Automatic repair attempts
     * - State reconstruction if needed
     */
    bool performPowerOnSelfTest(bool auto_repair = true);
    
    /**
     * @brief Extended diagnostic scan of entire partition
     * @return true if diagnostics complete (not necessarily healthy)
     * 
     * Deep analysis for troubleshooting:
     * - Sector-by-sector validation
     * - Record integrity checking
     * - Detailed corruption reporting
     * - Performance metrics
     */
    bool performExtendedDiagnostics();
    
    bool performDeepSectorValidation();                                     // Validate every sector and record
    bool performPowerLossRecoveryTest();                                    // Test recovery from simulated power loss
    bool performStressTest(uint32_t num_records = 1000);                    // Stress test with high write load
    
    // === Safe Shutdown Operations ===
    /**
     * @brief Prepare system for safe power-down
     * 
     * Marine safety procedure before cutting power:
     * - Flush all pending writes
     * - Save state to NVS
     * - Mark system safe for power-off
     * - Disable further write operations
     */
    void prepareForShutdown();
    
    /**
     * @brief Emergency flush of all pending data
     * 
     * Force write of any buffered data to flash.
     * Used in critical shutdown scenarios.
     */
    void emergencyFlush();
    
    // === Reset and Recovery Operations ===
    bool factoryReset();                                                    // Complete system reset to factory state
    bool clearAllData();                                                    // Erase all stored records (keep structure)
    bool repairCorruption();                                                // Attempt to repair detected corruption
    
    // === Testing and Failure Injection (TESTING_MODE only) ===
    /**
     * @brief Failure injection methods for comprehensive testing
     * 
     * These methods are only available when TESTING_MODE is defined.
     * Used to validate system robustness and recovery capabilities
     * in controlled test environments.
     * 
     * WARNING: Never use these methods in production code!
     * They intentionally corrupt data and can cause system failures.
     */
    #ifdef TESTING_MODE
    bool injectSectorCorruption(uint32_t sector_index);                     // Corrupt specific sector for testing
    bool corruptPersistedState();                                           // Corrupt NVS state for recovery testing
    bool simulateIncompleteWrite();                                         // Simulate power-loss during write
    bool corruptRingPointers();                                             // Corrupt head/tail pointers
    bool acceleratedWearTest(uint32_t cycles);                              // Rapid wear testing
    bool injectRandomCorruption(uint32_t num_sectors);                      // Random corruption injection
    bool simulatePartitionFailure();                                        // Simulate flash partition failure
    bool injectCRCCorruption(uint32_t sector_index);                        // Corrupt CRC values for testing
    void enableFailureInjection() { USB_SERIAL_PRINTLN("WARNING: Failure injection methods enabled for testing"); }
    #endif
};

#endif // FLASH_RING_BUFFER_H