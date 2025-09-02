/**
 * @file FlashTelemetryManager.h
 * @brief Marine telemetry management with flash persistence and PSRAM fallback
 * 
 * This class provides a compatibility layer that integrates the FlashRingBuffer
 * with the existing TelemetryPipeline system, enabling seamless switching between
 * flash-based persistent storage and PSRAM-based temporary storage.
 * 
 * Key Design Principles:
 * - Non-intrusive integration: Same API as TelemetryPipeline for drop-in replacement
 * - Fallback safety: Can switch to PSRAM if flash system fails
 * - Marine reliability: Persistent storage survives power cycles
 * - Operational flexibility: Runtime switching between storage modes
 * 
 * Marine Use Cases:
 * - Extended dive logging (8+ hours) with flash persistence
 * - Fallback to PSRAM mode if flash system encounters issues
 * - Seamless integration with existing MQTT upload system
 * - Zero-impact testing and validation of flash system
 * 
 * Storage Modes:
 * - PSRAM_ONLY: Traditional operation (battle-tested fallback)
 * - FLASH_ONLY: New persistent storage (marine extended operation)
 * - HYBRID: Future enhancement combining both storage types
 * 
 * @author Generated for Mercator Origins dive computer system
 * @version 1.0
 * @date 2024
 */

#ifndef FLASH_TELEMETRY_MANAGER_H
#define FLASH_TELEMETRY_MANAGER_H

#include "FlashRingBuffer.h"
#include <TelemetryPipeline.h>
/**
 * @class FlashTelemetryManager
 * @brief Marine telemetry manager with persistent flash storage and PSRAM fallback
 * 
 * Provides a unified interface for telemetry storage that can seamlessly switch
 * between flash-based persistent storage and PSRAM-based temporary storage.
 * Designed for non-intrusive integration with existing marine telemetry systems.
 * 
 * Thread Safety: This class is NOT thread-safe. External synchronization required.
 * Memory Usage: ~12KB RAM (4KB + 4KB + 4KB buffers) plus TelemetryPipeline overhead
 */
class FlashTelemetryManager {
public:
    /**
     * @brief Storage mode selection for telemetry data
     * 
     * Marine Operational Modes:
     * - PSRAM_ONLY: Battle-tested mode for proven reliability
     * - FLASH_ONLY: Extended marine operation with persistent storage
     * - HYBRID: Future enhancement for optimal performance
     */
    enum StorageMode {
        PSRAM_ONLY,      // Use existing TelemetryPipeline (default/fallback mode)
        FLASH_ONLY,      // Use FlashRingBuffer only (extended marine operation)
        HYBRID           // Use FlashRingBuffer with PSRAM cache (future enhancement)
    };
    
private:
    // === Core Storage Systems ===
    FlashRingBuffer m_flash_buffer;      // Persistent flash storage (10MB, power-safe)
    TelemetryPipeline m_psram_pipeline;  // PSRAM fallback/hybrid storage (battle-tested)
    
    // === Configuration and State ===
    StorageMode m_storage_mode;          // Current operational mode
    bool m_initialized;                  // System initialization status
    bool m_enable_flash_buffer;          // Flash buffer enable/disable flag
    
    // === Performance Statistics ===
    // Critical for marine diagnostics and maintenance planning
    uint32_t m_flash_writes;             // Total records written to flash
    uint32_t m_flash_reads;              // Total records read from flash
    uint32_t m_psram_fallbacks;          // Number of PSRAM fallback operations
    
    // === Dependencies ===
    long unsigned int (*m_fn_millis)(void); // Timing function for diagnostics

public:
    // === Construction and Destruction ===
    FlashTelemetryManager();                                                // Constructor - safe defaults
    ~FlashTelemetryManager();                                               // Destructor - safe cleanup
    
    // === Initialization - TelemetryPipeline Compatible API ===
    /**
     * @brief Initialize telemetry management system
     * @param fn_millis Function pointer to millis() for timing
     * @param maxBlockBufferMemoryUsageKB PSRAM memory limit (for fallback mode)
     * @param maxBlockBufferMemoryUsageBytesRemainder Additional PSRAM bytes
     * @return true if initialization successful
     * 
     * Marine Initialization Sequence:
     * 1. Initialize both flash and PSRAM storage systems
     * 2. Perform power-on self-test with auto-repair
     * 3. Set operational mode based on flash system health
     * 4. Provide detailed diagnostic information
     * 
     * Maintains full API compatibility with TelemetryPipeline for drop-in replacement
     */
    bool init(long unsigned int (*fn_millis)(void), 
              const uint16_t maxBlockBufferMemoryUsageKB = 2048,
              const uint16_t maxBlockBufferMemoryUsageBytesRemainder = 0);
    
    void teardown();                                                        // Safe system shutdown
    bool isInitialized() const { return m_initialized; }                   // Initialization status check
    
    // === Configuration Management ===
    void setStorageMode(StorageMode mode);                                  // Runtime mode switching
    StorageMode getStorageMode() const { return m_storage_mode; }           // Current mode query
    void enableFlashBuffer(bool enable);                                    // Flash enable/disable
    bool isFlashBufferEnabled() const { return m_enable_flash_buffer; }     // Flash status query
    
    // === TelemetryPipeline-Compatible API ===
    /**
     * @brief Get block header for new telemetry data
     * @return BlockHeader for data population
     * 
     * Drop-in replacement for TelemetryPipeline::getHeadBlockForPopulating()
     * Routes to appropriate storage system based on current mode
     */
    BlockHeader getHeadBlockForPopulating();
    
    /**
     * @brief Commit populated telemetry block
     * @param head Populated block header
     * @param pipelineFull Returns true if storage is full
     * @return true if commit successful
     * 
     * Drop-in replacement for TelemetryPipeline::commitPopulatedHeadBlock()
     */
    bool commitPopulatedHeadBlock(BlockHeader head, bool& pipelineFull);
    
    /**
     * @brief Pull oldest telemetry block for MQTT upload
     * @param header Returns block header with data
     * @return true if block available
     * 
     * Drop-in replacement for TelemetryPipeline::pullTailBlock()
     */
    bool pullTailBlock(BlockHeader& header);
    
    /**
     * @brief Acknowledge successful MQTT upload
     * 
     * Drop-in replacement for TelemetryPipeline::tailBlockCommitted()
     * Removes uploaded block from storage
     */
    void tailBlockCommitted();
    
    // === Status Methods - TelemetryPipeline Compatible ===
    bool pipelineEmpty() const;                                             // True if no data stored
    bool pipelineFull() const;                                              // True if storage full
    uint16_t getPipelineLength() const;                                     // Number of blocks/records
    bool isPipelineDraining() const;                                        // True if uploading active
    
    // === Flash-Specific Status Methods ===
    uint32_t getFlashRecordCount() const;                                   // Records in flash storage
    uint32_t getFlashUsedSpace() const;                                     // Flash bytes used
    uint32_t getFlashFreeSpace() const;                                     // Flash bytes available
    uint32_t getFlashWriteCount() const;                                    // Total flash writes (wear)
    
    // === Performance Statistics ===
    // Critical for marine maintenance and troubleshooting
    uint32_t getTotalFlashWrites() const { return m_flash_writes; }         // Flash write operations
    uint32_t getTotalFlashReads() const { return m_flash_reads; }           // Flash read operations
    uint32_t getPsramFallbacks() const { return m_psram_fallbacks; }        // PSRAM fallback usage
    
    // === Debug and Maintenance ===
    void printStatus() const;                                               // Comprehensive status report
    bool performSelfTest();                                                 // Basic functional test
    void prepareForShutdown();                                              // Safe shutdown preparation
    
    // === Extended Diagnostic Methods ===
    // Critical for marine deployment validation
    bool performPowerOnSelfTest(bool auto_repair = true);                   // POST with auto-repair
    bool performDeepSectorValidation();                                     // Complete flash validation
    bool performPowerLossRecoveryTest();                                    // Power-loss recovery test
    bool performStressTest(uint32_t num_records = 1000);                    // High-load stress test
    
    // === Reset and Recovery Functions ===
    bool factoryReset();                                                    // Complete system reset
    bool clearAllFlashData();                                               // Clear flash data only
    bool repairFlashCorruption();                                           // Repair corrupted flash
    
    // === Failure Injection Methods (TESTING_MODE Only) ===
    /**
     * @brief Failure injection for testing system robustness
     * 
     * WARNING: TESTING_MODE ONLY - Never use in production
     * These methods provide controlled failure injection for validating
     * the system's ability to handle marine environment challenges
     */
    #ifdef TESTING_MODE
    bool injectSectorCorruption(uint32_t sector_index);                     // Corrupt sector header
    bool corruptPersistedState();                                           // Corrupt NVS state
    bool simulateIncompleteWrite();                                         // Simulate power-loss
    bool corruptRingPointers();                                             // Corrupt head/tail pointers
    bool acceleratedWearTest(uint32_t cycles);                              // Rapid wear testing
    bool injectRandomCorruption(uint32_t num_sectors);                      // Random corruption
    bool simulatePartitionFailure();                                        // Partition failure
    bool injectCRCCorruption(uint32_t sector_index);                        // CRC corruption
    void enableFailureInjection();                                          // Enable testing mode
    #endif
    
private:
    // === Helper Methods ===
    bool convertBlockToFlashRecord(const BlockHeader& block);              // TelemetryPipeline block → flash
    bool convertFlashRecordToBlock(BlockHeader& block);                     // Flash → TelemetryPipeline block
    bool shouldUsePsramFallback() const;                                    // Fallback decision logic
    
    // === Block Processing State ===
    BlockHeader m_current_head_block;                                       // Current block being populated
    bool m_has_pending_head_block;                                          // Pending block flag
    
    // === Temporary Storage ===
    uint8_t* m_temp_buffer;                                                 // Conversion buffer
    static const uint16_t TEMP_BUFFER_SIZE = 1024;                         // Buffer size
};

#endif // FLASH_TELEMETRY_MANAGER_H