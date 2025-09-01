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

// Sector header structure (16 bytes, aligned)
struct SectorHeader {
    uint32_t magic;      // 0x42474F4C "LOGB" (little endian)
    uint32_t seq;        // Monotonically increasing sector sequence number
    uint16_t used;       // Bytes used in this sector (after header)
    uint16_t reserved;   // Reserved for future use
    uint32_t crc32;      // CRC32 of sector header (excluding this field)
    uint32_t rsvd2;      // Additional reserved space
} __attribute__((packed));

// Record structure for variable-length messages
struct RecordHeader {
    uint16_t len;        // Payload length (16-928 bytes)
    uint16_t crc16;      // CRC16 of payload
    uint8_t  valid;      // 0x00 = complete; 0xFF = not yet committed
    uint8_t  reserved[3]; // Padding for alignment
} __attribute__((packed));

// Persistent state structure for EEPROM storage
struct FlashRingBufferState {
    uint32_t head_sector;
    uint32_t tail_sector;
    uint32_t head_sector_used;
    uint32_t next_seq_number;
    uint32_t write_count;        // Total writes for wear tracking
    uint32_t state_crc;          // CRC of this structure (excluding this field)
} __attribute__((packed));

// Diagnostic results structure
struct FlashDiagnostics {
    bool partition_found;
    bool partition_accessible;
    uint32_t total_sectors;
    uint32_t valid_sectors;
    uint32_t corrupted_sectors;
    uint32_t empty_sectors;
    uint32_t total_records;
    uint32_t valid_records;
    uint32_t corrupted_records;
    uint32_t state_load_success;
    uint32_t head_tail_consistency;
    uint32_t sequence_consistency;
    bool auto_repair_attempted;
    bool auto_repair_successful;
    uint32_t diagnostic_duration_ms;
} __attribute__((packed));

class FlashRingBuffer {
public:
    static const uint32_t SECTOR_SIZE = 4096;
    static const uint32_t RING_BUFFER_SIZE = 10 * 1024 * 1024; // 10MB
    static const uint32_t TOTAL_SECTORS = RING_BUFFER_SIZE / SECTOR_SIZE; // 2560 sectors
    static const uint32_t SECTOR_HEADER_SIZE = sizeof(SectorHeader);
    static const uint32_t RECORD_HEADER_SIZE = sizeof(RecordHeader);
    static const uint32_t SECTOR_MAGIC = 0x42474F4C; // "LOGB"
    
    // Message size constraints
    static const uint16_t MIN_MESSAGE_SIZE = 16;
    static const uint16_t MAX_MESSAGE_SIZE = 1008;  // Max size for 4 messages per 4KB sector with sentinels
    static const uint32_t MAX_RECORD_SIZE = MAX_MESSAGE_SIZE + RECORD_HEADER_SIZE;
    static const uint32_t USABLE_SECTOR_SIZE = SECTOR_SIZE - SECTOR_HEADER_SIZE;

private:
    const esp_partition_t* m_partition;
    FlashRingBufferState m_state;
    bool m_initialized;
    Preferences m_preferences;
    
    // Serial debug function pointer
    long unsigned int (*m_fn_millis)(void);

    // RAM buffer for sector assembly (4KB)
    uint8_t* m_sector_buffer;
    uint8_t* m_sector_assembly_buffer;  // 4KB RAM buffer for sector assembly
    uint32_t m_buffer_used;             // Bytes used in current RAM buffer

    // Private methods
    bool findPartition();
    esp_err_t eraseSector(uint32_t sector_index);
    esp_err_t writeSector(uint32_t sector_index, const void* data, size_t size);
    esp_err_t readSector(uint32_t sector_index, void* data, size_t size) const;
    
    bool loadPersistedState();
    bool savePersistedState();
    bool validateState(const FlashRingBufferState& state) const;
    
    bool scanAndRecover();
    uint32_t scanSectorRecords(uint32_t sector_index, uint32_t claimed_used);
    bool validateSectorHeader(const SectorHeader& header) const;
    bool validateSectorComplete(uint32_t sector_index) const;
    uint16_t calculateCRC16(const uint8_t* data, size_t length) const;
    uint32_t calculateCRC32(const uint8_t* data, size_t length) const;
    
    bool advanceSector();
    bool sectorHasSpace(uint32_t required_bytes) const;
    uint32_t getRingDistance() const;
    void printDiagnosticReport(const FlashDiagnostics& diag) const;
    
    bool flushRAMBufferToFlash();       // Write complete RAM buffer to flash

public:
    FlashRingBuffer();
    ~FlashRingBuffer();
    
    // Initialization and teardown
    bool init(long unsigned int (*fn_millis)(void));
    void teardown();
    bool isInitialized() const { return m_initialized; }
    
    // Core operations
    bool appendRecord(const uint8_t* payload, uint16_t length);
    bool readOldestRecord(uint8_t* payload, uint16_t max_length, uint16_t& actual_length);
    bool deleteOldestRecord();
    
    // Status and statistics
    uint32_t getRecordCount() const;
    uint32_t getUsedSpace() const;
    uint32_t getFreeSpace() const;
    bool isEmpty() const;
    bool isFull() const;
    uint32_t getWriteCount() const { return m_state.write_count; }
    
    // Debug and diagnostics
    void printStatus() const;
    bool performSelfTest();
    bool performPowerOnSelfTest(bool auto_repair = true);
    bool performExtendedDiagnostics();
    bool performDeepSectorValidation();
    bool performPowerLossRecoveryTest();
    bool performStressTest(uint32_t num_records = 1000);
    
    // Safe shutdown
    void prepareForShutdown();
    void emergencyFlush();
    
    // Reset and recovery
    bool factoryReset();
    bool clearAllData();
    bool repairCorruption();
};

#endif // FLASH_RING_BUFFER_H