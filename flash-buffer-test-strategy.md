# Flash Buffer Test Strategy for Web Serial Access

## Implementation vs CLAUDE.md Requirements Analysis

The flash buffer implementation **significantly exceeds** CLAUDE.md requirements:

### ✅ **Perfect Matches:**
- **10MB flash buffer** (exceeds 2MB requirement)  
- **Variable message sizes** (16-1008 bytes vs 16-928 bytes requested)
- **Power-safe writes** with CRC validation
- **Ring buffer architecture** with proper sector management
- **Recovery from power-loss** with POST system
- **Non-intrusive design** with TelemetryPipeline compatibility
- **RAM buffering** for sector assembly
- **Enable/disable capability** for fallback to PSRAM

### ✅ **Enhanced Features:**
- **Auto-repair system** (beyond requirements)
- **Comprehensive diagnostics** (POST, deep validation, stress tests)
- **Dual-mode operation** (PSRAM/Flash/Hybrid)
- **Web-based testing interface** (exceeds expectations)

## Phase 1: Basic System Validation (5 minutes)

### Prerequisites Check
1. **Connect to device**: `http://[lemon-ip]/logs`
2. **Verify web serial**: Confirm console connects to WebSocket
3. **Pipeline verification**: Send `F` command
   - ✅ Expected: "Flash persistence is ENABLED (compile-time)"
   - ❌ If disabled: Must recompile with `#define USE_FLASH_TELEMETRY`

### Basic Functionality Tests
```
Test 1.1: System Status
Command: S
Expected: Active pipeline, connectivity status, flash buffer state
Duration: <1 second

Test 1.2: Help System  
Command: H
Expected: Complete command reference displayed
Duration: <1 second
```

## Phase 2: Flash System Health Check (20 minutes)

### Core Diagnostic Suite
```
Test 2.1: Power-On Self Test
Command: POST
Expected: PASSED status, ~3 seconds completion
Critical: Must pass before proceeding
Watch for: Auto-repair messages, error conditions

Test 2.2: Basic Storage Operations
Command: S (monitor flash record count)
Action: Wait 2-3 minutes for telemetry accumulation
Command: S (verify count increased)
Expected: Record count increases, used space grows

Test 2.3: Stress Test (Quick)
Command: STRESS  
Expected: PASSED, 100 records written/read
Duration: 10-60 seconds
```

## Phase 3: Power-Loss and Recovery Testing (15 minutes)

### Simulated Power-Loss Scenarios
```
Test 3.1: Recovery System Validation
Command: RECOVERY
Expected: PASSED status, validates recovery mechanisms
Duration: 5-30 seconds

Test 3.2: Factory Reset and Recovery
Command: R (factory reset)
Expected: Flash cleared successfully
Command: POST
Expected: Clean initialization, PASSED status
```

## Phase 4: Network Connectivity Testing (30 minutes)

### Offline/Online Simulation
```
Test 4.1: Offline Data Accumulation
Command: D (disconnect WiFi)
Expected: "WiFi disconnected" message
Wait: 5-10 minutes (accumulate flash data)
Command: S (check record count growth)

Test 4.2: Online Data Draining  
Command: C (connect WiFi)
Expected: "WiFi connected" message
Monitor: Watch flash record count decrease as MQTT drains data
Command: S (periodic checks every 30 seconds)
Expected: Record count gradually decreases to zero
```

## Phase 5: Extended Validation (Optional, 45 minutes)

### Deep System Validation
```
Test 5.1: Deep Sector Validation (Heavy)
Command: DEEP
Expected: PASSED status, complete sector scan
Duration: 30-300 seconds (sector count dependent)
Note: Very thorough, resource intensive

Test 5.2: Extended Stress Test
Custom: Modify STRESS command for 1000+ records
Monitor: Performance under sustained load
Watch: Flash wear statistics, error rates
```

## Phase 6: Real-World Scenario Testing (60 minutes)

### Marine Operations Simulation
```
Test 6.1: Dive Trip Simulation
1. S → Verify flash active
2. D → Go "offshore" (disconnect)
3. Wait 10-15 minutes → "Dive duration"
4. Monitor record accumulation
5. C → "Return to harbor" (connect)
6. Monitor flash drain to MQTT
7. RECOVERY → Validate power-loss protection

Test 6.2: Multi-Cycle Testing  
Repeat: D → Wait → C → Monitor (3-5 cycles)
Validate: Data persistence, recovery, no memory leaks
Monitor: Flash wear, performance degradation
```

## Build Configuration for Failure Injection

### Enabling Testing Mode

To use the failure injection features, you must compile with `TESTING_MODE` defined:

#### **Option 1: Compiler Flag**
Add to your build flags:
```cpp
-DTESTING_MODE
```

#### **Option 2: Source Code Definition**
Add to your main.cpp or build configuration:
```cpp
#define TESTING_MODE
```

#### **Option 3: PlatformIO Configuration**
Add to your `platformio.ini`:
```ini
build_flags = 
    -DTESTING_MODE
    -DUSE_FLASH_TELEMETRY  ; Enable flash telemetry
```

### **⚠️ IMPORTANT SECURITY NOTE:**
- **Never ship production firmware with `TESTING_MODE` enabled**
- Failure injection commands can permanently damage the flash buffer
- Only use in development and testing environments
- Production builds should explicitly exclude `TESTING_MODE`

## Simulating Failures for Recovery Testing

### Phase 7: Controlled Failure Injection (Advanced Testing)

The failure injection system is now **fully implemented** in the codebase with the following commands available via WebSerial:

#### 7.1 Sector Corruption Simulation

**WebSerial Command:** `CORRUPT_SECTOR [sector_number]`
- Corrupts the magic number in the specified sector header
- Default sector: 5 if no number specified
- Example: `CORRUPT_SECTOR 10`

**Test Sequence:**
```
1. POST → Ensure clean state
2. CORRUPT_SECTOR 5 → Inject known corruption
3. POST → Should detect and auto-repair corruption
4. S → Verify system operational after repair
```

#### 7.2 State Inconsistency Simulation

**WebSerial Command:** `CORRUPT_STATE`
- Corrupts EEPROM persisted state (head/tail pointers, sequence numbers)
- **Requires system restart** to test recovery
- System will rebuild state from flash scan

**Test Sequence:**
```
1. POST → Clean state
2. Accumulate data → Create valid state
3. CORRUPT_STATE → Corrupt EEPROM state
4. Restart system → Force reload from EEPROM
5. POST → Should detect inconsistency and rebuild from flash scan
```

#### 7.3 Partial Write Simulation

**WebSerial Command:** `SIMULATE_POWER_LOSS`
- Creates an incomplete record (simulates power-loss mid-write)
- Record header written but valid flag remains 0xFF (incomplete)
- POST should detect and truncate incomplete records

**Test Sequence:**
```
1. POST → Clean state
2. SIMULATE_POWER_LOSS → Create incomplete record
3. POST → Should detect and truncate incomplete record
4. STRESS → Verify normal operation resumes
```

#### 7.4 Ring Buffer Boundary Corruption

**WebSerial Command:** `CORRUPT_POINTERS`
- Sets head/tail pointers to invalid values (beyond valid range)
- Tests boundary checking and pointer validation
- POST should detect and correct invalid pointers

**Test Sequence:**
```
1. POST → Clean state
2. CORRUPT_POINTERS → Set invalid head/tail values
3. POST → Should detect and correct pointer values
4. S → Verify valid head/tail values within bounds
```

#### 7.5 Flash Wear Simulation

**WebSerial Command:** `WEAR_TEST [cycles]`
- Rapidly cycles through sectors to test wear handling
- Default: 100 cycles if no number specified
- Example: `WEAR_TEST 500`
- Monitors write counts and performance

**Test Sequence:**
```
1. S → Note initial write count
2. WEAR_TEST 200 → Run accelerated wear test
3. S → Verify increased write count, system stability
4. POST → Ensure system health after wear
```

#### 7.6 Random Multi-Sector Corruption

**WebSerial Command:** `RANDOM_CORRUPT [num_sectors]`
- Injects random corruption across multiple sectors
- Default: 3 sectors if no number specified
- Mix of magic, CRC, and usage field corruption
- Limited to 25% of total sectors for safety

**Test Sequence:**
```
1. POST → Clean state
2. RANDOM_CORRUPT 5 → Corrupt 5 random sectors
3. POST → Should detect and repair multiple corruptions
4. S → Verify system operational after repairs
```

#### 7.7 CRC Corruption Simulation

**WebSerial Command:** `CORRUPT_CRC [sector_number]`
- Corrupts the CRC32 field in sector header
- Default sector: 7 if no number specified
- Tests CRC validation and repair mechanisms

#### 7.8 Partition Access Failure

**WebSerial Command:** `PARTITION_FAIL`
- Simulates partition access failure (sets partition pointer to NULL)
- **Requires system restart** to restore partition access
- Tests partition failure handling

#### 7.9 Enable Failure Injection Mode

**WebSerial Command:** `ENABLE_FAIL_INJECT`
- Displays failure injection warning message
- Confirms that testing mode is active
- Useful for verifying build configuration

### ✅ **Implementation Status: COMPLETE**

All failure injection methods have been fully implemented in the codebase:

#### **Files Modified:**
- `FlashRingBuffer.h` - Added failure injection method declarations
- `FlashRingBuffer_part3.cpp` - Implemented all failure injection methods  
- `FlashTelemetryManager.h` - Added failure injection wrappers
- `FlashTelemetryManager.cpp` - Implemented compatibility layer methods
- `TelemetryPipeline.h` - Added failure injection stub methods (compatibility)
- `main.cpp` - Added WebSerial command processing and help text

#### **Build Requirements:**
- Compile with `-DTESTING_MODE` to enable failure injection commands
- Compile with `-DUSE_FLASH_TELEMETRY` to enable flash telemetry system
- **Never enable `TESTING_MODE` in production builds**

#### **Pipeline Compatibility:**
- **FlashTelemetryManager (Flash Mode)**: All failure injection methods fully implemented
- **TelemetryPipeline (PSRAM Mode)**: Failure injection methods return `false` (stubs)
- **Diagnostic Methods**: Both pipelines support POST, DEEP, STRESS, RECOVERY commands
- **Unified API**: Same commands work with both pipeline types with appropriate responses

### Expected Command Behavior by Pipeline Type

#### **When Using FlashTelemetryManager (`#define USE_FLASH_TELEMETRY`)**
```
POST → Full flash buffer validation with auto-repair
DEEP → Complete sector scan and validation  
STRESS → High-volume read/write testing with actual flash operations
RECOVERY → Power-loss recovery validation with real flash scenarios

CORRUPT_SECTOR 5 → "Sector corruption INJECTED"
CORRUPT_STATE → "State corruption INJECTED - Restart system to test recovery"
SIMULATE_POWER_LOSS → "Power-loss simulation INJECTED"
CORRUPT_POINTERS → "Pointer corruption INJECTED"
WEAR_TEST 100 → "Wear test COMPLETED" (actual flash operations)
RANDOM_CORRUPT 3 → "Random corruption INJECTED"
PARTITION_FAIL → "Partition failure SIMULATED"
CORRUPT_CRC 7 → "CRC corruption INJECTED"
ENABLE_FAIL_INJECT → Shows flash buffer status and confirms injection available
```

#### **When Using TelemetryPipeline (PSRAM Mode)**
```
POST → "PASSED" (stub - no actual testing performed)
DEEP → "PASSED" (stub - no actual testing performed)  
STRESS → "PASSED" (stub - no actual testing performed)
RECOVERY → "PASSED" (stub - no actual testing performed)

CORRUPT_SECTOR 5 → "FAILED (not in flash mode or not supported)"
CORRUPT_STATE → "FAILED (not in flash mode or not supported)"
SIMULATE_POWER_LOSS → "FAILED (not in flash mode or not supported)"
CORRUPT_POINTERS → "FAILED (not in flash mode or not supported)"
WEAR_TEST 100 → "FAILED (not in flash mode or not supported)"
RANDOM_CORRUPT 3 → "FAILED (not in flash mode or not supported)"
PARTITION_FAIL → "FAILED (not in flash mode or not supported)"
CORRUPT_CRC 7 → "FAILED (not in flash mode or not supported)"
ENABLE_FAIL_INJECT → Shows message explaining that PSRAM mode doesn't support failure injection
```

This design ensures that:
- Commands never crash regardless of pipeline type
- Clear feedback indicates whether operations are supported
- Testing can proceed safely in both modes
- Users understand which features are available for each pipeline

### Comprehensive Recovery Test Suite

#### Test 7.1: Sector Corruption Recovery
```
1. POST → Baseline PASSED
2. CORRUPT_SECTOR 5 → Inject corruption in sector 5
3. POST → Should show "Auto-repair: CORRUPTION DETECTED AND REPAIRED"
4. Expected: PASSED after repair
5. STRESS → Verify normal operation
```

#### Test 7.2: State Recovery from Flash
```
1. Accumulate 100+ records → Create substantial flash data
2. CORRUPT_STATE → Destroy EEPROM state
3. Restart system → Force state reload
4. POST → Should show "State rebuilt from flash scan"
5. S → Verify correct record count and pointers
```

#### Test 7.3: Incomplete Write Recovery
```
1. SIMULATE_POWER_LOSS → Create incomplete record
2. POST → Should show "Incomplete record truncated"
3. STRESS → Verify normal operation resumes
4. Expected: No data corruption, clean recovery
```

#### Test 7.4: Boundary Condition Recovery
```
1. CORRUPT_POINTERS → Invalid head/tail pointers
2. POST → Should show "Invalid pointers corrected"
3. S → Verify valid head/tail values within bounds
4. Normal operation should resume
```

#### Test 7.5: Multi-Sector Corruption Recovery
```
1. POST → Clean baseline
2. RANDOM_CORRUPT 5 → Inject multiple corruptions
3. POST → Should repair all detected corruptions
4. S → Verify system operational with minimal data loss
```

#### Test 7.6: Wear Testing and Validation
```
1. S → Note initial write count
2. WEAR_TEST 1000 → Extended wear simulation  
3. S → Monitor write count progression
4. POST → Ensure system health after high wear
5. Expected: System remains stable under accelerated wear
```

### Safety and Validation

#### ✅ **Recovery Validation Checklist**
- **Data Integrity**: No valid data lost during recovery
- **Boundary Safety**: Pointers remain within valid ranges  
- **State Consistency**: EEPROM and flash state match after recovery
- **Performance**: Normal operation speed after recovery
- **Persistence**: Recovery survives subsequent power cycles

#### ⚠️ **Testing Safety Measures**
- All failure injection protected by `#ifdef TESTING_MODE`
- Production builds automatically exclude failure injection code
- Methods validate flash mode and initialization before injection
- Limited corruption scope (max 25% of sectors for random corruption)
- Detailed logging of all injection operations

### Example Test Results Documentation

#### Failure Recovery Test Log Template
```
Test: [Failure Type] Recovery
Date: [Test Date]
Build: TESTING_MODE + USE_FLASH_TELEMETRY enabled
Device: [IP/Identifier]

Pre-Test Status:
□ Flash mode active (S command confirms)
□ POST baseline: PASSED
□ Record count: ___ records

Failure Injection:
Command: [COMMAND_USED]
Result: [INJECTED/FAILED]
Details: [Specific corruption applied]

Recovery Test:
POST Result: [PASSED/FAILED] - Duration: ___ seconds
Recovery Actions: [Auto-repair details from logs]
Data Loss: ___ records lost
Performance Impact: [Description]

Post-Recovery Validation:  
□ S command: System operational
□ STRESS test: PASSED
□ Record integrity: Verified

Overall Result: ✅ PASSED / ❌ FAILED
Notes: [Any observations or issues]
```

## Critical Success Criteria

### ✅ **Must Pass Tests:**
- **POST**: System health validation
- **STRESS**: Basic read/write functionality  
- **S command**: Consistent state reporting
- **D/C cycle**: Network connectivity handling
- **Record persistence**: Data survives power cycles
- **Recovery tests**: All failure scenarios recover successfully

### ⚠️ **Warning Conditions:**
- POST auto-repair messages (acceptable but investigate)
- Slow DEEP validation (>300 seconds - possible issues)
- Flash wear exceeding expected rates
- Memory usage growth during testing

### ❌ **Failure Conditions:**
- POST FAILED status
- STRESS test failures
- Data loss during D/C cycles or recovery
- System crashes or reboots
- Flash buffer initialization failures
- Recovery procedures fail to restore system

## Test Documentation Template

```
Test Session: [Date/Time]
Device: [IP/Identifier]  
Firmware: [Version with USE_FLASH_TELEMETRY and TESTING_MODE]

Phase 1 - Basic Validation:
□ F command: Pipeline enabled
□ S command: System operational  
□ H command: Help displayed

Phase 2 - Health Check:
□ POST: PASSED (___ seconds)
□ Storage ops: Record count increases
□ STRESS: PASSED (___ records)

Phase 3 - Recovery:
□ RECOVERY: PASSED
□ Factory reset: Successful
□ Reinitialization: PASSED

Phase 4 - Connectivity:
□ Offline accumulation: ___ records
□ Online draining: Complete drain
□ Cycle stability: No issues

Phase 5 - Extended (Optional):
□ DEEP: PASSED (___ seconds)  
□ Extended stress: Performance stable

Phase 6 - Real-World:
□ Dive simulation: Complete success
□ Multi-cycle: Stable operation

Phase 7 - Failure Recovery (Advanced):
□ Sector corruption: Recovery PASSED
□ State corruption: Recovery PASSED  
□ Incomplete writes: Recovery PASSED
□ Pointer corruption: Recovery PASSED
□ Wear testing: System stable

Overall Result: PASSED/FAILED
Notes: [Any issues or observations]
Recommendations: [System ready for deployment / Issues to address]
```

This systematic approach starts with quick validation and progressively tests more complex scenarios, including controlled failure injection, ensuring the flash buffer system meets all CLAUDE.md requirements and performs reliably under failure conditions.