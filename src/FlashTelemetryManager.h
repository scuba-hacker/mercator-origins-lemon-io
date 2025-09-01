#ifndef FLASH_TELEMETRY_MANAGER_H
#define FLASH_TELEMETRY_MANAGER_H

#include "FlashRingBuffer.h"
#include <TelemetryPipeline.h>

// Compatibility layer to integrate FlashRingBuffer with existing MQTT system
// Provides same API as TelemetryPipeline for drop-in replacement
class FlashTelemetryManager {
public:
    // Storage modes
    enum StorageMode {
        PSRAM_ONLY,      // Use existing TelemetryPipeline (default/fallback)
        FLASH_ONLY,      // Use FlashRingBuffer only
        HYBRID           // Use FlashRingBuffer with PSRAM cache (future enhancement)
    };
    
private:
    FlashRingBuffer m_flash_buffer;
    TelemetryPipeline m_psram_pipeline;  // Fallback/hybrid mode
    StorageMode m_storage_mode;
    bool m_initialized;
    bool m_enable_flash_buffer;
    
    // Statistics
    uint32_t m_flash_writes;
    uint32_t m_flash_reads;
    uint32_t m_psram_fallbacks;
    
    long unsigned int (*m_fn_millis)(void);

public:
    FlashTelemetryManager();
    ~FlashTelemetryManager();
    
    // Initialization - same API as TelemetryPipeline
    bool init(long unsigned int (*fn_millis)(void), 
              const uint16_t maxBlockBufferMemoryUsageKB = 2048,
              const uint16_t maxBlockBufferMemoryUsageBytesRemainder = 0);
    
    void teardown();
    bool isInitialized() const { return m_initialized; }
    
    // Configuration
    void setStorageMode(StorageMode mode);
    StorageMode getStorageMode() const { return m_storage_mode; }
    void enableFlashBuffer(bool enable);
    bool isFlashBufferEnabled() const { return m_enable_flash_buffer; }
    
    // TelemetryPipeline-compatible API
    BlockHeader getHeadBlockForPopulating();
    bool commitPopulatedHeadBlock(BlockHeader head, bool& pipelineFull);
    bool pullTailBlock(BlockHeader& header);
    void tailBlockCommitted();
    
    // Status methods - same API as TelemetryPipeline
    bool pipelineEmpty() const;
    bool pipelineFull() const;
    uint16_t getPipelineLength() const;
    bool isPipelineDraining() const;
    
    // Flash-specific methods
    uint32_t getFlashRecordCount() const;
    uint32_t getFlashUsedSpace() const;
    uint32_t getFlashFreeSpace() const;
    uint32_t getFlashWriteCount() const;
    
    // Statistics
    uint32_t getTotalFlashWrites() const { return m_flash_writes; }
    uint32_t getTotalFlashReads() const { return m_flash_reads; }
    uint32_t getPsramFallbacks() const { return m_psram_fallbacks; }
    
    // Debug and maintenance
    void printStatus() const;
    bool performSelfTest();
    void prepareForShutdown();
    
    // Extended diagnostic methods
    bool performPowerOnSelfTest(bool auto_repair = true);
    bool performDeepSectorValidation();
    bool performPowerLossRecoveryTest();
    bool performStressTest(uint32_t num_records = 1000);
    
    // Reset and recovery functions
    bool factoryReset();
    bool clearAllFlashData();
    bool repairFlashCorruption();
    
    // Failure injection methods for testing (only available in testing builds)
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
    
private:
    // Helper methods
    bool convertBlockToFlashRecord(const BlockHeader& block);
    bool convertFlashRecordToBlock(BlockHeader& block);
    bool shouldUsePsramFallback() const;
    
    // Current block being processed
    BlockHeader m_current_head_block;
    bool m_has_pending_head_block;
    
    uint8_t* m_temp_buffer;  // Temporary buffer for conversions
    static const uint16_t TEMP_BUFFER_SIZE = 1024;
};

#endif // FLASH_TELEMETRY_MANAGER_H