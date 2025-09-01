# WebSerial Command System Documentation

## Overview

The Lemon-IO device features a comprehensive WebSerial command system that provides remote access to the production flash persistence system through a web browser interface. This system enables both operational control of the dual-pipeline telemetry system and advanced diagnostic capabilities for field deployment validation.

## System Architecture

### Components

1. **Web Interface** (`logs_page.html`) - Browser-based serial console
2. **WebSerial Library** - Handles WebSocket communication at `/webserialws` 
3. **NetworkManager** - Processes incoming commands and routes to handlers
4. **Dual Pipeline System** - Runtime-switchable PSRAM and Flash telemetry pipelines
5. **Command Processors** - Execute operational and diagnostic commands
6. **Flash Diagnostic System** - Advanced flash buffer health monitoring capabilities

### Communication Flow

```
Web Browser → WebSocket (/webserialws) → WebSerial Library → NetworkManager → Command Handlers → Dual Pipeline System
                                                                                      ↓
Web Browser ← USB_SERIAL_PRINTF Output ← Command Results ← Production/Diagnostic Operations ←┘
```

## WebSerial Output Routing

**Important**: The system uses mutually exclusive output routing controlled by the `WEB_SERIAL` macro:

- **`WEB_SERIAL` defined**: All `USB_SERIAL_PRINTF` output goes to WebSerial (web interface)
- **`WEB_SERIAL` undefined**: All `USB_SERIAL_PRINTF` output goes to USB Serial port

This means when using the web interface, all diagnostic output, command results, and system logs appear in the browser console rather than the physical USB serial connection.

## Pipeline System Architecture

### Compile-Time Pipeline Selection

The Lemon-IO system uses **compile-time pipeline selection** via a simple macro:

```cpp
// Uncomment to use Flash persistence instead of PSRAM telemetry pipeline
// #define USE_FLASH_TELEMETRY

#ifdef USE_FLASH_TELEMETRY
FlashTelemetryManager telemetryPipeline;    // Flash-based persistent pipeline
#else
TelemetryPipeline telemetryPipeline;        // Original PSRAM-based pipeline  
#endif
```

### Pipeline Characteristics

| Feature | PSRAM Pipeline | Flash Pipeline |
|---------|----------------|----------------|
| **Storage Type** | Volatile (lost on power-off) | Persistent (survives power-off) |
| **Capacity** | 2MB | 10MB (2,560 sectors) |
| **Use Case** | Development, temporary storage | Marine operations, field deployment |
| **Data Retention** | Until restart | ~12 hours @ 224-byte messages |
| **Performance** | Higher speed | Optimized with RAM buffering |

## Command Categories

### Production Commands

These operational commands control the core system functionality:

| Command | Description | Action |
|---------|-------------|---------|
| **`F`** | Show Flash Persistence Setting | Displays current compile-time pipeline selection and instructions to change it |
| **`S`** | Show System Status | Displays active pipeline, connectivity, and system state |
| **`R`** | Factory Reset Flash | Resets flash storage to factory state |
| **`H`**, **`?`** | Show Help | Displays complete command reference |

### Testing/Simulation Commands

These commands simulate real-world conditions for development and validation:

| Command | Description | Action |
|---------|-------------|---------|
| **`D`** | Disconnect WiFi | Simulates offline/at-sea conditions |
| **`C`** | Connect WiFi | Simulates return to harbor with connectivity |

### Flash Diagnostic Commands (WebSerial Only)

These advanced diagnostic commands provide comprehensive health monitoring for the flash persistence system:

| Command | Description | Duration | Purpose |
|---------|-------------|----------|---------|
| **`POST`** | Power-On Self Test | 2-20 seconds | Comprehensive system validation with auto-repair |
| **`DEEP`** | Deep Sector Validation | 30-300 seconds | Complete flash integrity verification |
| **`STRESS`** | Stress Test (100 records) | 10-60 seconds | High-volume read/write performance testing |
| **`RECOVERY`** | Power-Loss Recovery Test | 5-30 seconds | Validates power-loss recovery mechanisms |

**Note**: These diagnostic commands work on the production flash system to verify its health and performance. They can be run safely during field deployment to validate system integrity.

## Web Interface Usage

### Accessing the Serial Console

1. **Connect to Lemon-IO WiFi network**
2. **Navigate to**: `http://[lemon-ip]/logs` or use device's web interface
3. **WebSocket connection**: Automatically connects to `/webserialws`
4. **Real-time logging**: All system output appears in the console

### Command Interface Elements

#### **Custom Command Input**
- **Text field**: Enter any command (single character or extended)
- **"Write Bytes" button**: Sends the command
- **Enter key**: Also sends the command

#### **Quick Command Dropdown** 
Pre-defined commands organized by category:

**Production Commands:**
- Show System Status
- Toggle Flash Persistence Enable/Disable  
- Factory Reset Flash Storage

**Testing Commands:**
- Disconnect WiFi (Simulate Offline)
- Connect WiFi (Simulate Online)

**Flash Diagnostics:**
- Run Power-On Self Test
- Run Deep Sector Validation  
- Run Stress Test (100 records)
- Run Power-Loss Recovery Test

**System Commands:**
- Show Help
- OTA Off
- Reboot

#### **Console Controls**
- **Clear**: Clears the console output
- **Save Log**: Downloads complete session log
- **Scroll Controls**: Navigate through output history
- **Auto-scroll**: Toggle automatic scrolling to latest output

## Command Processing Architecture

### NetworkManager Integration

```cpp
// WebSerial command callbacks (set in main.cpp)
networkManager.webSerialCommandCallback = [](char command) {
    processSerialCommand(command);  // Handle single characters
};

networkManager.webSerialExtendedCommandCallback = [](const String& command) {
    processExtendedCommand(command);  // Handle multi-character commands
};
```

### Command Flow

1. **Web Interface** sends command via WebSocket
2. **WebSerial Library** receives and calls `NetworkManager::webSerialReceiveMessage()`
3. **NetworkManager** determines command type:
   - Single character → calls `webSerialCommandCallback`
   - Multi-character → calls `webSerialExtendedCommandCallback`  
4. **main.cpp** processes command and executes appropriate action
5. **Results** are output via `USB_SERIAL_PRINTF` (appears in web console)

## Flash Diagnostic System Integration

### Compile-Time Pipeline Integration

The system routes commands to the appropriate pipeline based on compile-time selection:

```cpp
#ifdef USE_FLASH_TELEMETRY
FlashTelemetryManager telemetryPipeline;    // Flash-based persistent pipeline
#else
TelemetryPipeline telemetryPipeline;        // Original PSRAM-based pipeline  
#endif

// Unified API - commands work with both pipeline types
telemetryPipeline.performPowerOnSelfTest(true);    // Full implementation or stub
```

### FlashTelemetryManager Diagnostic Methods

The flash diagnostic commands delegate to FlashTelemetryManager, which provides safe access to FlashRingBuffer diagnostics:

```cpp
// Extended diagnostic methods available:
bool performPowerOnSelfTest(bool auto_repair = true);
bool performDeepSectorValidation();  
bool performPowerLossRecoveryTest();
bool performStressTest(uint32_t num_records = 1000);
```

### Safety Features

- **Compile-Time Selection**: Pipeline type is determined at build time, preventing runtime configuration errors
- **Unified API**: Both pipeline types implement the same diagnostic methods (full implementation or stubs)
- **Initialization Check**: FlashTelemetryManager verifies flash buffer is properly initialized
- **Graceful Degradation**: TelemetryPipeline stubs return `true` for compatibility
- **Error Handling**: FlashTelemetryManager provides descriptive error messages for actual diagnostics
- **Result Reporting**: Clear PASSED/FAILED status for each diagnostic test

## Testing Workflows

### Basic System Validation

1. **Connect to web interface**
2. **Send `S` command** - Verify current system status and active pipeline
3. **Send `F` command** - Display current compile-time pipeline setting
4. **If needed**: Uncomment/comment `#define USE_FLASH_TELEMETRY` in main.cpp and recompile
5. **Send `S` command** - Confirm correct pipeline is active

### Production Flash System Validation

**Note**: This workflow requires `#define USE_FLASH_TELEMETRY` to be enabled at compile time.

```
1. S       → Check current pipeline status  
2. F       → Confirm "Flash persistence is ENABLED (compile-time setting)"
3. POST    → Validate system integrity and auto-repair
4. STRESS  → Test high-volume operations  
5. DEEP    → Comprehensive sector validation (optional - takes 30-300s)
8. S       → Final system health check
```

### Network Connectivity Testing

```
1. D → Disconnect WiFi (simulate offline)
2. S → Verify flash buffer activated
3. Wait 30 seconds → Allow data accumulation
4. C → Reconnect WiFi  
5. S → Monitor flash buffer draining to MQTT
```

## Real-World Testing Scenarios

### Dive Trip Simulation

**Scenario**: Test offline data persistence during diving operations  
**Prerequisite**: System compiled with `#define USE_FLASH_TELEMETRY` enabled

```
1. S → Verify flash persistence is active
2. D → Disconnect WiFi (simulate going offshore)
3. Wait 5-10 minutes → Simulate dive duration
4. C → Connect WiFi (simulate return to harbor)
5. Monitor → Watch flash data upload to MQTT
6. RECOVERY → Validate power-loss recovery
```

### Extended Diagnostics

**Scenario**: Comprehensive system health check

```  
1. POST → Full system validation (2-20 seconds)
2. DEEP → Complete flash integrity check (30-300 seconds)  
3. STRESS → High-volume operation test (10-60 seconds)
4. S → Review final system status
```

## Diagnostic Output Interpretation

### Power-On Self Test (POST)
```
Expected Output:
=== FlashRingBuffer Power-On Self Test ===
POST: Partition access... OK
POST: State validation... OK  
POST: Sector scan... OK
POST: Auto-repair... NONE NEEDED
POST: Performance check... OK
=== Power-On Self Test: PASSED (3.2 seconds) ===
```

### Deep Sector Validation
```
Expected Output:
=== FlashRingBuffer Deep Sector Validation ===
Deep validation: Scanning 2560 sectors...
Deep validation: Progress 512/2560 sectors, 8756 records
Deep validation: Progress 1024/2560 sectors, 17512 records
...
=== Deep Sector Validation: PASSED ===
```

### Stress Test
```
Expected Output:
=== FlashRingBuffer Stress Test (100 records) ===
Stress test: Writing 100 records with varying sizes...
Stress test: Written 100/100 records (0 errors)
Stress test: Reading back 100 records...
=== Stress Test: PASSED ===
```

## Error Conditions and Troubleshooting

### Common Issues

#### **"Not in flash mode"**
- **Cause**: System compiled with PSRAM pipeline (`USE_FLASH_TELEMETRY` not defined)
- **Solution**: Uncomment `#define USE_FLASH_TELEMETRY` in main.cpp, recompile, and flash the system

#### **"Flash buffer not initialized"**  
- **Cause**: Flash partition access failure or initialization error
- **Solution**: Check partition table, run POST with auto-repair

#### **WebSocket connection failures**
- **Cause**: Network issues or WebSerial disabled
- **Solution**: Refresh page, check WiFi connection, verify `enableWebSerial` setting

#### **Commands not responding**
- **Cause**: WebSerial callbacks not set or processing error
- **Solution**: Check callback initialization in NetworkManager setup

### Diagnostic Steps

1. **Connection Issues**:
   ```
   1. Check WiFi connection
   2. Navigate to correct IP address  
   3. Verify /logs page loads
   4. Check WebSocket connection status in browser
   ```

2. **Command Issues**:
   ```
   1. Send 'S' to verify basic communication
   2. Check pipeline type in response
   3. Send 'F' to confirm compile-time setting
   4. If wrong pipeline, recompile with correct #define setting
   ```

3. **Flash Buffer Issues**:
   ```
   1. Run POST to check system health
   2. Use auto-repair if corruption detected
   3. Factory reset with 'R' if severe issues
   4. Check partition table configuration
   ```

## Security Considerations

- **Local Network Access**: WebSerial interface is accessible to any device on the same network
- **Command Execution**: All commands execute with full system privileges
- **Data Exposure**: Serial output may contain sensitive system information
- **Factory Reset**: 'R' command permanently erases all flash data

## Implementation Files

### Core Files
- **`src/logs_page.html`** - Web interface UI and JavaScript
- **`src/logs_page.h`** - Generated C++ header from HTML
- **`src/NetworkManager.cpp`** - WebSerial message processing
- **`src/NetworkManager.h`** - WebSerial callback declarations
- **`src/main.cpp`** - Command processing and callback setup

### Flash System Files  
- **`src/FlashRingBuffer.h/.cpp`** - Core flash ring buffer implementation
- **`src/FlashTelemetryManager.h/.cpp`** - High-level flash interface
- **`partitions/ota_spiffs_16MB.csv`** - Flash partition configuration

## Future Enhancements

### Planned Improvements
- **Authentication**: Add login/password protection
- **Remote Monitoring**: Real-time system metrics dashboard  
- **Automated Testing**: Scheduled diagnostic routines
- **Log Export**: Download diagnostic reports
- **Configuration Management**: Remote system configuration updates

### Integration Opportunities
- **MQTT Integration**: Remote command execution via MQTT
- **Status Dashboard**: Real-time flash buffer statistics
- **Alert System**: Automatic notification of system issues
- **Performance Monitoring**: Historical diagnostic data tracking

## Conclusion

The WebSerial command system provides comprehensive remote access to the Lemon-IO flash persistence system, enabling thorough testing and diagnostics without physical access to the device. The combination of traditional serial commands and extended diagnostic capabilities makes it suitable for both development testing and field deployment validation.

The system's compile-time pipeline selection provides a clean, non-intrusive approach while maintaining backward compatibility with the original PSRAM-based telemetry pipeline. The unified diagnostic API with stub functions ensures that commands work consistently regardless of the selected pipeline. The extensive diagnostic capabilities enable confident deployment of the flash persistence system in demanding marine environments.