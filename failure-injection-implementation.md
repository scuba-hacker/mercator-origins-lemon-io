# Flash Buffer Failure Injection Implementation Guide

## Overview

This document describes the complete implementation of the failure injection system for the Mercator Origins flash buffer testing. The system provides comprehensive failure simulation capabilities to validate the robustness and recovery mechanisms of the flash persistence system.

## Architecture

### System Components

```
┌─────────────────────────────────────────────────────────────────┐
│                    WebSerial Interface                         │
│                    (Web Browser)                               │
└─────────────────────┬───────────────────────────────────────────┘
                      │ WebSocket Commands
┌─────────────────────▼───────────────────────────────────────────┐
│                    main.cpp                                    │
│           processExtendedCommand()                             │
└─────────────────────┬───────────────────────────────────────────┘
                      │ Method Calls
        ┌─────────────▼──────────────┐
        │   Compile-Time Selection   │
        │  #ifdef USE_FLASH_TELEMETRY│
        └─────────────┬──────────────┘
                      │
        ┌─────────────▼──────────────┬─────────────────────────────┐
        │    FlashTelemetryManager   │      TelemetryPipeline      │
        │    (Full Implementation)   │      (Stub Methods)         │
        └─────────────┬──────────────┴─────────────────────────────┘
                      │
        ┌─────────────▼──────────────┐
        │      FlashRingBuffer       │
        │   (Core Implementation)    │
        └────────────────────────────┘
```

### Design Principles

1. **Non-Intrusive**: Failure injection only available with `TESTING_MODE` compiler flag
2. **Safe**: Production builds automatically exclude all failure injection code
3. **Unified API**: Same commands work with both FlashTelemetryManager and TelemetryPipeline
4. **Clear Feedback**: Users receive appropriate responses based on pipeline capabilities
5. **Comprehensive**: Covers all major failure scenarios for thorough testing

## Implementation Details

### 1. Core Failure Injection Methods (FlashRingBuffer)

#### File: `src/FlashRingBuffer.h`
```cpp
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
```

#### File: `src/FlashRingBuffer_part3.cpp`
Each method includes:
- **Parameter validation** (initialization check, range validation)
- **Detailed logging** via USB_SERIAL_PRINTF macros
- **Safe corruption** with bounds checking
- **Recovery guidance** (restart instructions where needed)

### 2. Compatibility Layer (FlashTelemetryManager)

#### File: `src/FlashTelemetryManager.h`
```cpp
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
```

#### File: `src/FlashTelemetryManager.cpp`
Each wrapper method includes:
- **Mode validation** (ensures FLASH_ONLY mode)
- **Initialization check** (verifies flash buffer is ready)
- **Error logging** with descriptive messages
- **Pass-through** to FlashRingBuffer implementation

### 3. Stub Methods (TelemetryPipeline)

#### File: `TelemetryPipeline.h` (Added by user)
```cpp
// Diagnostic method stubs (for compatibility with FlashTelemetryManager)
bool performPowerOnSelfTest(bool auto_repair = true) { return true; }
bool performDeepSectorValidation() { return true; }
bool performPowerLossRecoveryTest() { return true; }
bool performStressTest(uint32_t num_records = 1000) { return true; }

// Failure injection method stubs (only available in testing builds)
#ifdef TESTING_MODE
bool injectSectorCorruption(uint32_t sector_index) { return false; }
bool corruptPersistedState() { return false; }
bool simulateIncompleteWrite() { return false; }
bool corruptRingPointers() { return false; }
bool acceleratedWearTest(uint32_t cycles) { return false; }
bool injectRandomCorruption(uint32_t num_sectors) { return false; }
bool simulatePartitionFailure() { return false; }
bool injectCRCCorruption(uint32_t sector_index) { return false; }
void enableFailureInjection() {}
#endif
```

**Stub Behavior:**
- **Diagnostic methods return `true`** (always pass - no actual testing)
- **Failure injection methods return `false`** (not supported in PSRAM mode)
- **enableFailureInjection() is no-op** (silent - no action needed)

### 4. WebSerial Command Processing

#### File: `src/main.cpp`

**Command Processing Flow:**
```cpp
void processExtendedCommand(const String& command) {
    // Diagnostic commands (both pipelines)
    if (command == "POST") {
        bool result = telemetryPipeline.performPowerOnSelfTest(true);
        // Result: true (always) for TelemetryPipeline, actual test for FlashTelemetryManager
    }
    
    // Failure injection commands (TESTING_MODE only)
    #ifdef TESTING_MODE
    else if (command.startsWith("CORRUPT_SECTOR")) {
        bool result = telemetryPipeline.injectSectorCorruption(sector_num);
        // Result: false for TelemetryPipeline, actual injection for FlashTelemetryManager
    }
    #endif
}
```

**Command Categories:**

1. **Diagnostic Commands** (Available in all builds):
   - `POST` - Power-On Self Test
   - `DEEP` - Deep Sector Validation  
   - `STRESS` - High-Volume Stress Test
   - `RECOVERY` - Power-Loss Recovery Test

2. **Failure Injection Commands** (TESTING_MODE builds only):
   - `CORRUPT_SECTOR [num]` - Corrupt sector magic number
   - `CORRUPT_STATE` - Corrupt EEPROM state
   - `SIMULATE_POWER_LOSS` - Create incomplete record
   - `CORRUPT_POINTERS` - Invalid head/tail pointers
   - `WEAR_TEST [cycles]` - Accelerated wear testing
   - `RANDOM_CORRUPT [num]` - Multi-sector corruption
   - `PARTITION_FAIL` - Partition access failure
   - `CORRUPT_CRC [num]` - Corrupt sector CRC
   - `ENABLE_FAIL_INJECT` - Verify injection mode

## Build Configuration

### Required Compiler Flags

#### For Failure Injection Testing:
```ini
build_flags = 
    -DTESTING_MODE          ; Enable failure injection commands
    -DUSE_FLASH_TELEMETRY   ; Enable flash telemetry system
```

#### For Production Builds:
```ini
build_flags = 
    -DUSE_FLASH_TELEMETRY   ; Enable flash telemetry (optional)
    ; Never include -DTESTING_MODE in production!
```

### Pipeline Selection

#### Flash Mode (Recommended for Testing):
```cpp
#define USE_FLASH_TELEMETRY
```

#### PSRAM Mode (Development/Fallback):
```cpp
// #define USE_FLASH_TELEMETRY  // Commented out
```

## Failure Injection Methods

### 1. Sector Corruption (`CORRUPT_SECTOR`)

**Purpose:** Test sector header validation and auto-repair

**Implementation:**
- Overwrites sector magic number with `0xDEADBEEF`
- Targets specific sector or default sector 5
- POST should detect and repair corruption

**Usage:** `CORRUPT_SECTOR 10`

### 2. State Corruption (`CORRUPT_STATE`)

**Purpose:** Test EEPROM state recovery from flash scan

**Implementation:**
- Corrupts persistent state in EEPROM (head/tail pointers, sequence)
- Sets values to `0xFFFFFFFF` (invalid)
- **Requires system restart** to test recovery

**Usage:** `CORRUPT_STATE`

### 3. Power-Loss Simulation (`SIMULATE_POWER_LOSS`)

**Purpose:** Test incomplete record recovery

**Implementation:**
- Writes record header and payload
- Leaves `valid` flag as `0xFF` (incomplete)
- POST should detect and truncate incomplete record

**Usage:** `SIMULATE_POWER_LOSS`

### 4. Pointer Corruption (`CORRUPT_POINTERS`)

**Purpose:** Test boundary validation and pointer recovery

**Implementation:**
- Sets head/tail pointers beyond valid range
- POST should detect and correct invalid pointers
- System should rebuild valid ring buffer state

**Usage:** `CORRUPT_POINTERS`

### 5. Accelerated Wear Testing (`WEAR_TEST`)

**Purpose:** Test system stability under high write counts

**Implementation:**
- Rapidly cycles through sectors by filling them
- Monitors write counts and performance
- Tests wear leveling and sector management

**Usage:** `WEAR_TEST 1000`

### 6. Random Multi-Sector Corruption (`RANDOM_CORRUPT`)

**Purpose:** Test recovery from widespread corruption

**Implementation:**
- Applies random corruption to multiple sectors
- Mix of magic, CRC, and usage field corruption
- Limited to 25% of sectors for safety

**Usage:** `RANDOM_CORRUPT 5`

### 7. CRC Corruption (`CORRUPT_CRC`)

**Purpose:** Test CRC validation and repair

**Implementation:**
- Corrupts CRC32 field in sector header
- Tests CRC validation during POST
- Auto-repair should recalculate correct CRC

**Usage:** `CORRUPT_CRC 7`

### 8. Partition Failure (`PARTITION_FAIL`)

**Purpose:** Test partition access failure handling

**Implementation:**
- Sets partition pointer to NULL
- **Requires system restart** to restore access
- Tests graceful fallback to PSRAM mode

**Usage:** `PARTITION_FAIL`

## Expected Behavior by Pipeline Type

### FlashTelemetryManager (Flash Mode)

```
Command: POST
Result: Full flash buffer validation with actual auto-repair

Command: CORRUPT_SECTOR 5
Result: "Sector corruption INJECTED"

Command: CORRUPT_STATE
Result: "State corruption INJECTED - Restart system to test recovery"

Command: WEAR_TEST 100
Result: "Wear test COMPLETED" (actual flash operations performed)
```

### TelemetryPipeline (PSRAM Mode)

```
Command: POST
Result: "PASSED" (stub - no actual testing)

Command: CORRUPT_SECTOR 5
Result: "FAILED (not in flash mode or not supported)"

Command: CORRUPT_STATE
Result: "FAILED (not in flash mode or not supported)"

Command: WEAR_TEST 100
Result: "FAILED (not in flash mode or not supported)"
```

## Safety Mechanisms

### Compile-Time Protection

1. **TESTING_MODE Guard:**
   ```cpp
   #ifdef TESTING_MODE
   // Failure injection code only included in test builds
   #endif
   ```

2. **Production Safety:**
   - Production builds automatically exclude failure injection
   - No performance impact in production
   - No security vulnerabilities from test code

### Runtime Protection

1. **Mode Validation:**
   - FlashTelemetryManager validates FLASH_ONLY mode
   - Prevents injection in wrong mode
   - Clear error messages for unsupported operations

2. **Initialization Checks:**
   - Verifies flash buffer is properly initialized
   - Prevents crashes from uninitialized state
   - Safe failure with descriptive error messages

3. **Bounds Checking:**
   - Sector numbers validated against total sectors
   - Corruption limited to safe ranges
   - Random corruption limited to 25% of sectors

## Testing Workflow

### Phase 1: Basic System Validation
1. `F` - Verify flash persistence setting
2. `S` - Check system status and active pipeline
3. `H` - Confirm available commands

### Phase 2: Diagnostic Baseline
1. `POST` - Establish clean system baseline
2. `STRESS` - Verify basic read/write functionality
3. `S` - Monitor system statistics

### Phase 3: Controlled Failure Injection
1. `CORRUPT_SECTOR 5` - Test single sector corruption
2. `POST` - Verify auto-repair functionality
3. `S` - Confirm system operational after repair

### Phase 4: Complex Failure Scenarios
1. `RANDOM_CORRUPT 3` - Test multi-sector corruption
2. `POST` - Verify comprehensive repair capability
3. `CORRUPT_STATE` + restart - Test state recovery
4. `POST` - Verify state reconstruction from flash

### Phase 5: Wear and Stress Testing
1. `WEAR_TEST 500` - Extended wear simulation
2. `S` - Monitor write counts and performance
3. `POST` - Ensure system health after high wear

## Troubleshooting

### Common Issues

#### "FAILED (not in flash mode or not supported)"
- **Cause:** Using TelemetryPipeline (PSRAM mode) or wrong storage mode
- **Solution:** Compile with `USE_FLASH_TELEMETRY` and `TESTING_MODE`

#### Commands not recognized
- **Cause:** `TESTING_MODE` not defined
- **Solution:** Add `-DTESTING_MODE` to build flags

#### Flash buffer not initialized
- **Cause:** Partition access failure or flash system not ready
- **Solution:** Check partition table and run `POST` with auto-repair

### Debug Information

**Enable detailed logging:**
- All failure injection methods include comprehensive USB_SERIAL_PRINTF logging
- POST operations provide detailed diagnostic reports
- Recovery operations log all actions taken

**Monitor system status:**
- Use `S` command frequently to monitor system state
- Check record counts and space usage
- Verify write counts for wear monitoring

## Integration Points

### Web Interface Integration

The failure injection commands are accessible through the existing web interface at `http://[device-ip]/logs`:

1. **Command Dropdown:** Pre-configured failure injection commands
2. **Custom Input Field:** Manual command entry with parameters
3. **Real-Time Console:** All output appears in web browser
4. **Session Logging:** Complete test sessions can be saved

### Automated Testing Integration

The system supports automated testing scenarios:

```javascript
// Example automated test sequence
const testSequence = [
    'POST',                    // Baseline
    'CORRUPT_SECTOR 5',       // Inject corruption
    'POST',                    // Verify repair
    'STRESS',                  // Validate functionality
    'S'                        // Check final status
];
```

## Performance Considerations

### Impact on System Performance

1. **Diagnostic Commands:**
   - POST: 2-20 seconds depending on repairs needed
   - DEEP: 30-300 seconds for complete validation
   - STRESS: 10-60 seconds for 100 records

2. **Failure Injection:**
   - Most operations complete in <1 second
   - WEAR_TEST duration scales with cycle count
   - RANDOM_CORRUPT scales with sector count

3. **Production Impact:**
   - Zero impact when `TESTING_MODE` not defined
   - No performance degradation
   - No memory overhead

## Maintenance and Updates

### Adding New Failure Injection Methods

1. **Add method declaration** to `FlashRingBuffer.h` within `#ifdef TESTING_MODE`
2. **Implement method** in `FlashRingBuffer_part3.cpp`
3. **Add wrapper** to `FlashTelemetryManager.h` and `.cpp`
4. **Add stub** to `TelemetryPipeline.h` (return `false` for failure injection)
5. **Add command processing** to `main.cpp` within `#ifdef TESTING_MODE`
6. **Update help text** in main.cpp
7. **Update documentation** in this file and test strategy

### Version Control Considerations

- Keep failure injection code in separate files when possible
- Use clear commit messages indicating test-only changes
- Tag production releases to exclude TESTING_MODE
- Document all failure injection capabilities in release notes

## Conclusion

The failure injection system provides comprehensive testing capabilities for the flash buffer persistence system. The design ensures:

- **Safety** through compile-time protection
- **Compatibility** across pipeline types  
- **Comprehensive coverage** of failure scenarios
- **Clear feedback** and logging
- **Easy integration** with existing systems

This implementation enables confident deployment of the flash persistence system by thoroughly validating its recovery and robustness mechanisms under controlled failure conditions.