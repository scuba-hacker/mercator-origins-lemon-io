#ifdef BUILD_INCLUDE_FLASHRINGBUFFER_PART2

uint32_t FlashRingBuffer::getUsedSpace() const {
    if (!m_initialized) {
        return 0;
    }
    
    uint32_t used_sectors = getRingDistance();
    return (used_sectors * SECTOR_SIZE) + m_state.head_sector_used + m_buffer_used;
}

uint32_t FlashRingBuffer::getFreeSpace() const {
    return RING_BUFFER_SIZE - getUsedSpace();
}

bool FlashRingBuffer::isEmpty() const {
    return (m_state.head_sector == m_state.tail_sector && m_state.head_sector_used == 0 && m_buffer_used == 0);
}

bool FlashRingBuffer::isFull() const {
    uint32_t next_head = (m_state.head_sector + 1) % TOTAL_SECTORS;
    return (next_head == m_state.tail_sector && m_state.head_sector_used >= USABLE_SECTOR_SIZE);
}

uint32_t FlashRingBuffer::getRingDistance() const {
    if (m_state.head_sector >= m_state.tail_sector) {
        return m_state.head_sector - m_state.tail_sector;
    } else {
        return TOTAL_SECTORS - m_state.tail_sector + m_state.head_sector;
    }
}

bool FlashRingBuffer::validateSectorHeader(const SectorHeader& header) {
    // Basic magic and usage validation
    if (header.magic != SECTOR_MAGIC) {
        return false;
    }
    
    if (header.used > USABLE_SECTOR_SIZE) {
        return false;
    }
    
    // CRC validation
    uint32_t calculated_crc = calculateCRC32((const uint8_t*)&header, 
                                            sizeof(SectorHeader) - sizeof(header.crc32));
    return (calculated_crc == header.crc32);
}

bool FlashRingBuffer::validateSectorComplete(uint32_t sector_index) const {
    if (sector_index >= TOTAL_SECTORS) {
        return false;
    }
    
    // Read entire sector to validate completion
    uint8_t* sector_data = m_sector_buffer; // Use temporary buffer
    esp_err_t err = readSector(sector_index, sector_data, SECTOR_SIZE);
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::validateSectorComplete() - Failed to read sector %u: %s\n", 
                  sector_index, esp_err_to_name(err));
        return false;
    }
    
    SectorHeader* header = (SectorHeader*)sector_data;
    
    // Skip empty sectors (all 0xFF)
    if (header->magic != SECTOR_MAGIC) {
        return true; // Empty sector is "complete" in its empty state
    }
    
    // Validate header first
    if (!validateSectorHeader(*header)) {
        USB_SERIAL_PRINTF("FlashRingBuffer::validateSectorComplete() - Sector %u has invalid header\n", sector_index);
        return false;
    }
    
    // Calculate where unused space starts
    uint32_t used_data_end = SECTOR_HEADER_SIZE + header->used;
    
    // If sector is completely full, no sentinel space needed
    if (used_data_end >= SECTOR_SIZE) {
        return true; // Completely full sector is valid
    }
    
    // Check sentinel pattern in unused space
    bool sentinel_valid = true;
    uint32_t sentinel_errors = 0;
    
    for (uint32_t i = used_data_end; i < SECTOR_SIZE - 1; i += 2) {
        uint16_t* word = (uint16_t*)(sector_data + i);
        
        if (*word == 0xFFFF) {
            // Found erased flash - incomplete sector write!
            USB_SERIAL_PRINTF("FlashRingBuffer::validateSectorComplete() - Sector %u incomplete write detected at offset 0x%03x (found 0xFFFF)\n", 
                      sector_index, i);
            return false;
        }
        
        if (*word != 0x55AA) {
            sentinel_errors++;
            if (sentinel_errors <= 3) { // Log first few errors to avoid spam
                USB_SERIAL_PRINTF("FlashRingBuffer::validateSectorComplete() - Sector %u corrupted sentinel at offset 0x%03x (expected 0x55AA, got 0x%04x)\n", 
                          sector_index, i, *word);
            }
            sentinel_valid = false;
        }
    }
    
    // Handle odd remaining byte
    if ((SECTOR_SIZE - used_data_end) % 2 == 1) {
        uint8_t last_byte = sector_data[SECTOR_SIZE - 1];
        if (last_byte == 0xFF) {
            USB_SERIAL_PRINTF("FlashRingBuffer::validateSectorComplete() - Sector %u incomplete write at last byte (found 0xFF)\n", 
                      sector_index);
            return false;
        }
        if (last_byte != 0x55) {
            USB_SERIAL_PRINTF("FlashRingBuffer::validateSectorComplete() - Sector %u corrupted last sentinel byte (expected 0x55, got 0x%02x)\n", 
                      sector_index, last_byte);
            sentinel_valid = false;
        }
    }
    
    if (!sentinel_valid) {
        USB_SERIAL_PRINTF("FlashRingBuffer::validateSectorComplete() - Sector %u has %u sentinel errors\n", 
                  sector_index, sentinel_errors);
    }
    
    return sentinel_valid;
}

bool FlashRingBuffer::advanceSector() {
    USB_SERIAL_PRINTF("FlashRingBuffer::advanceSector() - Advancing from sector %u\n", m_state.head_sector);
    
    uint32_t next_head = (m_state.head_sector + 1) % TOTAL_SECTORS;
    
    // Check if we're about to overwrite the tail sector (buffer full condition)
    if (next_head == m_state.tail_sector) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::advanceSector() - Ring buffer full, advancing tail");
        // Advance tail to make room
        m_state.tail_sector = (m_state.tail_sector + 1) % TOTAL_SECTORS;
        
        USB_SERIAL_PRINTF("FlashRingBuffer::advanceSector() - Advanced tail to sector %u\n", m_state.tail_sector);
    }
    
    // Erase the new head sector
    esp_err_t err = eraseSector(next_head);
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::advanceSector() - Failed to erase sector %u: %s\n", 
                  next_head, esp_err_to_name(err));
        return false;
    }
    
    // Create and write new sector header
    SectorHeader new_header;
    new_header.magic = SECTOR_MAGIC;
    new_header.seq = m_state.next_seq_number++;
    new_header.used = 0;
    new_header.reserved = 0;
    new_header.rsvd2 = 0;
    new_header.crc32 = calculateCRC32((const uint8_t*)&new_header, 
                                     sizeof(new_header) - sizeof(new_header.crc32));
    
    err = writeSector(next_head, &new_header, sizeof(new_header));
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::advanceSector() - Failed to write sector header %u: %s\n", 
                  next_head, esp_err_to_name(err));
        return false;
    }
    
    // Update state
    m_state.head_sector = next_head;
    m_state.head_sector_used = 0;
    
    USB_SERIAL_PRINTF("FlashRingBuffer::advanceSector() - Advanced to sector %u, seq=%u\n", 
              m_state.head_sector, new_header.seq);
    
    savePersistedState();
    
    return true;
}

// Scan and recovery functions
bool FlashRingBuffer::scanAndRecover() {
    USB_SERIAL_PRINTLN("FlashRingBuffer::scanAndRecover() - Starting sector scan");
    
    uint32_t highest_seq = 0;
    uint32_t lowest_seq = UINT32_MAX;
    uint32_t head_candidate = 0;
    uint32_t tail_candidate = 0;
    bool found_valid_sectors = false;
    
    // Scan all sectors to find valid headers and determine head/tail positions
    for (uint32_t sector = 0; sector < TOTAL_SECTORS; sector++) {
        SectorHeader header;
        esp_err_t err = readSector(sector, &header, sizeof(header));
        
        if (err != ESP_OK) {
            continue;
        }
        
        if (!validateSectorHeader(header)) {
            continue;
        }
        
        // Enhanced power-loss detection: Check sector completion with sentinel pattern
        if (!validateSectorComplete(sector)) {
            USB_SERIAL_PRINTF("FlashRingBuffer::scanAndRecover() - Sector %u failed completion validation\n", sector);
            continue; // Skip incomplete/corrupted sectors
        }
        
        found_valid_sectors = true;
        
        // Track highest and lowest sequence numbers
        if (header.seq > highest_seq) {
            highest_seq = header.seq;
            head_candidate = sector;
        }
        
        if (header.seq < lowest_seq) {
            lowest_seq = header.seq;
            tail_candidate = sector;
        }
        
        USB_SERIAL_PRINTF("FlashRingBuffer::scanAndRecover() - Valid sector %u: seq=%u, used=%u\n", 
                  sector, header.seq, header.used);
    }
    
    if (!found_valid_sectors) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::scanAndRecover() - No valid sectors found, initializing fresh");
        
        // Initialize fresh ring buffer
        m_state.head_sector = 0;
        m_state.tail_sector = 0;
        m_state.head_sector_used = 0;
        m_state.next_seq_number = 1;
        m_state.write_count = 0;
        
        // Erase and initialize first sector
        if (eraseSector(0) != ESP_OK) {
            USB_SERIAL_PRINTLN("FlashRingBuffer::scanAndRecover() - Failed to erase first sector");
            return false;
        }
        
        // Create initial sector header
        SectorHeader initial_header;
        initial_header.magic = SECTOR_MAGIC;
        initial_header.seq = 0;
        initial_header.used = 0;
        initial_header.reserved = 0;
        initial_header.crc32 = calculateCRC32((const uint8_t*)&initial_header, 
                                             sizeof(initial_header) - sizeof(initial_header.crc32));
        initial_header.rsvd2 = 0;
        
        if (writeSector(0, &initial_header, sizeof(initial_header)) != ESP_OK) {
            USB_SERIAL_PRINTLN("FlashRingBuffer::scanAndRecover() - Failed to write initial header");
            return false;
        }
        
        USB_SERIAL_PRINTLN("FlashRingBuffer::scanAndRecover() - Fresh initialization complete");
        return true;
    }
    
    // Validate recovered head sector
    SectorHeader head_header;
    if (readSector(head_candidate, &head_header, sizeof(head_header)) != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::scanAndRecover() - Failed to read head sector %u\n", head_candidate);
        return false;
    }
    
    // Check for partial writes in head sector by scanning records
    uint32_t verified_used = scanSectorRecords(head_candidate, head_header.used);
    if (verified_used != head_header.used) {
        USB_SERIAL_PRINTF("FlashRingBuffer::scanAndRecover() - Head sector %u has partial write, repairing: %u -> %u\n", 
                  head_candidate, head_header.used, verified_used);
        
        // Update sector header with corrected usage
        head_header.used = verified_used;
        head_header.crc32 = calculateCRC32((const uint8_t*)&head_header, 
                                          sizeof(head_header) - sizeof(head_header.crc32));
        
        if (writeSector(head_candidate, &head_header, SECTOR_HEADER_SIZE) != ESP_OK) {
            USB_SERIAL_PRINTLN("FlashRingBuffer::scanAndRecover() - Failed to repair head sector header");
            return false;
        }
    }
    
    // Set recovered state
    m_state.head_sector = head_candidate;
    m_state.tail_sector = tail_candidate;
    m_state.head_sector_used = head_header.used;
    m_state.next_seq_number = highest_seq + 1;
    m_state.write_count++; // Increment to track recovery
    
    USB_SERIAL_PRINTF("FlashRingBuffer::scanAndRecover() - Recovery complete: head=%u, tail=%u, next_seq=%u\n", 
              m_state.head_sector, m_state.tail_sector, m_state.next_seq_number);
    
    return true;
}

uint32_t FlashRingBuffer::scanSectorRecords(uint32_t sector_index, uint32_t claimed_used) {
    uint8_t* sector_data = m_sector_buffer; // Reuse sector buffer
    esp_err_t err = readSector(sector_index, sector_data, SECTOR_SIZE);
    
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::scanSectorRecords() - Failed to read sector %u\n", sector_index);
        return 0;
    }
    
    uint32_t verified_used = 0;
    uint32_t offset = SECTOR_HEADER_SIZE;
    
    while (offset + RECORD_HEADER_SIZE <= SECTOR_SIZE && offset < SECTOR_HEADER_SIZE + claimed_used) {
        RecordHeader* record = (RecordHeader*)(sector_data + offset);
        
        // Check if this looks like a valid record
        if (record->len < MIN_MESSAGE_SIZE || record->len > MAX_MESSAGE_SIZE) {
            USB_SERIAL_PRINTF("FlashRingBuffer::scanSectorRecords() - Invalid record length %u at offset %u\n", 
                      record->len, offset);
            break;
        }
        
        uint32_t total_record_size = RECORD_HEADER_SIZE + record->len;
        if (offset + total_record_size > SECTOR_SIZE) {
            USB_SERIAL_PRINTF("FlashRingBuffer::scanSectorRecords() - Record extends beyond sector at offset %u\n", offset);
            break;
        }
        
        // Check if record is marked as valid
        if (record->valid != 0x00) {
            USB_SERIAL_PRINTF("FlashRingBuffer::scanSectorRecords() - Incomplete record at offset %u (valid=0x%02x)\n", 
                      offset, record->valid);
            break;
        }
        
        // Verify CRC
        uint8_t* payload = sector_data + offset + RECORD_HEADER_SIZE;
        uint16_t calculated_crc = calculateCRC16(payload, record->len);
        if (calculated_crc != record->crc16) {
            USB_SERIAL_PRINTF("FlashRingBuffer::scanSectorRecords() - CRC mismatch at offset %u\n", offset);
            break;
        }
        
        // Record is valid
        verified_used = offset + total_record_size - SECTOR_HEADER_SIZE;
        offset += total_record_size;
    }
    
    USB_SERIAL_PRINTF("FlashRingBuffer::scanSectorRecords() - Sector %u: claimed=%u, verified=%u\n", 
              sector_index, claimed_used, verified_used);
    
    return verified_used;
}

// Power-On Self-Test with comprehensive diagnostics
bool FlashRingBuffer::performPowerOnSelfTest(bool auto_repair) {
    USB_SERIAL_PRINTLN("=== FlashRingBuffer Power-On Self-Test ===");
    uint32_t start_time = m_fn_millis ? m_fn_millis() : 0;
    
    FlashDiagnostics diag;
    memset(&diag, 0, sizeof(diag));
    diag.total_sectors = TOTAL_SECTORS;
    
    // Test 1: Partition accessibility
    USB_SERIAL_PRINTLN("POST: Testing partition accessibility...");
    diag.partition_found = (m_partition != nullptr);
    if (!diag.partition_found) {
        USB_SERIAL_PRINTLN("POST: CRITICAL - Flash partition not found!");
        return false;
    }
    
    // Test partition read/write access
    uint8_t test_data[16] = {0xAA, 0x55, 0xAA, 0x55, 0xCC, 0x33, 0xCC, 0x33,
                            0xFF, 0x00, 0xFF, 0x00, 0x12, 0x34, 0x56, 0x78};
    uint8_t read_buffer[16];
    
    esp_err_t err = esp_partition_write(m_partition, 0, test_data, sizeof(test_data));
    if (err == ESP_OK) {
        err = esp_partition_read(m_partition, 0, read_buffer, sizeof(read_buffer));
        diag.partition_accessible = (err == ESP_OK && memcmp(test_data, read_buffer, sizeof(test_data)) == 0);
    }
    
    if (!diag.partition_accessible) {
        USB_SERIAL_PRINTLN("POST: CRITICAL - Flash partition not accessible!");
        return false;
    }
    USB_SERIAL_PRINTLN("POST: Partition accessibility... OK");
    
    // Test 2: Comprehensive sector analysis
    USB_SERIAL_PRINTLN("POST: Analyzing flash sectors...");
    bool corruption_detected = false;
    
    for (uint32_t sector = 0; sector < TOTAL_SECTORS; sector++) {
        SectorHeader header;
        esp_err_t read_err = readSector(sector, &header, sizeof(header));
        
        if (read_err != ESP_OK) {
            diag.corrupted_sectors++;
            corruption_detected = true;
            continue;
        }
        
        if (header.magic != SECTOR_MAGIC) {
            diag.empty_sectors++;
            continue;
        }
        
        // Validate sector header
        if (header.used > USABLE_SECTOR_SIZE) {
            USB_SERIAL_PRINTF("POST: Sector %u has invalid usage: %u bytes\n", sector, header.used);
            diag.corrupted_sectors++;
            corruption_detected = true;
            continue;
        }
        
        // Validate header CRC
        uint32_t calculated_crc = calculateCRC32((const uint8_t*)&header, 
                                                sizeof(header) - sizeof(header.crc32));
        if (calculated_crc != header.crc32) {
            USB_SERIAL_PRINTF("POST: Sector %u header CRC mismatch\n", sector);
            diag.corrupted_sectors++;
            corruption_detected = true;
            continue;
        }
        
        // Validate records in sector
        uint32_t verified_used = scanSectorRecords(sector, header.used);
        if (verified_used != header.used) {
            USB_SERIAL_PRINTF("POST: Sector %u record corruption detected\n", sector);
            diag.corrupted_sectors++;
            corruption_detected = true;
            continue;
        }
        
        // Enhanced power-loss detection: Validate sector completion with sentinel pattern
        if (!validateSectorComplete(sector)) {
            USB_SERIAL_PRINTF("POST: Sector %u failed sentinel completion validation\n", sector);
            diag.corrupted_sectors++;
            corruption_detected = true;
            continue;
        }
        
        diag.valid_sectors++;
        
        // Count valid records (simplified)
        if (header.used > 0) {
            diag.total_records += (header.used / (RECORD_HEADER_SIZE + 64)); // Estimate
        }
        
        // Yield every 32 sectors to prevent watchdog
        if (sector % 32 == 0) {
            delay(1);
        }
    }
    
    USB_SERIAL_PRINTF("POST: Sector analysis complete - Valid: %u, Corrupted: %u, Empty: %u\n", 
              diag.valid_sectors, diag.corrupted_sectors, diag.empty_sectors);
    
    // Test 3: State consistency check
    USB_SERIAL_PRINTLN("POST: Checking state consistency...");
    diag.state_load_success = loadPersistedState();
    
    if (diag.state_load_success) {
        // Validate state consistency
        diag.head_tail_consistency = (m_state.head_sector < TOTAL_SECTORS && 
                                     m_state.tail_sector < TOTAL_SECTORS &&
                                     m_state.head_sector_used <= USABLE_SECTOR_SIZE);
        
        if (!diag.head_tail_consistency) {
            USB_SERIAL_PRINTF("POST: State inconsistency detected - head: %u, tail: %u, used: %u\n",
                      m_state.head_sector, m_state.tail_sector, m_state.head_sector_used);
        }
    }
    
    // Test 4: Auto-repair if requested and corruption detected
    if (auto_repair && (corruption_detected || !diag.head_tail_consistency)) {
        USB_SERIAL_PRINTLN("POST: Corruption detected, attempting auto-repair...");
        diag.auto_repair_attempted = true;
        diag.auto_repair_successful = repairCorruption();
        
        if (diag.auto_repair_successful) {
            USB_SERIAL_PRINTLN("POST: Auto-repair successful");
        } else {
            USB_SERIAL_PRINTLN("POST: Auto-repair failed");
        }
    }
    
    // Calculate diagnostics duration
    if (m_fn_millis) {
        diag.diagnostic_duration_ms = m_fn_millis() - start_time;
    }
    
    // Print comprehensive diagnostic report
    printDiagnosticReport(diag);
    
    // Determine overall health
    bool overall_health = diag.partition_accessible && 
                         (diag.corrupted_sectors == 0 || diag.auto_repair_successful);
    
    USB_SERIAL_PRINTF("=== POST Complete: %s ===\n", overall_health ? "PASSED" : "FAILED");
    
    return overall_health;
}

void FlashRingBuffer::printDiagnosticReport(const FlashDiagnostics& diag) const {
    USB_SERIAL_PRINTLN("=== Diagnostic Report ===");
    USB_SERIAL_PRINTF("Partition Found: %s\n", diag.partition_found ? "Yes" : "No");
    USB_SERIAL_PRINTF("Partition Accessible: %s\n", diag.partition_accessible ? "Yes" : "No");
    USB_SERIAL_PRINTF("Total Sectors: %u\n", diag.total_sectors);
    USB_SERIAL_PRINTF("Valid Sectors: %u\n", diag.valid_sectors);
    USB_SERIAL_PRINTF("Corrupted Sectors: %u\n", diag.corrupted_sectors);
    USB_SERIAL_PRINTF("Empty Sectors: %u\n", diag.empty_sectors);
    USB_SERIAL_PRINTF("State Load Success: %s\n", diag.state_load_success ? "Yes" : "No");
    USB_SERIAL_PRINTF("Head/Tail Consistency: %s\n", diag.head_tail_consistency ? "Yes" : "No");
    USB_SERIAL_PRINTF("Auto-Repair Attempted: %s\n", diag.auto_repair_attempted ? "Yes" : "No");
    if (diag.auto_repair_attempted) {
        USB_SERIAL_PRINTF("Auto-Repair Successful: %s\n", diag.auto_repair_successful ? "Yes" : "No");
    }
    USB_SERIAL_PRINTF("Diagnostic Duration: %u ms\n", diag.diagnostic_duration_ms);
    USB_SERIAL_PRINTLN("========================");
}

// Reset and recovery functions
bool FlashRingBuffer::factoryReset() {
    USB_SERIAL_PRINTLN("FlashRingBuffer::factoryReset() - Starting factory reset");
    
    if (!m_partition) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::factoryReset() - No partition found");
        return false;
    }
    
    // Clear all persistent state from EEPROM/Preferences
    if (m_preferences.begin("flashring", false)) {
        m_preferences.clear();
        m_preferences.end();
        USB_SERIAL_PRINTLN("FlashRingBuffer::factoryReset() - Cleared EEPROM preferences");
    }
    
    // Clear all flash data
    if (!clearAllData()) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::factoryReset() - Failed to clear flash data");
        return false;
    }
    
    // Reset internal state
    memset(&m_state, 0, sizeof(m_state));
    m_buffer_used = 0;
    
    // Re-initialize if we were previously initialized
    if (m_initialized) {
        m_initialized = false;
        USB_SERIAL_PRINTLN("FlashRingBuffer::factoryReset() - Re-initializing after reset");
        
        if (!init(m_fn_millis)) {
            return false;
        }
    }
    
    USB_SERIAL_PRINTLN("FlashRingBuffer::factoryReset() - Factory reset complete");
    return true;
}

bool FlashRingBuffer::clearAllData() {
    USB_SERIAL_PRINTLN("FlashRingBuffer::clearAllData() - Erasing entire flash partition");
    
    if (!m_partition) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::clearAllData() - No partition found");
        return false;
    }
    
    // Erase entire partition in chunks to avoid watchdog timeout
    const uint32_t CHUNK_SIZE = 64 * 1024; // 64KB chunks
    uint32_t erased_bytes = 0;
    
    while (erased_bytes < m_partition->size) {
        uint32_t chunk_size = (m_partition->size - erased_bytes > CHUNK_SIZE) ? 
                             CHUNK_SIZE : (m_partition->size - erased_bytes);
        
        USB_SERIAL_PRINTF("FlashRingBuffer::clearAllData() - Erasing chunk at 0x%x, size %u\n", 
                  erased_bytes, chunk_size);
        
        esp_err_t err = esp_partition_erase_range(m_partition, erased_bytes, chunk_size);
        if (err != ESP_OK) {
            USB_SERIAL_PRINTF("FlashRingBuffer::clearAllData() - Failed to erase chunk at 0x%x: %s\n", 
                      erased_bytes, esp_err_to_name(err));
            return false;
        }
        
        erased_bytes += chunk_size;
        
        // Yield periodically to prevent watchdog timeout
        if (erased_bytes % (4 * CHUNK_SIZE) == 0) {
            delay(10);
        }
    }
    
    USB_SERIAL_PRINTF("FlashRingBuffer::clearAllData() - Successfully erased %u bytes\n", erased_bytes);
    return true;
}

bool FlashRingBuffer::repairCorruption() {
    USB_SERIAL_PRINTLN("FlashRingBuffer::repairCorruption() - Starting corruption repair");
    
    if (!m_partition) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::repairCorruption() - No partition found");
        return false;
    }
    
    uint32_t corrupted_sectors = 0;
    uint32_t repaired_sectors = 0;
    bool major_repairs_needed = false;
    
    // Scan all sectors and repair what we can
    for (uint32_t sector = 0; sector < TOTAL_SECTORS; sector++) {
        SectorHeader header;
        esp_err_t read_err = readSector(sector, &header, sizeof(header));
        
        if (read_err != ESP_OK) {
            USB_SERIAL_PRINTF("FlashRingBuffer::repairCorruption() - Cannot read sector %u: %s\n", 
                      sector, esp_err_to_name(read_err));
            corrupted_sectors++;
            
            // Erase corrupted sector
            if (eraseSector(sector) == ESP_OK) {
                repaired_sectors++;
            }
            continue;
        }
        
        if (header.magic == SECTOR_MAGIC) {
            // Validate sector usage
            if (header.used > USABLE_SECTOR_SIZE) {
                USB_SERIAL_PRINTF("FlashRingBuffer::repairCorruption() - Sector %u has invalid usage: %u\n", 
                          sector, header.used);
                corrupted_sectors++;
                
                // Try to repair by erasing sector
                if (eraseSector(sector) == ESP_OK) {
                    USB_SERIAL_PRINTF("FlashRingBuffer::repairCorruption() - Erased corrupted sector %u\n", sector);
                    repaired_sectors++;
                }
                continue;
            }
            
            // Validate header CRC
            uint32_t calculated_crc = calculateCRC32((const uint8_t*)&header, 
                                                    sizeof(header) - sizeof(header.crc32));
            if (calculated_crc != header.crc32) {
                USB_SERIAL_PRINTF("FlashRingBuffer::repairCorruption() - Sector %u header CRC mismatch\n", sector);
                corrupted_sectors++;
                
                // Try to repair CRC
                header.crc32 = calculated_crc;
                if (writeSector(sector, &header, SECTOR_HEADER_SIZE) == ESP_OK) {
                    USB_SERIAL_PRINTF("FlashRingBuffer::repairCorruption() - Repaired sector %u header\n", sector);
                    repaired_sectors++;
                }
                continue;
            }
            
            // Check record integrity
            uint32_t verified_used = scanSectorRecords(sector, header.used);
            if (verified_used != header.used) {
                USB_SERIAL_PRINTF("FlashRingBuffer::repairCorruption() - Sector %u record corruption: %u -> %u\n", 
                          sector, header.used, verified_used);
                corrupted_sectors++;
                
                // Try to repair by updating header
                header.used = verified_used;
                header.crc32 = calculateCRC32((const uint8_t*)&header, 
                                             sizeof(header) - sizeof(header.crc32));
                if (writeSector(sector, &header, SECTOR_HEADER_SIZE) == ESP_OK) {
                    USB_SERIAL_PRINTF("FlashRingBuffer::repairCorruption() - Repaired sector %u records\n", sector);
                    repaired_sectors++;
                }
            }
            
            // Check sector completion with sentinel validation
            if (!validateSectorComplete(sector)) {
                USB_SERIAL_PRINTF("FlashRingBuffer::repairCorruption() - Sector %u failed sentinel completion validation\n", sector);
                corrupted_sectors++;
                
                // For incomplete sectors, we need to erase and lose the data
                // This is the safest approach for power-loss recovery
                if (eraseSector(sector) == ESP_OK) {
                    USB_SERIAL_PRINTF("FlashRingBuffer::repairCorruption() - Erased incomplete sector %u\n", sector);
                    repaired_sectors++;
                } else {
                    USB_SERIAL_PRINTF("FlashRingBuffer::repairCorruption() - Failed to erase incomplete sector %u\n", sector);
                }
            }
        }
        
        // Yield every 32 sectors
        if (sector % 32 == 0) {
            delay(1);
        }
    }
    
    USB_SERIAL_PRINTF("FlashRingBuffer::repairCorruption() - Repair complete: %u corrupted, %u repaired\n", 
              corrupted_sectors, repaired_sectors);
    
    // Rebuild state if major repairs were made
    if (corrupted_sectors > 0) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::repairCorruption() - Re-scanning after repairs");
        return scanAndRecover();
    }
    
    return (repaired_sectors >= corrupted_sectors / 2); // At least 50% repair success rate
}

#define BUILD_INCLUDE_FLASHRINGBUFFER_PART3

#include "FlashRingBuffer_part3.cpp"

#endif
