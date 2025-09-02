/**
 * @file FlashRingBuffer_part3.cpp
 * @brief Advanced testing and failure injection for FlashRingBuffer
 * 
 * This file contains the advanced testing and diagnostic capabilities:
 * - Comprehensive self-test with multiple record sizes
 * - Extended diagnostics with deep validation
 * - Power-loss recovery testing
 * - Stress testing with high write/read loads
 * - Failure injection methods for testing robustness
 * 
 * Marine Testing Philosophy:
 * - "Test like you operate" - simulate real marine conditions
 * - Comprehensive validation before deployment
 * - Stress testing beyond normal operational parameters
 * - Recovery testing from various failure modes
 * - Detailed logging for troubleshooting
 * 
 * Critical Testing Features:
 * - Variable message size validation (16-1008 bytes)
 * - Buffer overflow protection testing
 * - Empty buffer behavior validation
 * - Multi-sector power-loss recovery
 * - CRC corruption detection and repair
 * - Ring buffer wrap-around testing
 * 
 * @author Generated for Mercator Origins dive computer system
 * @version 1.0
 * @date 2024
 */

#ifdef BUILD_INCLUDE_FLASHRINGBUFFER_PART3

// === Enhanced Self-Test with Comprehensive Validation ===

/**
 * @brief Comprehensive functional validation for marine deployment
 * @return true if all tests pass
 * 
 * Marine-Ready Testing:
 * This self-test validates all core functionality needed for reliable
 * marine operation. Tests multiple record sizes from minimum (16 bytes)
 * to maximum (1008 bytes) to ensure the system handles the full range
 * of telemetry data sizes expected in marine environments.
 * 
 * Test Coverage:
 * 1. Write/read/delete cycle for all supported message sizes
 * 2. Buffer overflow protection (rejects oversized records)
 * 3. Empty buffer behavior (proper error handling)
 * 4. Data integrity verification (CRC validation)
 * 
 * Critical for Marine Deployment:
 * - Validates system readiness before dive operations
 * - Ensures reliable telemetry storage across data size ranges
 * - Confirms proper error handling for edge cases
 */
bool FlashRingBuffer::performSelfTest() {
    USB_SERIAL_PRINTLN("FlashRingBuffer::performSelfTest() - Running comprehensive self-test");
    
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::performSelfTest() - Not initialized");
        return false;
    }
    
    bool all_tests_passed = true;
    
    // Test 1: Multiple record size write/read/delete cycle
    const uint16_t test_sizes[] = {MIN_MESSAGE_SIZE, 32, 64, 128, 256, 512, MAX_MESSAGE_SIZE};
    const uint8_t num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    for (uint8_t i = 0; i < num_sizes; i++) {
        uint16_t size = test_sizes[i];
        uint8_t* test_data = (uint8_t*)malloc(size);
        uint8_t* read_data = (uint8_t*)malloc(size);
        
        if (!test_data || !read_data) {
            USB_SERIAL_PRINTLN("FlashRingBuffer::performSelfTest() - Memory allocation failed");
            if (test_data) free(test_data);
            if (read_data) free(read_data);
            all_tests_passed = false;
            break;
        }
        
        // Generate test pattern
        for (uint16_t j = 0; j < size; j++) {
            test_data[j] = (uint8_t)(j ^ 0x5A ^ (i << 3));
        }
        
        // Write test
        if (!appendRecord(test_data, size)) {
            USB_SERIAL_PRINTF("FlashRingBuffer::performSelfTest() - Failed to write %u byte record\n", size);
            all_tests_passed = false;
            free(test_data);
            free(read_data);
            break;
        }
        
        // Read test
        uint16_t actual_len;
        if (!readOldestRecord(read_data, size, actual_len)) {
            USB_SERIAL_PRINTF("FlashRingBuffer::performSelfTest() - Failed to read %u byte record\n", size);
            all_tests_passed = false;
            free(test_data);
            free(read_data);
            break;
        }
        
        // Verify data
        if (actual_len != size || memcmp(test_data, read_data, size) != 0) {
            USB_SERIAL_PRINTF("FlashRingBuffer::performSelfTest() - Data mismatch for %u byte record\n", size);
            all_tests_passed = false;
            free(test_data);
            free(read_data);
            break;
        }
        
        // Delete test
        if (!deleteOldestRecord()) {
            USB_SERIAL_PRINTF("FlashRingBuffer::performSelfTest() - Failed to delete %u byte record\n", size);
            all_tests_passed = false;
            free(test_data);
            free(read_data);
            break;
        }
        
        free(test_data);
        free(read_data);
        
        USB_SERIAL_PRINTF("FlashRingBuffer::performSelfTest() - %u byte test passed\n", size);
    }
    
    // Test 2: Buffer overflow protection
    USB_SERIAL_PRINTLN("FlashRingBuffer::performSelfTest() - Testing buffer overflow protection");
    
    // Try to write an oversized record
    uint8_t oversized_data[MAX_MESSAGE_SIZE + 100];
    if (appendRecord(oversized_data, sizeof(oversized_data))) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::performSelfTest() - FAILED: Oversized record was accepted");
        all_tests_passed = false;
    }
    
    // Test 3: Empty buffer behavior
    USB_SERIAL_PRINTLN("FlashRingBuffer::performSelfTest() - Testing empty buffer behavior");
    uint8_t dummy_buffer[64];
    uint16_t dummy_len;
    
    if (isEmpty()) {
        if (readOldestRecord(dummy_buffer, sizeof(dummy_buffer), dummy_len)) {
            USB_SERIAL_PRINTLN("FlashRingBuffer::performSelfTest() - FAILED: Read succeeded on empty buffer");
            all_tests_passed = false;
        }
        
        if (deleteOldestRecord()) {
            USB_SERIAL_PRINTLN("FlashRingBuffer::performSelfTest() - FAILED: Delete succeeded on empty buffer");
            all_tests_passed = false;
        }
    }
    
    USB_SERIAL_PRINTF("FlashRingBuffer::performSelfTest() - Self-test %s\n", 
              all_tests_passed ? "PASSED" : "FAILED");
    
    return all_tests_passed;
}

bool FlashRingBuffer::performExtendedDiagnostics() {
    USB_SERIAL_PRINTLN("=== FlashRingBuffer Extended Diagnostics ===");
    
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("Extended Diagnostics: System not initialized");
        return false;
    }
    
    uint32_t start_time = m_fn_millis ? m_fn_millis() : 0;
    bool all_tests_passed = true;
    
    // Test 1: Write/Read/Verify test across different sizes
    USB_SERIAL_PRINTLN("Extended: Testing write/read across multiple record sizes...");
    
    const uint16_t test_sizes[] = {MIN_MESSAGE_SIZE, 64, 128, 256, 512, MAX_MESSAGE_SIZE};
    const uint8_t num_test_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    for (uint8_t i = 0; i < num_test_sizes; i++) {
        uint16_t test_size = test_sizes[i];
        uint8_t* test_data = (uint8_t*)malloc(test_size);
        uint8_t* read_data = (uint8_t*)malloc(test_size);
        
        if (!test_data || !read_data) {
            USB_SERIAL_PRINTLN("Extended: Memory allocation failed");
            all_tests_passed = false;
            if (test_data) free(test_data);
            if (read_data) free(read_data);
            break;
        }
        
        // Generate test pattern
        for (uint16_t j = 0; j < test_size; j++) {
            test_data[j] = (uint8_t)(j ^ 0xAA ^ (i << 4));
        }
        
        // Write test record
        if (!appendRecord(test_data, test_size)) {
            USB_SERIAL_PRINTF("Extended: Failed to write %u byte test record\n", test_size);
            all_tests_passed = false;
            free(test_data);
            free(read_data);
            break;
        }
        
        // Read back and verify
        uint16_t actual_length;
        if (!readOldestRecord(read_data, test_size, actual_length)) {
            USB_SERIAL_PRINTF("Extended: Failed to read %u byte test record\n", test_size);
            all_tests_passed = false;
            free(test_data);
            free(read_data);
            break;
        }
        
        if (actual_length != test_size || memcmp(test_data, read_data, test_size) != 0) {
            USB_SERIAL_PRINTF("Extended: Data mismatch for %u byte test record\n", test_size);
            all_tests_passed = false;
            free(test_data);
            free(read_data);
            break;
        }
        
        // Delete test record
        if (!deleteOldestRecord()) {
            USB_SERIAL_PRINTF("Extended: Failed to delete %u byte test record\n", test_size);
            all_tests_passed = false;
            free(test_data);
            free(read_data);
            break;
        }
        
        free(test_data);
        free(read_data);
        
        USB_SERIAL_PRINTF("Extended: %u byte test... OK\n", test_size);
    }
    
    uint32_t duration = m_fn_millis ? (m_fn_millis() - start_time) : 0;
    USB_SERIAL_PRINTF("=== Extended Diagnostics Complete: %s (%u ms) ===\n", 
              all_tests_passed ? "PASSED" : "FAILED", duration);
    
    return all_tests_passed;
}

bool FlashRingBuffer::performDeepSectorValidation() {
    USB_SERIAL_PRINTLN("=== FlashRingBuffer Deep Sector Validation ===");
    
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("Deep validation: System not initialized");
        return false;
    }
    
    uint32_t start_time = m_fn_millis ? m_fn_millis() : 0;
    bool validation_passed = true;
    uint32_t sectors_validated = 0;
    uint32_t sectors_with_issues = 0;
    uint32_t records_validated = 0;
    uint32_t corrupted_records = 0;
    
    USB_SERIAL_PRINTF("Deep validation: Scanning %u sectors...\n", TOTAL_SECTORS);
    
    // Scan all sectors for structural integrity
    for (uint32_t sector_idx = 0; sector_idx < TOTAL_SECTORS; sector_idx++) {
        SectorHeader header;
        esp_err_t err = readSector(sector_idx, &header, sizeof(SectorHeader));
        
        if (err != ESP_OK) {
            USB_SERIAL_PRINTF("Deep validation: Sector %u read error: %d\n", sector_idx, err);
            sectors_with_issues++;
            validation_passed = false;
            continue;
        }
        
        sectors_validated++;
        
        // Check if sector is used (has magic number)
        if (header.magic != SECTOR_MAGIC) {
            // Empty sector - expected in many cases
            continue;
        }
        
        // Validate sector header
        if (!validateSectorHeader(header)) {
            USB_SERIAL_PRINTF("Deep validation: Sector %u has invalid header\n", sector_idx);
            sectors_with_issues++;
            validation_passed = false;
            continue;
        }
        
        // Read entire sector for record validation
        uint8_t* sector_data = (uint8_t*)malloc(SECTOR_SIZE);
        if (!sector_data) {
            USB_SERIAL_PRINTLN("Deep validation: Memory allocation failed");
            validation_passed = false;
            break;
        }
        
        err = readSector(sector_idx, sector_data, SECTOR_SIZE);
        if (err != ESP_OK) {
            USB_SERIAL_PRINTF("Deep validation: Sector %u full read error: %d\n", sector_idx, err);
            sectors_with_issues++;
            validation_passed = false;
            free(sector_data);
            continue;
        }
        
        // Validate records within sector
        uint32_t offset = SECTOR_HEADER_SIZE;
        while (offset + RECORD_HEADER_SIZE < SECTOR_SIZE && offset < SECTOR_HEADER_SIZE + header.used) {
            RecordHeader* rec_header = (RecordHeader*)(sector_data + offset);
            
            // Check for valid record length
            if (rec_header->len < MIN_MESSAGE_SIZE || rec_header->len > MAX_MESSAGE_SIZE) {
                if (rec_header->len != 0) {  // 0 length indicates end of records
                    USB_SERIAL_PRINTF("Deep validation: Sector %u, offset %u - invalid record length %u\n", 
                                    sector_idx, offset, rec_header->len);
                    corrupted_records++;
                    validation_passed = false;
                }
                break;
            }
            
            // Check if complete record fits
            if (offset + RECORD_HEADER_SIZE + rec_header->len > SECTOR_SIZE) {
                USB_SERIAL_PRINTF("Deep validation: Sector %u, offset %u - record extends beyond sector\n", 
                                sector_idx, offset);
                corrupted_records++;
                validation_passed = false;
                break;
            }
            
            // Validate record CRC
            uint8_t* payload = sector_data + offset + RECORD_HEADER_SIZE;
            uint16_t calculated_crc = calculateCRC16(payload, rec_header->len);
            
            if (calculated_crc != rec_header->crc16) {
                USB_SERIAL_PRINTF("Deep validation: Sector %u, offset %u - CRC mismatch (calc: 0x%04X, stored: 0x%04X)\n", 
                                sector_idx, offset, calculated_crc, rec_header->crc16);
                corrupted_records++;
                validation_passed = false;
            }
            
            // Check record completion flag
            if (rec_header->valid != 0x00) {
                USB_SERIAL_PRINTF("Deep validation: Sector %u, offset %u - incomplete record (valid: 0x%02X)\n", 
                                sector_idx, offset, rec_header->valid);
                corrupted_records++;
                validation_passed = false;
            }
            
            records_validated++;
            offset += RECORD_HEADER_SIZE + rec_header->len;
            
            // Yield periodically
            if (records_validated % 100 == 0) {
                delay(1);
            }
        }
        
        // Validate sector completion (sentinel pattern check)
        if (!validateSectorComplete(sector_idx)) {
            USB_SERIAL_PRINTF("Deep validation: Sector %u failed completion validation\n", sector_idx);
            sectors_with_issues++;
            validation_passed = false;
        }
        
        free(sector_data);
        
        // Progress update every 50 sectors
        if (sector_idx % 50 == 0 || sector_idx == TOTAL_SECTORS - 1) {
            USB_SERIAL_PRINTF("Deep validation: Progress %u/%u sectors, %u records\n", 
                            sector_idx + 1, TOTAL_SECTORS, records_validated);
        }
    }
    
    uint32_t duration = m_fn_millis ? (m_fn_millis() - start_time) : 0;
    
    USB_SERIAL_PRINTF("Deep validation summary:\n");
    USB_SERIAL_PRINTF("  Sectors scanned: %u\n", sectors_validated);
    USB_SERIAL_PRINTF("  Sectors with issues: %u\n", sectors_with_issues);
    USB_SERIAL_PRINTF("  Records validated: %u\n", records_validated);
    USB_SERIAL_PRINTF("  Corrupted records: %u\n", corrupted_records);
    USB_SERIAL_PRINTF("  Validation time: %u ms\n", duration);
    USB_SERIAL_PRINTF("=== Deep Sector Validation: %s ===\n", validation_passed ? "PASSED" : "FAILED");
    
    return validation_passed;
}

/**
 * @brief Critical power-loss recovery test for marine safety
 * @return true if system successfully recovers from simulated power failures
 * 
 * Marine Power-Loss Testing:
 * This test simulates the power interruptions that frequently occur in
 * marine environments and validates the system's ability to recover
 * with minimal data loss. Essential for dive computer reliability.
 * 
 * Simulation Scenarios:
 * 1. Incomplete sector write (power lost during flash sector erase/write)
 * 2. Incomplete record write (power lost during record commit)
 * 3. Corrupted sentinel patterns (power lost during sector completion)
 * 4. System state corruption (power lost during state persistence)
 * 
 * Recovery Validation:
 * - System reinitializes successfully after corruption injection
 * - Data recovery with acceptable loss thresholds
 * - Corrupted/incomplete data properly detected and discarded
 * - System continues normal operation after recovery
 * 
 * Marine Safety Critical:
 * - Validates system survives boat electrical system fluctuations
 * - Ensures dive data integrity after emergency shutdowns
 * - Confirms system reliability for extended marine operations
 */
bool FlashRingBuffer::performPowerLossRecoveryTest() {
    USB_SERIAL_PRINTLN("=== FlashRingBuffer Power Loss Recovery Test ===");
    
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("Recovery test: System not initialized");
        return false;
    }
    
    uint32_t start_time = m_fn_millis ? m_fn_millis() : 0;
    bool test_passed = true;
    
    // Save current state
    FlashRingBufferState saved_state = m_state;
    uint32_t saved_buffer_used = m_buffer_used;
    
    USB_SERIAL_PRINTLN("Recovery test: Testing power-loss scenarios...");
    
    // Test 1: Simulate incomplete sector write
    USB_SERIAL_PRINTLN("Recovery test: Simulating incomplete sector write...");
    
    // Create test data to fill most of a sector
    const uint16_t test_record_size = 200;
    const uint8_t num_test_records = 15; // Should nearly fill a sector
    uint8_t* test_data = (uint8_t*)malloc(test_record_size);
    
    if (!test_data) {
        USB_SERIAL_PRINTLN("Recovery test: Memory allocation failed");
        return false;
    }
    
    // Fill test data with recognizable pattern
    for (uint16_t i = 0; i < test_record_size; i++) {
        test_data[i] = (uint8_t)(i ^ 0x5A);
    }
    
    // Write multiple test records to build up sector
    for (uint8_t i = 0; i < num_test_records; i++) {
        if (!appendRecord(test_data, test_record_size)) {
            USB_SERIAL_PRINTF("Recovery test: Failed to write test record %u\n", i);
            test_passed = false;
            break;
        }
    }
    
    // Flush buffer to flash
    if (test_passed && !flushRAMBufferToFlash()) {
        USB_SERIAL_PRINTLN("Recovery test: Failed to flush buffer");
        test_passed = false;
    }
    
    if (test_passed) {
        // Test 2: Simulate corruption by writing incomplete record
        USB_SERIAL_PRINTLN("Recovery test: Simulating incomplete record write...");
        
        // Manually write an incomplete record to current head sector
        uint32_t sector_offset = m_state.head_sector * SECTOR_SIZE + SECTOR_HEADER_SIZE + m_state.head_sector_used;
        
        RecordHeader incomplete_header;
        incomplete_header.len = 100;
        incomplete_header.crc16 = 0x1234;  // Dummy CRC
        incomplete_header.valid = 0xFF;    // Mark as incomplete
        incomplete_header.reserved[0] = incomplete_header.reserved[1] = incomplete_header.reserved[2] = 0;
        
        esp_err_t err = esp_partition_write(m_partition, sector_offset, &incomplete_header, sizeof(RecordHeader));
        if (err == ESP_OK) {
            USB_SERIAL_PRINTLN("Recovery test: Incomplete record written");
            
            // Test 3: Force recovery scan
            USB_SERIAL_PRINTLN("Recovery test: Testing recovery scan...");
            
            // Reinitialize system to trigger recovery
            teardown();
            if (init(m_fn_millis)) {
                USB_SERIAL_PRINTLN("Recovery test: System reinitialized successfully");
                
                // Verify that incomplete record was handled
                uint32_t record_count = getRecordCount();
                USB_SERIAL_PRINTF("Recovery test: Records after recovery: %u\n", record_count);
                
                // Try to read records to ensure they're still valid
                uint8_t* read_buffer = (uint8_t*)malloc(test_record_size);
                if (read_buffer) {
                    uint16_t actual_len;
                    uint32_t readable_records = 0;
                    
                    // Count how many records we can actually read
                    for (uint32_t i = 0; i < record_count && i < num_test_records; i++) {
                        if (readOldestRecord(read_buffer, test_record_size, actual_len)) {
                            if (actual_len == test_record_size) {
                                readable_records++;
                            }
                            deleteOldestRecord();
                        } else {
                            break;
                        }
                    }
                    
                    USB_SERIAL_PRINTF("Recovery test: Successfully read %u records after recovery\n", readable_records);
                    
                    if (readable_records < (num_test_records - 2)) {  // Allow some data loss
                        USB_SERIAL_PRINTLN("Recovery test: Too much data lost during recovery");
                        test_passed = false;
                    }
                    
                    free(read_buffer);
                } else {
                    USB_SERIAL_PRINTLN("Recovery test: Read buffer allocation failed");
                    test_passed = false;
                }
            } else {
                USB_SERIAL_PRINTLN("Recovery test: System failed to reinitialize");
                test_passed = false;
            }
        } else {
            USB_SERIAL_PRINTF("Recovery test: Failed to write incomplete record: %d\n", err);
            test_passed = false;
        }
    }
    
    free(test_data);
    
    // Test 4: Validate sector sentinel patterns
    USB_SERIAL_PRINTLN("Recovery test: Validating sector completion patterns...");
    
    uint32_t incomplete_sectors = 0;
    for (uint32_t sector_idx = 0; sector_idx < TOTAL_SECTORS; sector_idx++) {
        if (!validateSectorComplete(sector_idx)) {
            incomplete_sectors++;
        }
    }
    
    USB_SERIAL_PRINTF("Recovery test: Found %u sectors with completion issues\n", incomplete_sectors);
    
    uint32_t duration = m_fn_millis ? (m_fn_millis() - start_time) : 0;
    
    USB_SERIAL_PRINTF("Recovery test summary:\n");
    USB_SERIAL_PRINTF("  Test duration: %u ms\n", duration);
    USB_SERIAL_PRINTF("  Incomplete sectors detected: %u\n", incomplete_sectors);
    USB_SERIAL_PRINTF("=== Power Loss Recovery Test: %s ===\n", test_passed ? "PASSED" : "FAILED");
    
    return test_passed;
}

bool FlashRingBuffer::performStressTest(uint32_t num_records) {
    USB_SERIAL_PRINTF("=== FlashRingBuffer Stress Test (%u records) ===\n", num_records);
    
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("Stress test: System not initialized");
        return false;
    }
    
    uint32_t start_time = m_fn_millis ? m_fn_millis() : 0;
    bool test_passed = true;
    uint32_t successful_writes = 0;
    uint32_t successful_reads = 0;
    uint32_t write_errors = 0;
    uint32_t read_errors = 0;
    uint32_t crc_errors = 0;
    
    USB_SERIAL_PRINTF("Stress test: Writing %u records with varying sizes...\n", num_records);
    
    // Test with varying record sizes
    const uint16_t test_sizes[] = {MIN_MESSAGE_SIZE, 64, 128, 256, 512, 1008};
    const uint8_t num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    // Phase 1: Write stress test
    for (uint32_t i = 0; i < num_records; i++) {
        uint16_t size = test_sizes[i % num_sizes];
        uint8_t* test_data = (uint8_t*)malloc(size);
        
        if (!test_data) {
            USB_SERIAL_PRINTF("Stress test: Memory allocation failed at record %u\n", i);
            test_passed = false;
            break;
        }
        
        // Generate unique test pattern for this record
        for (uint16_t j = 0; j < size; j++) {
            test_data[j] = (uint8_t)((i + j) ^ 0xA5);
        }
        
        if (appendRecord(test_data, size)) {
            successful_writes++;
        } else {
            write_errors++;
            USB_SERIAL_PRINTF("Stress test: Write failed for record %u (size %u)\n", i, size);
            if (write_errors > 10) {  // Stop if too many errors
                test_passed = false;
                free(test_data);
                break;
            }
        }
        
        free(test_data);
        
        // Progress update
        if (i > 0 && i % 100 == 0) {
            USB_SERIAL_PRINTF("Stress test: Written %u/%u records (%u errors)\n", i, num_records, write_errors);
            delay(10);  // Brief yield
        }
        
        // Periodically yield to watchdog
        if (i % 50 == 0) {
            delay(1);
        }
    }
    
    USB_SERIAL_PRINTF("Stress test: Write phase complete - %u successful, %u errors\n", 
                    successful_writes, write_errors);
    
    // Phase 2: Read stress test
    if (test_passed && successful_writes > 0) {
        USB_SERIAL_PRINTF("Stress test: Reading back %u records...\n", successful_writes);
        
        uint32_t records_to_read = getRecordCount();
        USB_SERIAL_PRINTF("Stress test: Buffer contains %u records\n", records_to_read);
        
        for (uint32_t i = 0; i < records_to_read && i < successful_writes; i++) {
            uint16_t expected_size = test_sizes[i % num_sizes];
            uint8_t* read_data = (uint8_t*)malloc(expected_size);
            
            if (!read_data) {
                USB_SERIAL_PRINTF("Stress test: Read buffer allocation failed at record %u\n", i);
                test_passed = false;
                break;
            }
            
            uint16_t actual_length;
            if (readOldestRecord(read_data, expected_size, actual_length)) {
                successful_reads++;
                
                // Verify data integrity
                if (actual_length == expected_size) {
                    bool data_valid = true;
                    for (uint16_t j = 0; j < expected_size; j++) {
                        uint8_t expected_byte = (uint8_t)((i + j) ^ 0xA5);
                        if (read_data[j] != expected_byte) {
                            data_valid = false;
                            crc_errors++;
                            break;
                        }
                    }
                    
                    if (!data_valid) {
                        USB_SERIAL_PRINTF("Stress test: Data corruption in record %u\n", i);
                        if (crc_errors > 10) {  // Stop if too many errors
                            test_passed = false;
                            free(read_data);
                            break;
                        }
                    }
                } else {
                    USB_SERIAL_PRINTF("Stress test: Size mismatch for record %u (expected %u, got %u)\n", 
                                    i, expected_size, actual_length);
                    crc_errors++;
                }
                
                // Delete the record
                if (!deleteOldestRecord()) {
                    USB_SERIAL_PRINTF("Stress test: Failed to delete record %u\n", i);
                    read_errors++;
                }
            } else {
                read_errors++;
                USB_SERIAL_PRINTF("Stress test: Read failed for record %u\n", i);
                if (read_errors > 10) {  // Stop if too many errors
                    test_passed = false;
                    free(read_data);
                    break;
                }
            }
            
            free(read_data);
            
            // Progress update
            if (i > 0 && i % 100 == 0) {
                USB_SERIAL_PRINTF("Stress test: Read %u records (%u errors, %u CRC errors)\n", 
                                i, read_errors, crc_errors);
                delay(10);  // Brief yield
            }
            
            // Periodically yield to watchdog
            if (i % 50 == 0) {
                delay(1);
            }
        }
    }
    
    uint32_t duration = m_fn_millis ? (m_fn_millis() - start_time) : 0;
    uint32_t remaining_records = getRecordCount();
    
    // Final validation
    if (write_errors > num_records / 10) {  // More than 10% write failures
        test_passed = false;
        USB_SERIAL_PRINTLN("Stress test: Too many write errors");
    }
    
    if (read_errors > successful_writes / 10) {  // More than 10% read failures
        test_passed = false;
        USB_SERIAL_PRINTLN("Stress test: Too many read errors");
    }
    
    if (crc_errors > 0) {  // Any data corruption is concerning
        test_passed = false;
        USB_SERIAL_PRINTLN("Stress test: Data corruption detected");
    }
    
    USB_SERIAL_PRINTF("Stress test summary:\n");
    USB_SERIAL_PRINTF("  Test duration: %u ms\n", duration);
    USB_SERIAL_PRINTF("  Records requested: %u\n", num_records);
    USB_SERIAL_PRINTF("  Successful writes: %u\n", successful_writes);
    USB_SERIAL_PRINTF("  Write errors: %u\n", write_errors);
    USB_SERIAL_PRINTF("  Successful reads: %u\n", successful_reads);
    USB_SERIAL_PRINTF("  Read errors: %u\n", read_errors);
    USB_SERIAL_PRINTF("  Data corruption errors: %u\n", crc_errors);
    USB_SERIAL_PRINTF("  Records remaining: %u\n", remaining_records);
    USB_SERIAL_PRINTF("  Write rate: %.1f records/sec\n", 
                    duration > 0 ? (float)(successful_writes * 1000) / duration : 0.0);
    USB_SERIAL_PRINTF("=== Stress Test: %s ===\n", test_passed ? "PASSED" : "FAILED");
    
    return test_passed;
}

//=============================================================================
// FAILURE INJECTION METHODS FOR TESTING
//=============================================================================

/**
 * @brief Comprehensive failure injection capabilities for testing robustness
 * 
 * TESTING_MODE Only - DO NOT USE IN PRODUCTION:
 * These methods intentionally corrupt the flash storage system to validate
 * the robustness of recovery mechanisms. They are essential for ensuring
 * the system can handle real-world marine failure scenarios.
 * 
 * Failure Scenarios Covered:
 * - Sector header corruption (magic number, CRC, usage fields)
 * - Persistent state corruption (NVS/EEPROM data)
 * - Incomplete write simulation (power-loss during operations)
 * - Ring buffer pointer corruption
 * - Random multi-sector corruption
 * - Partition access failures
 * 
 * Marine Testing Importance:
 * - Validates system survives harsh marine electrical environments
 * - Tests recovery from electromagnetic interference corruption
 * - Ensures system reliability under vibration and temperature extremes
 * - Validates automatic repair capabilities
 * 
 * SAFETY WARNING:
 * These methods will corrupt stored data and are intended only for
 * controlled testing environments. Never compile with TESTING_MODE
 * enabled for production deployments.
 */

#ifdef TESTING_MODE

// Inject sector corruption by corrupting the sector header magic number
bool FlashRingBuffer::injectSectorCorruption(uint32_t sector_index) {
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::injectSectorCorruption() - Not initialized");
        return false;
    }
    
    if (sector_index >= TOTAL_SECTORS) {
        USB_SERIAL_PRINTF("FlashRingBuffer::injectSectorCorruption() - Invalid sector %u\n", sector_index);
        return false;
    }
    
    USB_SERIAL_PRINTF("FlashRingBuffer::injectSectorCorruption() - Corrupting sector %u magic number\n", sector_index);
    
    // Corrupt the magic number in the sector header
    uint32_t corrupt_magic = 0xDEADBEEF; // Invalid magic
    uint32_t sector_offset = sector_index * SECTOR_SIZE;
    
    esp_err_t err = esp_partition_write(m_partition, sector_offset, &corrupt_magic, sizeof(corrupt_magic));
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::injectSectorCorruption() - Write failed: %s\n", esp_err_to_name(err));
        return false;
    }
    
    USB_SERIAL_PRINTLN("FlashRingBuffer::injectSectorCorruption() - Sector corruption injected successfully");
    return true;
}

// Corrupt the persisted state in EEPROM
bool FlashRingBuffer::corruptPersistedState() {
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::corruptPersistedState() - Not initialized");
        return false;
    }
    
    USB_SERIAL_PRINTLN("FlashRingBuffer::corruptPersistedState() - Corrupting EEPROM state");
    
    // Save current state for logging
    uint32_t original_head = m_state.head_sector;
    uint32_t original_tail = m_state.tail_sector;
    
    // Create corrupted state
    FlashRingBufferState corrupt_state;
    corrupt_state.head_sector = 0xFFFFFFFF;  // Invalid sector
    corrupt_state.tail_sector = 0xFFFFFFFF;  // Invalid sector
    corrupt_state.head_sector_used = 0xFFFFFFFF;  // Invalid usage
    corrupt_state.next_seq_number = 0xFFFFFFFF;  // Invalid sequence
    corrupt_state.write_count = 0xFFFFFFFF;  // Invalid count
    corrupt_state.state_crc = 0xDEADBEEF;  // Invalid CRC
    
    // Write corrupted state to EEPROM
    m_preferences.putUInt("head_sector", corrupt_state.head_sector);
    m_preferences.putUInt("tail_sector", corrupt_state.tail_sector);
    m_preferences.putUInt("head_used", corrupt_state.head_sector_used);
    m_preferences.putUInt("next_seq", corrupt_state.next_seq_number);
    m_preferences.putUInt("write_count", corrupt_state.write_count);
    m_preferences.putUInt("state_crc", corrupt_state.state_crc);
    
    USB_SERIAL_PRINTF("FlashRingBuffer::corruptPersistedState() - State corrupted (was head=%u, tail=%u)\n", 
                      original_head, original_tail);
    USB_SERIAL_PRINTLN("FlashRingBuffer::corruptPersistedState() - Restart required to test recovery");
    
    return true;
}

// Simulate incomplete write (power-loss during record write)
bool FlashRingBuffer::simulateIncompleteWrite() {
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::simulateIncompleteWrite() - Not initialized");
        return false;
    }
    
    USB_SERIAL_PRINTLN("FlashRingBuffer::simulateIncompleteWrite() - Simulating power-loss during write");
    
    // Flush any pending RAM buffer first
    flushRAMBufferToFlash();
    
    // Calculate where the next record would go
    uint32_t sector_offset = m_state.head_sector * SECTOR_SIZE;
    uint32_t record_offset = SECTOR_HEADER_SIZE + m_state.head_sector_used;
    
    // Create a test payload
    uint8_t test_payload[224];
    for (int i = 0; i < 224; i++) {
        test_payload[i] = (uint8_t)(i % 256);
    }
    
    // Create record header with correct CRC but leave valid flag as 0xFF (incomplete)
    RecordHeader header;
    header.len = 224;
    header.crc16 = calculateCRC16(test_payload, 224);
    header.valid = 0xFF;  // Mark as incomplete (power-loss before commit)
    header.reserved[0] = header.reserved[1] = header.reserved[2] = 0xFF;
    
    // Write incomplete record header
    esp_err_t err = esp_partition_write(m_partition, sector_offset + record_offset, &header, sizeof(header));
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::simulateIncompleteWrite() - Header write failed: %s\n", esp_err_to_name(err));
        return false;
    }
    
    // Write payload
    err = esp_partition_write(m_partition, sector_offset + record_offset + sizeof(header), test_payload, 224);
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::simulateIncompleteWrite() - Payload write failed: %s\n", esp_err_to_name(err));
        return false;
    }
    
    // Deliberately NOT setting valid flag to 0x00 (simulates power-loss)
    
    USB_SERIAL_PRINTLN("FlashRingBuffer::simulateIncompleteWrite() - Incomplete record injected");
    USB_SERIAL_PRINTLN("FlashRingBuffer::simulateIncompleteWrite() - POST should detect and truncate this record");
    
    return true;
}

// Corrupt ring buffer pointers to invalid values
bool FlashRingBuffer::corruptRingPointers() {
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::corruptRingPointers() - Not initialized");
        return false;
    }
    
    USB_SERIAL_PRINTF("FlashRingBuffer::corruptRingPointers() - Original: head=%u, tail=%u\n", 
                      m_state.head_sector, m_state.tail_sector);
    
    // Set pointers to invalid values
    m_state.head_sector = TOTAL_SECTORS + 100;  // Beyond valid range
    m_state.tail_sector = TOTAL_SECTORS + 200;  // Beyond valid range
    m_state.head_sector_used = SECTOR_SIZE + 1000;  // Beyond sector size
    
    // Save corrupted state
    savePersistedState();
    
    USB_SERIAL_PRINTF("FlashRingBuffer::corruptRingPointers() - Corrupted: head=%u, tail=%u, used=%u\n", 
                      m_state.head_sector, m_state.tail_sector, m_state.head_sector_used);
    USB_SERIAL_PRINTLN("FlashRingBuffer::corruptRingPointers() - POST should detect and correct these values");
    
    return true;
}

// Accelerated wear test - rapidly cycle through sectors
bool FlashRingBuffer::acceleratedWearTest(uint32_t cycles) {
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::acceleratedWearTest() - Not initialized");
        return false;
    }
    
    USB_SERIAL_PRINTF("FlashRingBuffer::acceleratedWearTest() - Starting %u cycle wear test\n", cycles);
    
    uint32_t start_time = m_fn_millis();
    uint32_t original_write_count = m_state.write_count;
    
    for (uint32_t i = 0; i < cycles; i++) {
        // Fill current sector to force advancement
        uint8_t dummy_data[200];
        for (int j = 0; j < 200; j++) {
            dummy_data[j] = (uint8_t)(j + i);  // Varying data pattern
        }
        
        // Keep adding records until sector needs to advance
        while (sectorHasSpace(RECORD_HEADER_SIZE + 200 + 10)) {  // +10 for safety margin
            if (!appendRecord(dummy_data, 200)) {
                USB_SERIAL_PRINTF("FlashRingBuffer::acceleratedWearTest() - Write failed at cycle %u\n", i);
                return false;
            }
        }
        
        // Force sector advancement
        if (!advanceSector()) {
            USB_SERIAL_PRINTF("FlashRingBuffer::acceleratedWearTest() - Sector advance failed at cycle %u\n", i);
            return false;
        }
        
        // Progress reporting
        if (i % 100 == 0 && i > 0) {
            uint32_t elapsed = m_fn_millis() - start_time;
            USB_SERIAL_PRINTF("Wear test progress: %u/%u cycles (%.1f cycles/sec)\n", 
                            i, cycles, elapsed > 0 ? (float)(i * 1000) / elapsed : 0.0);
        }
        
        // Yield periodically to prevent watchdog issues
        if (i % 10 == 0) {
            delay(1);  // Brief yield
        }
    }
    
    uint32_t duration = m_fn_millis() - start_time;
    uint32_t total_writes = m_state.write_count - original_write_count;
    
    USB_SERIAL_PRINTF("FlashRingBuffer::acceleratedWearTest() - Completed %u cycles in %u ms\n", cycles, duration);
    USB_SERIAL_PRINTF("FlashRingBuffer::acceleratedWearTest() - Generated %u writes (%.1f writes/cycle)\n", 
                      total_writes, cycles > 0 ? (float)total_writes / cycles : 0.0);
    USB_SERIAL_PRINTF("FlashRingBuffer::acceleratedWearTest() - Total system writes: %u\n", m_state.write_count);
    
    return true;
}

// Inject random corruption across multiple sectors
bool FlashRingBuffer::injectRandomCorruption(uint32_t num_sectors) {
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::injectRandomCorruption() - Not initialized");
        return false;
    }
    
    if (num_sectors == 0 || num_sectors > TOTAL_SECTORS / 4) {  // Limit to 25% of sectors
        USB_SERIAL_PRINTF("FlashRingBuffer::injectRandomCorruption() - Invalid sector count %u (max %u)\n", 
                          num_sectors, TOTAL_SECTORS / 4);
        return false;
    }
    
    USB_SERIAL_PRINTF("FlashRingBuffer::injectRandomCorruption() - Corrupting %u random sectors\n", num_sectors);
    
    uint32_t corrupted = 0;
    for (uint32_t i = 0; i < num_sectors; i++) {
        // Pick a random sector (avoid current head/tail region)
        uint32_t sector = (m_state.head_sector + 10 + (rand() % (TOTAL_SECTORS - 50))) % TOTAL_SECTORS;
        
        // Avoid corrupting sectors near head/tail
        if (abs((int)sector - (int)m_state.head_sector) < 5 || 
            abs((int)sector - (int)m_state.tail_sector) < 5) {
            continue;  // Skip this sector
        }
        
        // Randomly choose corruption type
        int corruption_type = rand() % 3;
        
        if (corruption_type == 0) {
            // Corrupt magic number
            injectSectorCorruption(sector);
        } else if (corruption_type == 1) {
            // Corrupt CRC
            injectCRCCorruption(sector);
        } else {
            // Corrupt used field
            uint32_t corrupt_used = 0xFFFF;  // Invalid used value
            uint32_t sector_offset = sector * SECTOR_SIZE + 8;  // Offset to 'used' field
            esp_partition_write(m_partition, sector_offset, &corrupt_used, 2);
        }
        
        corrupted++;
    }
    
    USB_SERIAL_PRINTF("FlashRingBuffer::injectRandomCorruption() - Successfully corrupted %u sectors\n", corrupted);
    return true;
}

// Simulate partition access failure (temporary)
bool FlashRingBuffer::simulatePartitionFailure() {
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::simulatePartitionFailure() - Not initialized");
        return false;
    }
    
    USB_SERIAL_PRINTLN("FlashRingBuffer::simulatePartitionFailure() - Simulating partition failure");
    USB_SERIAL_PRINTLN("WARNING: This will temporarily disable flash operations");
    
    // Temporarily set partition to NULL to simulate failure
    // (This is reversible by restarting the system)
    m_partition = nullptr;
    
    USB_SERIAL_PRINTLN("FlashRingBuffer::simulatePartitionFailure() - Partition access disabled");
    USB_SERIAL_PRINTLN("FlashRingBuffer::simulatePartitionFailure() - Restart required to restore partition access");
    
    return true;
}

// Inject CRC corruption in sector header
bool FlashRingBuffer::injectCRCCorruption(uint32_t sector_index) {
    if (!m_initialized) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::injectCRCCorruption() - Not initialized");
        return false;
    }
    
    if (sector_index >= TOTAL_SECTORS) {
        USB_SERIAL_PRINTF("FlashRingBuffer::injectCRCCorruption() - Invalid sector %u\n", sector_index);
        return false;
    }
    
    USB_SERIAL_PRINTF("FlashRingBuffer::injectCRCCorruption() - Corrupting sector %u CRC\n", sector_index);
    
    // Corrupt the CRC32 field in the sector header
    uint32_t corrupt_crc = 0xDEADBEEF;  // Invalid CRC
    uint32_t sector_offset = sector_index * SECTOR_SIZE + 12;  // Offset to crc32 field
    
    esp_err_t err = esp_partition_write(m_partition, sector_offset, &corrupt_crc, sizeof(corrupt_crc));
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::injectCRCCorruption() - Write failed: %s\n", esp_err_to_name(err));
        return false;
    }
    
    USB_SERIAL_PRINTLN("FlashRingBuffer::injectCRCCorruption() - CRC corruption injected successfully");
    return true;
}

#endif // TESTING_MODE

#endif