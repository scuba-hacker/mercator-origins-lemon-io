#ifdef BUILD_INCLUDE_FLASHRINGBUFFER_PART3

// Enhanced self-test with comprehensive validation
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

#endif