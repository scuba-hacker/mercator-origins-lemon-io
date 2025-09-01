# Failure Injection API Reference

## Overview

This document provides a complete API reference for the failure injection system, including method signatures, parameters, return values, and usage examples.

## Build Configuration

### Required Preprocessor Definitions

```cpp
#define TESTING_MODE           // Enable failure injection methods
#define USE_FLASH_TELEMETRY    // Enable flash telemetry system (recommended for testing)
```

### Conditional Compilation

All failure injection methods are protected by compile-time guards:

```cpp
#ifdef TESTING_MODE
// Failure injection methods only available in test builds
#endif
```

## FlashRingBuffer API

### Core Failure Injection Methods

Located in: `src/FlashRingBuffer.h` and `src/FlashRingBuffer_part3.cpp`

#### `bool injectSectorCorruption(uint32_t sector_index)`

**Purpose:** Corrupt a sector header magic number to test auto-repair functionality

**Parameters:**
- `sector_index` (uint32_t): Target sector number (0 to TOTAL_SECTORS-1)

**Returns:**
- `true`: Corruption successfully injected
- `false`: Operation failed (not initialized, invalid sector)

**Implementation:**
- Overwrites sector magic number with `0xDEADBEEF`
- Validates sector index against `TOTAL_SECTORS` (2560)
- Logs operation details via USB_SERIAL_PRINTF

**Usage Example:**
```cpp
FlashRingBuffer buffer;
if (buffer.injectSectorCorruption(5)) {
    // Sector 5 magic number corrupted
    // Run POST to test auto-repair
}
```

**WebSerial Command:** `CORRUPT_SECTOR 5`

---

#### `bool corruptPersistedState()`

**Purpose:** Corrupt EEPROM state to test flash-based state recovery

**Parameters:** None

**Returns:**
- `true`: State corruption successfully injected
- `false`: Operation failed (not initialized)

**Implementation:**
- Sets all EEPROM state fields to `0xFFFFFFFF` (invalid values)
- Affects: head_sector, tail_sector, head_sector_used, next_seq_number, write_count
- **Requires system restart** to test recovery

**Usage Example:**
```cpp
FlashRingBuffer buffer;
if (buffer.corruptPersistedState()) {
    // EEPROM state corrupted
    // Restart system to test recovery from flash scan
}
```

**WebSerial Command:** `CORRUPT_STATE`

---

#### `bool simulateIncompleteWrite()`

**Purpose:** Create incomplete record to test power-loss recovery

**Parameters:** None

**Returns:**
- `true`: Incomplete record successfully created
- `false`: Operation failed (not initialized, write error)

**Implementation:**
- Creates 224-byte test record with correct CRC
- Writes header and payload but leaves `valid` flag as `0xFF`
- Simulates power-loss before record commit
- POST should detect and truncate incomplete record

**Usage Example:**
```cpp
FlashRingBuffer buffer;
if (buffer.simulateIncompleteWrite()) {
    // Incomplete record created
    // Run POST to test truncation and recovery
}
```

**WebSerial Command:** `SIMULATE_POWER_LOSS`

---

#### `bool corruptRingPointers()`

**Purpose:** Set invalid ring buffer pointers to test boundary validation

**Parameters:** None

**Returns:**
- `true`: Pointers successfully corrupted
- `false`: Operation failed (not initialized)

**Implementation:**
- Sets head_sector to `TOTAL_SECTORS + 100`
- Sets tail_sector to `TOTAL_SECTORS + 200`
- Sets head_sector_used to `SECTOR_SIZE + 1000`
- Saves corrupted state to EEPROM

**Usage Example:**
```cpp
FlashRingBuffer buffer;
if (buffer.corruptRingPointers()) {
    // Ring pointers corrupted beyond valid range
    // Run POST to test pointer validation and correction
}
```

**WebSerial Command:** `CORRUPT_POINTERS`

---

#### `bool acceleratedWearTest(uint32_t cycles)`

**Purpose:** Perform accelerated wear testing by rapidly cycling through sectors

**Parameters:**
- `cycles` (uint32_t): Number of sector advancement cycles to perform

**Returns:**
- `true`: Wear test completed successfully
- `false`: Operation failed (not initialized, write errors)

**Implementation:**
- Fills current sector with dummy data to force advancement
- Advances to next sector and repeats for specified cycles
- Yields every 10 cycles to prevent watchdog issues
- Logs progress every 100 cycles
- Tracks total writes and performance metrics

**Usage Example:**
```cpp
FlashRingBuffer buffer;
if (buffer.acceleratedWearTest(1000)) {
    // 1000 sector advancement cycles completed
    // Check write count and performance statistics
}
```

**WebSerial Command:** `WEAR_TEST 1000`

---

#### `bool injectRandomCorruption(uint32_t num_sectors)`

**Purpose:** Apply random corruption across multiple sectors

**Parameters:**
- `num_sectors` (uint32_t): Number of sectors to corrupt (max 25% of total)

**Returns:**
- `true`: Corruption successfully applied to requested sectors
- `false`: Operation failed (not initialized, invalid count)

**Implementation:**
- Limited to maximum 25% of total sectors (640 sectors)
- Avoids sectors near current head/tail (±5 sectors)
- Randomly selects corruption type: magic, CRC, or usage field
- Uses random sector selection to distribute corruption

**Usage Example:**
```cpp
FlashRingBuffer buffer;
if (buffer.injectRandomCorruption(10)) {
    // 10 random sectors corrupted with mixed corruption types
    // Run POST to test comprehensive auto-repair
}
```

**WebSerial Command:** `RANDOM_CORRUPT 10`

---

#### `bool simulatePartitionFailure()`

**Purpose:** Simulate partition access failure

**Parameters:** None

**Returns:**
- `true`: Partition failure successfully simulated
- `false`: Operation failed (not initialized)

**Implementation:**
- Sets partition pointer to NULL
- **Requires system restart** to restore partition access
- Tests partition failure handling and PSRAM fallback

**Usage Example:**
```cpp
FlashRingBuffer buffer;
if (buffer.simulatePartitionFailure()) {
    // Partition access disabled
    // Restart system to restore normal operation
}
```

**WebSerial Command:** `PARTITION_FAIL`

---

#### `bool injectCRCCorruption(uint32_t sector_index)`

**Purpose:** Corrupt sector header CRC to test validation

**Parameters:**
- `sector_index` (uint32_t): Target sector number (0 to TOTAL_SECTORS-1)

**Returns:**
- `true`: CRC corruption successfully injected
- `false`: Operation failed (not initialized, invalid sector)

**Implementation:**
- Overwrites sector CRC32 field with `0xDEADBEEF`
- Validates sector index against total sectors
- Tests CRC validation during sector reads

**Usage Example:**
```cpp
FlashRingBuffer buffer;
if (buffer.injectCRCCorruption(7)) {
    // Sector 7 CRC corrupted
    // POST should detect and repair CRC
}
```

**WebSerial Command:** `CORRUPT_CRC 7`

---

#### `void enableFailureInjection()`

**Purpose:** Display failure injection status and warnings

**Parameters:** None

**Returns:** None (void)

**Implementation:**
- Displays warning message about failure injection being enabled
- Provides confirmation that testing mode is active
- Used for build verification and status checking

**Usage Example:**
```cpp
FlashRingBuffer buffer;
buffer.enableFailureInjection();
// Displays: "WARNING: Failure injection methods enabled for testing"
```

**WebSerial Command:** `ENABLE_FAIL_INJECT`

## FlashTelemetryManager API

### Compatibility Wrapper Methods

Located in: `src/FlashTelemetryManager.h` and `src/FlashTelemetryManager.cpp`

All FlashTelemetryManager failure injection methods are wrappers that:

1. **Validate storage mode** (must be FLASH_ONLY)
2. **Check initialization** (flash buffer must be ready)
3. **Delegate to FlashRingBuffer** (actual implementation)
4. **Provide error logging** (descriptive failure messages)

#### Method Signatures

```cpp
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

#### Common Return Behavior

**Success Path:**
- Storage mode is FLASH_ONLY
- Flash buffer is initialized
- Delegates to FlashRingBuffer method
- Returns result from FlashRingBuffer

**Failure Path:**
- Not in FLASH_ONLY mode → logs mode error, returns `false`
- Flash buffer not initialized → logs init error, returns `false`
- FlashRingBuffer method fails → returns `false`

#### Usage Example

```cpp
FlashTelemetryManager manager;
manager.setStorageMode(FLASH_ONLY);

if (manager.injectSectorCorruption(5)) {
    // Success: In flash mode, initialized, corruption injected
} else {
    // Failure: Wrong mode, not initialized, or injection failed
    // Check logs for specific error message
}
```

## TelemetryPipeline API

### Stub Methods

Located in: `TelemetryPipeline.h` (added by user)

All TelemetryPipeline failure injection methods are stubs for compatibility:

#### Method Signatures

```cpp
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

#### Stub Behavior

- **All methods return `false`** (operation not supported)
- **enableFailureInjection() is no-op** (no action required)
- **No error logging** (silent failure expected)
- **Parameters ignored** (no validation needed)

#### Usage Example

```cpp
TelemetryPipeline pipeline;

if (pipeline.injectSectorCorruption(5)) {
    // Never executes - always returns false
} else {
    // Always executes - stub returns false
    // Main.cpp will log "FAILED (not in flash mode or not supported)"
}
```

## WebSerial Commands

### Command Format

**Single Commands:** `COMMAND`
**Parameterized Commands:** `COMMAND parameter`

### Command Processing

Located in: `src/main.cpp` function `processExtendedCommand(const String& command)`

#### Parameter Parsing

```cpp
// Extract numeric parameter from command
int sector_num = 5; // Default value
if (command.indexOf(' ') > 0) {
    sector_num = command.substring(command.indexOf(' ') + 1).toInt();
}
```

#### Response Format

```cpp
// Success response
USB_SERIAL_PRINTF(">>> FAILURE INJECTION: [Operation] [RESULT]\n");

// Examples:
">>> FAILURE INJECTION: Sector corruption INJECTED"
">>> FAILURE INJECTION: Wear test COMPLETED"
">>> FAILURE INJECTION: State corruption FAILED (not in flash mode or not supported)"
```

### Available Commands

| Command | Parameters | Description | Default Value |
|---------|------------|-------------|---------------|
| `CORRUPT_SECTOR` | `[sector_num]` | Corrupt sector magic number | 5 |
| `CORRUPT_STATE` | None | Corrupt EEPROM state | N/A |
| `SIMULATE_POWER_LOSS` | None | Create incomplete record | N/A |
| `CORRUPT_POINTERS` | None | Invalid ring pointers | N/A |
| `WEAR_TEST` | `[cycles]` | Accelerated wear test | 100 |
| `RANDOM_CORRUPT` | `[num_sectors]` | Random multi-sector corruption | 3 |
| `PARTITION_FAIL` | None | Simulate partition failure | N/A |
| `CORRUPT_CRC` | `[sector_num]` | Corrupt sector CRC | 7 |
| `ENABLE_FAIL_INJECT` | None | Show injection status | N/A |

### Command Examples

```
CORRUPT_SECTOR          # Corrupts sector 5 (default)
CORRUPT_SECTOR 10       # Corrupts sector 10
WEAR_TEST               # 100 cycles (default)
WEAR_TEST 1000          # 1000 cycles
RANDOM_CORRUPT          # Corrupts 3 sectors (default)
RANDOM_CORRUPT 5        # Corrupts 5 sectors
CORRUPT_CRC             # Corrupts sector 7 CRC (default)
CORRUPT_CRC 15          # Corrupts sector 15 CRC
```

## Error Handling

### Common Error Conditions

#### Not Initialized
```
FlashRingBuffer::methodName() - Not initialized
```
**Cause:** Flash buffer init() not called or failed
**Solution:** Call init() method and verify partition access

#### Invalid Parameters
```
FlashRingBuffer::methodName() - Invalid sector X (max Y)
```
**Cause:** Sector number exceeds TOTAL_SECTORS
**Solution:** Use valid sector range (0 to 2559)

#### Wrong Storage Mode
```
FlashTelemetryManager::methodName() - Not in flash mode (mode: X)
```
**Cause:** Storage mode is not FLASH_ONLY
**Solution:** Call setStorageMode(FLASH_ONLY)

#### Write Failures
```
FlashRingBuffer::methodName() - Write failed: ESP_ERR_XXX
```
**Cause:** Flash partition write operation failed
**Solution:** Check partition configuration and flash health

### Error Recovery

1. **Initialization Failures:**
   - Restart system
   - Check partition table
   - Verify flash hardware

2. **Parameter Errors:**
   - Use valid parameter ranges
   - Check documentation for limits

3. **Mode Errors:**
   - Verify compile-time flags
   - Set correct storage mode
   - Check system status with 'S' command

## Performance Characteristics

### Operation Timing

| Operation | Typical Duration | Notes |
|-----------|-----------------|-------|
| `injectSectorCorruption` | <1 ms | Single write operation |
| `corruptPersistedState` | <10 ms | Multiple EEPROM writes |
| `simulateIncompleteWrite` | <5 ms | Header + payload write |
| `corruptRingPointers` | <10 ms | EEPROM state update |
| `acceleratedWearTest(100)` | 10-30 seconds | Scales with cycle count |
| `injectRandomCorruption(5)` | <50 ms | Scales with sector count |
| `simulatePartitionFailure` | <1 ms | Pointer assignment |
| `injectCRCCorruption` | <1 ms | Single write operation |

### Memory Usage

- **No additional RAM** required for failure injection
- **No persistent storage** beyond normal flash operations
- **Temporary buffers** reused from existing system allocations

### Impact on System Performance

- **Zero impact** when `TESTING_MODE` not defined
- **Minimal impact** during normal operation (methods not called)
- **Temporary impact** during active failure injection testing

## Integration Examples

### Automated Test Sequence

```cpp
// Example automated failure injection test
bool runFailureInjectionTest(FlashTelemetryManager& manager) {
    // Phase 1: Baseline
    if (!manager.performPowerOnSelfTest(true)) {
        return false; // System not healthy
    }
    
    // Phase 2: Single sector corruption
    if (!manager.injectSectorCorruption(5)) {
        return false; // Injection failed
    }
    
    if (!manager.performPowerOnSelfTest(true)) {
        return false; // Auto-repair failed
    }
    
    // Phase 3: Multi-sector corruption
    if (!manager.injectRandomCorruption(3)) {
        return false; // Injection failed
    }
    
    if (!manager.performPowerOnSelfTest(true)) {
        return false; // Recovery failed
    }
    
    // Phase 4: Wear testing
    if (!manager.acceleratedWearTest(100)) {
        return false; // Wear test failed
    }
    
    return true; // All tests passed
}
```

### Web Interface Integration

```javascript
// Example web interface command sender
function sendFailureInjectionCommand(command, parameter = null) {
    const fullCommand = parameter ? `${command} ${parameter}` : command;
    
    // Send via existing WebSerial interface
    webSerial.send(fullCommand);
    
    // Monitor response
    webSerial.onMessage((response) => {
        if (response.includes('FAILURE INJECTION:')) {
            handleFailureInjectionResponse(response);
        }
    });
}

// Usage examples
sendFailureInjectionCommand('CORRUPT_SECTOR', 10);
sendFailureInjectionCommand('WEAR_TEST', 500);
sendFailureInjectionCommand('CORRUPT_STATE');
```

## Best Practices

### Testing Guidelines

1. **Always establish baseline** with POST before injection
2. **Test one failure type at a time** for clear results  
3. **Verify recovery** with POST after each injection
4. **Monitor system status** with 'S' command regularly
5. **Document all test results** for analysis

### Safety Recommendations

1. **Never use in production** - only enable TESTING_MODE for development
2. **Backup important data** before extensive testing
3. **Monitor system stability** during long-duration tests
4. **Use reasonable parameters** - avoid excessive corruption
5. **Restart system** if behavior becomes unstable

### Development Workflow

1. **Start with PSRAM mode** to verify stub behavior
2. **Switch to flash mode** for actual testing
3. **Begin with diagnostic commands** (POST, STRESS)
4. **Progress to simple injections** (single sector corruption)
5. **Advance to complex scenarios** (multi-sector, wear testing)

This API reference provides complete documentation for all failure injection methods, enabling comprehensive testing and validation of the flash buffer persistence system.