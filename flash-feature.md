# Flash Ring Buffer Feature Documentation

## Overview

This document describes the comprehensive flash ring buffer system implemented for the Mercator Origins Lemon-IO device. The system provides persistent telemetry storage using a 2MB flash ring buffer as a non-intrusive enhancement to the existing PSRAM-based TelemetryPipeline.

## System Architecture

### Core Components

1. **FlashRingBuffer** (`src/FlashRingBuffer.h/.cpp`)
   - 2MB flash partition with 512 sectors (4KB each)
   - Power-safe record writes with CRC validation
   - Variable message sizes (16-928 bytes)
   - EEPROM state persistence via ESP32 Preferences
   - Comprehensive auto-repair and diagnostics

2. **FlashTelemetryManager** (`src/FlashTelemetryManager.h/.cpp`)
   - Drop-in replacement for TelemetryPipeline
   - Runtime switching between PSRAM/Flash storage
   - Automatic fallback to PSRAM on flash errors
   - Same API compatibility for seamless integration

3. **Partition Configuration**
   - Updated `partitions/ota_spiffs_16MB.csv`
   - Added 2MB `flashbuf` partition at 0x1000000
   - Maintains OTA capabilities

## Storage Mode Coexistence

### Three Operating Modes

The FlashTelemetryManager supports three distinct storage modes that can coexist:

#### **1. PSRAM_ONLY Mode (Default/Fallback)**
- Uses existing TelemetryPipeline class
- 2MB PSRAM buffer (bullet-proof, battle-tested)
- Data lost on power cycle
- Immediate failover if flash issues occur
- **Use when**: Flash disabled or flash errors detected

#### **2. FLASH_ONLY Mode (Primary)**  
- Uses FlashRingBuffer for persistent storage
- 2MB flash ring buffer (power-safe, persistent)
- Data survives power cycles and restarts
- Auto-repair and corruption recovery
- **Use when**: Flash buffer enabled and healthy

#### **3. HYBRID Mode (Future Enhancement)**
- PSRAM cache with flash persistence
- Best of both worlds - speed + persistence
- **Status**: Planned for future implementation

### Runtime Mode Switching

The system can switch modes dynamically based on conditions:

```cpp
// FlashTelemetryManager automatically switches modes based on:

1. Configuration setting (flashBufferTestingEnabled)
2. Flash buffer initialization success/failure  
3. Runtime flash errors (auto-fallback to PSRAM)
4. Manual mode selection via API
```

### Mode Selection Logic

```
Startup Decision Tree:
├── Flash Buffer Enabled? 
│   ├── YES → Initialize FlashRingBuffer
│   │   ├── Init Success? 
│   │   │   ├── YES → POST Passes?
│   │   │   │   ├── YES → Mode: FLASH_ONLY ✅
│   │   │   │   └── NO → Auto-repair → Success?
│   │   │   │       ├── YES → Mode: FLASH_ONLY ✅
│   │   │   │       └── NO → Mode: PSRAM_ONLY ⚠️
│   │   │   └── NO → Mode: PSRAM_ONLY ⚠️
│   │   └── PSRAM Always Available as Fallback
│   └── NO → Mode: PSRAM_ONLY (default)
```

### Configuration Control

#### **Persistent Configuration (EEPROM)**
```cpp
// Testing preferences namespace "testing"
flashBufferTestingEnabled = true/false  // Enable/disable flash buffer
wifiTestingBlocked = true/false         // Testing connectivity state

// Flash buffer state namespace "flashring"  
head_sector, tail_sector, etc.          // Persistent ring buffer state
```

#### **Runtime Mode Control**
```cpp
FlashTelemetryManager manager;

// Enable/disable flash buffer (requires restart)
manager.enableFlashBuffer(true/false);

// Manual mode switching  
manager.setStorageMode(FLASH_ONLY);
manager.setStorageMode(PSRAM_ONLY);
manager.setStorageMode(HYBRID);  // Future

// Query current mode
StorageMode mode = manager.getStorageMode();
```

#### **Serial Commands for Testing**
- **`F`**: Toggle flash buffer enable/disable (persisted, requires restart)
- **`S`**: Show current storage mode and status
- Mode changes logged via SerialConfig.h macros

### Automatic Fallback Behavior

The system provides graceful degradation:

#### **Flash → PSRAM Fallback Scenarios**
1. **Flash initialization failure** → Immediate PSRAM mode
2. **POST failure** → Auto-repair attempt → PSRAM if repair fails  
3. **Runtime write errors** → Log error, continue with PSRAM
4. **Corruption detection** → Auto-repair attempt → PSRAM if needed
5. **Partition access errors** → Immediate PSRAM mode

#### **Fallback Statistics Tracking**
```cpp
manager.getPsramFallbacks();     // Count of fallback events
manager.getTotalFlashWrites();   // Flash operation statistics
manager.getTotalFlashReads();    // Flash read statistics
```

### Data Continuity During Mode Switches

#### **PSRAM → Flash Transition**
- Existing PSRAM data remains accessible
- New data goes to flash buffer
- Gradual drain of PSRAM via MQTT
- No data loss during transition

#### **Flash → PSRAM Fallback**
- Flash data preserved (power-safe)
- New data goes to PSRAM immediately
- Flash data recovery possible after restart
- Fallback logged and counted

#### **Mode Persistence**
- Configuration survives power cycles
- Flash data survives power cycles
- PSRAM data lost on power cycle (expected)
- Auto-recovery attempts on next boot

## Integration Strategy

### Non-Intrusive Design Principles

The flash ring buffer system was designed to be completely non-intrusive:

#### **1. API Compatibility**
```cpp
// Existing code using TelemetryPipeline:
TelemetryPipeline telemetryPipeline;
telemetryPipeline.init(&millis, 2048);
BlockHeader block = telemetryPipeline.getHeadBlockForPopulating();
// ... existing code unchanged ...

// New code using FlashTelemetryManager (drop-in replacement):
FlashTelemetryManager telemetryPipeline;  // Same variable name!
telemetryPipeline.init(&millis, 2048);    // Same parameters!
BlockHeader block = telemetryPipeline.getHeadBlockForPopulating();  // Same API!
// ... all existing code works unchanged ...
```

#### **2. Gradual Migration Path**
- **Phase 1**: Test with existing TelemetryPipeline (no changes)
- **Phase 2**: Enable FlashTelemetryManager but use PSRAM_ONLY mode
- **Phase 3**: Enable FLASH_ONLY mode for testing
- **Phase 4**: Production deployment with flash buffer active
- **Always**: PSRAM fallback available for safety

#### **3. Zero Impact on Existing Features**
- MQTT upload system unchanged
- Web interface unaffected
- Statistics and monitoring preserved
- OTA updates work normally
- Network management unchanged

### Integration Points in main.cpp

#### **Current State (Line 284)**
```cpp
TelemetryPipeline telemetryPipeline;
```

#### **Future State (Simple Replacement)**
```cpp
#include "FlashTelemetryManager.h"
FlashTelemetryManager telemetryPipeline;  // Drop-in replacement
```

#### **Configuration Integration**
```cpp
// In setup() after line 951:
telemetryPipeline.enableFlashBuffer(flashBufferTestingEnabled);
if (flashBufferTestingEnabled) {
    telemetryPipeline.setStorageMode(FLASH_ONLY);
} else {
    telemetryPipeline.setStorageMode(PSRAM_ONLY);  
}
```

#### **Runtime Status Integration**
```cpp
// Enhanced 'S' command status display:
USB_SERIAL_PRINTF("Storage Mode: %s\n", 
    (telemetryPipeline.getStorageMode() == FLASH_ONLY) ? "FLASH" :
    (telemetryPipeline.getStorageMode() == PSRAM_ONLY) ? "PSRAM" : "HYBRID");
USB_SERIAL_PRINTF("Flash Records: %u\n", telemetryPipeline.getFlashRecordCount());
USB_SERIAL_PRINTF("Flash Space Used: %u KB\n", telemetryPipeline.getFlashUsedSpace() / 1024);
USB_SERIAL_PRINTF("PSRAM Fallbacks: %u\n", telemetryPipeline.getPsramFallbacks());
```

## Flash Storage Structure

### Sector Layout (4KB sectors)
```
Sector Header (16 bytes):
- magic: 0x42474F4C "LOGB"
- seq: Monotonic sequence number
- used: Bytes used in sector
- crc32: Header CRC validation

Records (variable size):
- len: Payload length (16-928 bytes)
- crc16: Payload CRC
- valid: 0x00=complete, 0xFF=incomplete
- payload: Binary telemetry data
```

### Power-Safe Write Pattern
1. Erase sector (if advancing)
2. Write record with valid=0xFF (incomplete)
3. Write payload and CRC
4. Write valid=0x00 (commit - atomic operation)
5. Update sector header

## Key Features

### Power-On Self-Test (POST)
- **Duration**: ~2-3 seconds typical, up to 20 seconds if major repairs needed
- **Tests**: Partition access, sector validation, state consistency
- **Auto-repair**: Automatically fixes corruption during boot
- **Comprehensive logging**: Detailed diagnostic reports
- **Mode Selection**: POST results influence FLASH vs PSRAM mode selection

### Auto-Repair Capabilities
- Corrupted sector headers (recalculates CRC32)
- Invalid sector usage (scans and repairs boundaries)  
- Partial record writes (validates and truncates)
- Severely corrupted sectors (erases and reinitializes)
- Inconsistent persistent state (rebuilds from flash)

### Reset Functions
- `factoryReset()`: Complete system reset (EEPROM + flash)
- `clearAllData()`: Flash data only (preserves config)
- `repairCorruption()`: Smart corruption repair

## Testing System

### Serial Commands (single key, no Enter)
- **`D`**: Disconnect WiFi (simulate no internet)
- **`C`**: Connect WiFi (simulate internet return)
- **`F`**: Toggle flash buffer enable/disable (requires restart for mode change)
- **`R`**: Reset flash buffer (factory reset)
- **`S`**: Show current status (includes storage mode)
- **`H`** or **`?`**: Show help

### Testing Modes and Scenarios

#### **Scenario 1: PSRAM-Only Testing (Safe Mode)**
```
1. Ensure flash buffer disabled: F command shows "DISABLED"  
2. Restart system
3. Verify 'S' command shows "Storage Mode: PSRAM"
4. Test normal operation - same as before flash feature
5. This validates no regression in existing functionality
```

#### **Scenario 2: Flash Buffer Activation**
```
1. Enable flash buffer: F command
2. Restart system  
3. Monitor POST sequence (~3 seconds)
4. Verify 'S' command shows "Storage Mode: FLASH"
5. Confirm telemetry pipeline using flash storage
```

#### **Scenario 3: Mode Switching Under Load**
```
1. Start in FLASH mode, accumulate data
2. Simulate flash error (could be done via hidden test command)
3. Verify automatic fallback to PSRAM mode
4. Confirm data continuity and no loss
5. Restart system, verify flash data recovery
```

#### **Scenario 4: Dual-Buffer Validation**
```
1. Run system in PSRAM mode, accumulate data
2. Switch to FLASH mode (requires restart)
3. Verify PSRAM data drains via MQTT
4. Verify new data goes to flash buffer
5. Power cycle, confirm only flash data survives
```

### Performance Comparison

| Feature | PSRAM Mode | Flash Mode | Notes |
|---------|------------|------------|--------|
| **Capacity** | 2MB | 2MB | Same buffer size |
| **Persistence** | ❌ Lost on power-off | ✅ Survives power-off | Flash advantage |
| **Write Speed** | ~1ms | ~10-50ms | PSRAM faster |
| **Read Speed** | ~0.1ms | ~5-20ms | PSRAM faster |
| **Reliability** | ✅ Battle-tested | ✅ Power-safe + POST | Both reliable |
| **Recovery** | ❌ Data lost | ✅ Auto-repair | Flash advantage |
| **Startup Time** | ~10ms | ~3 seconds (POST) | PSRAM faster |

## Performance Characteristics

### Storage Capacity
- **Total**: 2MB flash partition
- **Usable**: ~1.8MB (accounting for headers)
- **Messages**: ~34,000 records @ 224 bytes each
- **Duration**: ~9.5 hours @ 1Hz telemetry rate

### Timing
- **Write**: ~10-50ms per record (includes sector management)
- **Read**: ~5-20ms per record
- **POST**: 2-3 seconds typical, up to 20 seconds with repairs
- **Sector advance**: ~100-500ms (erase + init)
- **Mode switch**: Immediate (runtime), restart required for config changes

### Wear Characteristics
- **Erase cycles**: Flash rated for 10,000-100,000 cycles
- **Even wear**: Ring buffer distributes across all 512 sectors
- **Monitoring**: Write count tracking for wear analysis

## Configuration and Preferences

### Flash Buffer State (EEPROM "flashring" namespace)
- `head_sector`: Current write position
- `tail_sector`: Current read position  
- `head_sector_used`: Bytes used in head sector
- `next_seq_number`: Sector sequence counter
- `write_count`: Total writes (wear tracking)
- `state_crc`: State integrity validation

### Testing Preferences (EEPROM "testing" namespace)
- `wifi_blocked`: WiFi disconnect state
- `flash_enabled`: Flash buffer enable state (persists across reboots)

## Error Handling and Recovery

### Boot Recovery
1. Load persisted state from EEPROM
2. Attempt flash buffer initialization
3. Run POST if flash enabled
4. Auto-repair corruption if detected
5. Select appropriate storage mode (FLASH or PSRAM)
6. Continue with selected mode

### Runtime Error Handling
- Flash write failures → Log error, automatic PSRAM fallback
- Corruption detection → Auto-repair attempt, PSRAM fallback if needed
- EEPROM failures → Rebuild state from flash scan
- Power loss → POST recovery on next boot
- Mode switching → Graceful transition with data preservation

### Diagnostics and Logging
All operations logged via `SerialConfig.h` macros:
- Mode selection reasoning
- Fallback event logging
- Performance metrics
- Error condition details
- Storage statistics

## Safety Features

### Power-Loss Protection
- Atomic commit operations (single byte write)
- Incomplete record detection and cleanup
- Sector boundary protection
- State persistence in EEPROM
- PSRAM fallback always available

### Data Integrity
- CRC16 validation for all payloads
- CRC32 validation for sector headers
- Magic number validation
- Length boundary checks
- Dual-buffer redundancy (PSRAM + Flash)

### Watchdog Protection
- Yields during long operations (POST, repairs)
- Chunked erase operations (64KB chunks)
- Timeout protection on flash operations

## Future Enhancements

### Planned Improvements
- **Hybrid mode**: PSRAM cache with flash persistence
- **Compression**: Increased storage efficiency
- **Wear leveling**: Advanced statistics and reporting
- **Remote diagnostics**: Web interface controls
- **Smart mode switching**: Load-based automatic selection

### Integration Opportunities
- Web interface controls for storage mode selection
- Real-time storage statistics on stats page
- Remote factory reset capabilities
- Firmware update protection for flash data
- Performance monitoring dashboard

## Implementation Status

✅ **Completed Features**
- Core flash ring buffer implementation
- FlashTelemetryManager compatibility layer
- Dual-mode operation (PSRAM + Flash)
- Automatic fallback system
- Runtime mode switching capability
- Power-safe write operations  
- Auto-repair and recovery system
- Power-on self-test suite
- Serial testing interface
- EEPROM state persistence
- Comprehensive error handling
- Reset and factory functions
- Diagnostic and monitoring tools

🔄 **Integration Status**
- FlashRingBuffer and FlashTelemetryManager classes complete
- Serial command interface implemented
- Testing system ready
- API compatibility verified
- Mode switching logic implemented

⚠️ **Next Steps for Full Integration**
1. Replace `TelemetryPipeline telemetryPipeline;` with `FlashTelemetryManager telemetryPipeline;` in main.cpp:284
2. Add FlashTelemetryManager includes to main.cpp
3. Update telemetryPipeline.init() call parameters if needed
4. Connect factory reset command (line 1739) to actual flash buffer
5. Test complete integration with mode switching

This documentation provides complete context for the coexistent dual-buffer system and mode switching capabilities.