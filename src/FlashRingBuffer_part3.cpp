/**
 * @file FlashRingBuffer_part3.cpp
 * @brief Self-test, stress/recovery tests and failure injection.
 *
 * Compiled by inclusion from FlashRingBuffer_part2.cpp.
 *
 * SAFETY RULE for every test in this file: tests that WRITE to the ring refuse
 * to run unless the ring is completely empty. A dive backlog is never used as
 * test material and never polluted with test records. Read-only checks always
 * run.
 */

#ifdef BUILD_INCLUDE_FLASHRINGBUFFER_PART3

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------

bool FlashRingBuffer::performSelfTest() {
    USB_SERIAL_PRINTLN("FlashRingBuffer::performSelfTest() - starting");

    if (!m_initialized) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::performSelfTest() - not initialized");
        return false;
    }

    if (!isEmpty()) {
        USB_SERIAL_PRINTF("FlashRingBuffer::performSelfTest() - %u records stored: running READ-ONLY checks only "
                          "(destructive write test requires an empty ring)\n", getRecordCount());
        return performPowerOnSelfTest(false);
    }

    bool all_passed = true;

    // Write/peek/consume cycle across the full size range.
    const uint16_t test_sizes[] = {MIN_MESSAGE_SIZE, 32, 64, 128, 256, 512, MAX_MESSAGE_SIZE};
    const uint8_t num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);

    uint8_t* test_data = (uint8_t*)malloc(MAX_MESSAGE_SIZE);
    uint8_t* read_data = (uint8_t*)malloc(MAX_MESSAGE_SIZE);
    if (!test_data || !read_data) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::performSelfTest() - allocation failed");
        if (test_data) free(test_data);
        if (read_data) free(read_data);
        return false;
    }

    for (uint8_t i = 0; i < num_sizes && all_passed; i++) {
        uint16_t size = test_sizes[i];
        uint16_t meta_in = (uint16_t)(0xC0DE + i);

        for (uint16_t j = 0; j < size; j++) {
            test_data[j] = (uint8_t)(j ^ 0x5A ^ (i << 3));
        }

        if (!appendRecord(test_data, size, meta_in)) {
            USB_SERIAL_PRINTF("SelfTest: append failed for %u bytes\n", size);
            all_passed = false;
            break;
        }
        if (!flush()) {
            USB_SERIAL_PRINTLN("SelfTest: flush failed");
            all_passed = false;
            break;
        }

        uint16_t actual_len = 0, meta_out = 0;
        if (!peekOldestRecord(read_data, MAX_MESSAGE_SIZE, actual_len, meta_out)) {
            USB_SERIAL_PRINTF("SelfTest: peek failed for %u bytes\n", size);
            all_passed = false;
            break;
        }
        if (actual_len != size || meta_out != meta_in ||
            memcmp(test_data, read_data, size) != 0) {
            USB_SERIAL_PRINTF("SelfTest: data mismatch for %u bytes (len %u, meta 0x%04X vs 0x%04X)\n",
                              size, actual_len, meta_out, meta_in);
            all_passed = false;
            break;
        }

        // Stable peek: a second peek must return the same record.
        uint16_t len2 = 0, meta2 = 0;
        if (!peekOldestRecord(read_data, MAX_MESSAGE_SIZE, len2, meta2) ||
            len2 != size || meta2 != meta_in) {
            USB_SERIAL_PRINTLN("SelfTest: repeated peek not stable");
            all_passed = false;
            break;
        }

        if (!consumeOldestRecord()) {
            USB_SERIAL_PRINTF("SelfTest: consume failed for %u bytes\n", size);
            all_passed = false;
            break;
        }

        USB_SERIAL_PRINTF("SelfTest: %u byte record... OK\n", size);
    }

    // Size limit enforcement.
    if (all_passed) {
        if (appendRecord(test_data, MAX_MESSAGE_SIZE, 0) &&
            appendRecord(test_data, MIN_MESSAGE_SIZE, 0)) {
            // valid appends fine - now clear them again below
        }
        flush();
        uint16_t len = 0, meta = 0;
        while (peekOldestRecord(read_data, MAX_MESSAGE_SIZE, len, meta)) {
            consumeOldestRecord();
        }

        uint8_t small[MIN_MESSAGE_SIZE - 1];
        memset(small, 0, sizeof(small));
        if (appendRecord(small, sizeof(small), 0)) {
            USB_SERIAL_PRINTLN("SelfTest: FAILED - undersized record accepted");
            all_passed = false;
        }
        // oversized: cannot exceed MAX via uint16 easily without a big buffer;
        // use length parameter beyond the limit against the existing buffer.
        if (appendRecord(test_data, MAX_MESSAGE_SIZE + 1, 0)) {
            USB_SERIAL_PRINTLN("SelfTest: FAILED - oversized record accepted");
            all_passed = false;
        }
    }

    // Empty-buffer behaviour.
    if (all_passed && isEmpty()) {
        uint16_t len = 0, meta = 0;
        if (peekOldestRecord(read_data, MAX_MESSAGE_SIZE, len, meta)) {
            USB_SERIAL_PRINTLN("SelfTest: FAILED - peek succeeded on empty ring");
            all_passed = false;
        }
        if (consumeOldestRecord()) {
            USB_SERIAL_PRINTLN("SelfTest: FAILED - consume succeeded on empty ring");
            all_passed = false;
        }
    }

    free(test_data);
    free(read_data);

    USB_SERIAL_PRINTF("FlashRingBuffer::performSelfTest() - %s\n", all_passed ? "PASSED" : "FAILED");
    return all_passed;
}

// ---------------------------------------------------------------------------
// Power-loss recovery test (empty ring only)
// ---------------------------------------------------------------------------

bool FlashRingBuffer::performPowerLossRecoveryTest() {
    USB_SERIAL_PRINTLN("=== FlashRingBuffer Power-Loss Recovery Test ===");

    if (!m_initialized) {
        return false;
    }
    if (!isEmpty()) {
        USB_SERIAL_PRINTF("Recovery test: REFUSED - %u records stored. Test requires an empty ring "
                          "(would pollute live data).\n", getRecordCount());
        return false;
    }

    const uint16_t record_size = 200;
    const uint8_t num_records = 10;
    bool passed = true;

    uint8_t* data = (uint8_t*)malloc(record_size);
    if (!data) {
        return false;
    }

    // 1. Write a batch of known records and flush them.
    for (uint8_t i = 0; i < num_records && passed; i++) {
        for (uint16_t j = 0; j < record_size; j++) {
            data[j] = (uint8_t)(j ^ 0x5A ^ i);
        }
        passed = appendRecord(data, record_size, i);
    }
    passed = passed && flush();

    // 2. Simulate a torn append: program a partial record (header + some
    //    payload, bad CRC region) directly after the flushed records.
    if (passed) {
        RecordHeader torn;
        torn.len = record_size;
        torn.crc16 = 0x1234;   // wrong on purpose
        torn.meta = 0xDEAD;
        torn.reserved = 0;
        uint32_t offset = m_head_sector * SECTOR_SIZE + m_head_write_offset;
        uint8_t partial[64];
        memset(partial, 0xA7, sizeof(partial));
        passed = writeBytes(offset, &torn, sizeof(torn)) == ESP_OK &&
                 writeBytes(offset + sizeof(torn), partial, sizeof(partial)) == ESP_OK;
        USB_SERIAL_PRINTLN("Recovery test: torn record injected after valid records");
    }

    // 3. Simulate the reboot: teardown WITHOUT flushing state cleanly first
    //    would be ideal, but teardown persists - which matches a clean case.
    //    To exercise the scan path, wipe the RAM state and re-init.
    if (passed) {
        uint32_t expected = num_records;
        teardown();
        if (!init(m_fn_millis)) {
            USB_SERIAL_PRINTLN("Recovery test: re-init failed");
            passed = false;
        } else {
            uint32_t recovered = getRecordCount();
            USB_SERIAL_PRINTF("Recovery test: %u/%u records recovered after simulated power loss\n",
                              recovered, expected);
            if (recovered != expected) {
                passed = false;
            }
        }
    }

    // 4. Verify every record is intact, then leave the ring empty again.
    if (passed) {
        uint8_t* read_back = (uint8_t*)malloc(record_size);
        if (read_back) {
            for (uint8_t i = 0; i < num_records && passed; i++) {
                uint16_t len = 0, meta = 0;
                if (!peekOldestRecord(read_back, record_size, len, meta) ||
                    len != record_size || meta != i) {
                    USB_SERIAL_PRINTF("Recovery test: record %u unreadable after recovery\n", i);
                    passed = false;
                    break;
                }
                for (uint16_t j = 0; j < record_size; j++) {
                    if (read_back[j] != (uint8_t)(j ^ 0x5A ^ i)) {
                        USB_SERIAL_PRINTF("Recovery test: record %u data corrupt\n", i);
                        passed = false;
                        break;
                    }
                }
                consumeOldestRecord();
            }
            free(read_back);
        } else {
            passed = false;
        }
    }

    free(data);

    USB_SERIAL_PRINTF("=== Power-Loss Recovery Test: %s ===\n", passed ? "PASSED" : "FAILED");
    return passed;
}

// ---------------------------------------------------------------------------
// Stress test (empty ring only)
// ---------------------------------------------------------------------------

bool FlashRingBuffer::performStressTest(uint32_t num_records) {
    USB_SERIAL_PRINTF("=== FlashRingBuffer Stress Test (%u records) ===\n", num_records);

    if (!m_initialized) {
        return false;
    }
    if (!isEmpty()) {
        USB_SERIAL_PRINTF("Stress test: REFUSED - %u records stored. Test requires an empty ring.\n",
                          getRecordCount());
        return false;
    }

    uint32_t start_time = m_fn_millis ? m_fn_millis() : 0;
    const uint16_t test_sizes[] = {MIN_MESSAGE_SIZE, 64, 128, 256, 512, MAX_MESSAGE_SIZE};
    const uint8_t num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);

    uint8_t* buffer = (uint8_t*)malloc(MAX_MESSAGE_SIZE);
    if (!buffer) {
        return false;
    }

    bool passed = true;
    uint32_t writes_ok = 0, reads_ok = 0, data_errors = 0;

    // Phase 1: writes.
    for (uint32_t i = 0; i < num_records; i++) {
        uint16_t size = test_sizes[i % num_sizes];
        for (uint16_t j = 0; j < size; j++) {
            buffer[j] = (uint8_t)((i + j) ^ 0xA5);
        }
        if (appendRecord(buffer, size, (uint16_t)i)) {
            writes_ok++;
        } else {
            USB_SERIAL_PRINTF("Stress test: write %u failed\n", i);
            passed = false;
            break;
        }
        if ((i & 0x3F) == 0) {
            delay(1);
        }
        if (i && (i % 200 == 0)) {
            USB_SERIAL_PRINTF("Stress test: %u/%u written\n", i, num_records);
        }
    }
    flush();

    // Phase 2: read back in order and verify.
    if (passed) {
        for (uint32_t i = 0; i < writes_ok; i++) {
            uint16_t expected_size = test_sizes[i % num_sizes];
            uint16_t len = 0, meta = 0;
            if (!peekOldestRecord(buffer, MAX_MESSAGE_SIZE, len, meta)) {
                USB_SERIAL_PRINTF("Stress test: peek %u failed\n", i);
                passed = false;
                break;
            }
            bool ok = (len == expected_size) && (meta == (uint16_t)i);
            for (uint16_t j = 0; ok && j < expected_size; j++) {
                ok = (buffer[j] == (uint8_t)((i + j) ^ 0xA5));
            }
            if (ok) {
                reads_ok++;
            } else {
                data_errors++;
                USB_SERIAL_PRINTF("Stress test: record %u corrupt (len %u vs %u, meta %u)\n",
                                  i, len, expected_size, meta);
                if (data_errors > 10) {
                    passed = false;
                    break;
                }
            }
            consumeOldestRecord();
            if ((i & 0x3F) == 0) {
                delay(1);
            }
        }
    }

    free(buffer);

    uint32_t duration = m_fn_millis ? (m_fn_millis() - start_time) : 0;
    passed = passed && (data_errors == 0) && (reads_ok == writes_ok);

    USB_SERIAL_PRINTF("Stress test: %u written, %u read OK, %u data errors, %u records left, %u ms (%.1f rec/s)\n",
                      writes_ok, reads_ok, data_errors, getRecordCount(), duration,
                      duration ? (float)(writes_ok * 1000) / duration : 0.0f);
    USB_SERIAL_PRINTF("=== Stress Test: %s ===\n", passed ? "PASSED" : "FAILED");
    return passed;
}

// ---------------------------------------------------------------------------
// Failure injection (TESTING_MODE only)
// ---------------------------------------------------------------------------

#ifdef TESTING_MODE

bool FlashRingBuffer::injectSectorCorruption(uint32_t sector_index) {
    if (!m_initialized || sector_index >= TOTAL_SECTORS) {
        return false;
    }
    USB_SERIAL_PRINTF("INJECT: corrupting magic of sector %u\n", sector_index);
    // Programming over the existing magic ANDs bits to garbage - exactly the
    // corruption class the header CRC must catch.
    uint32_t garbage = 0x00000000;
    return writeBytes(sector_index * SECTOR_SIZE, &garbage, sizeof(garbage)) == ESP_OK;
}

bool FlashRingBuffer::injectCRCCorruption(uint32_t sector_index) {
    if (!m_initialized || sector_index >= TOTAL_SECTORS) {
        return false;
    }
    USB_SERIAL_PRINTF("INJECT: corrupting header CRC of sector %u\n", sector_index);
    uint32_t garbage = 0x00000000;  // hdr_crc at offset 8
    return writeBytes(sector_index * SECTOR_SIZE + 8, &garbage, sizeof(garbage)) == ESP_OK;
}

bool FlashRingBuffer::corruptPersistedState() {
    if (!m_initialized) {
        return false;
    }
    USB_SERIAL_PRINTLN("INJECT: corrupting NVS state (restart to test recovery)");
    uint8_t garbage[sizeof(FlashRingPersistedState)];
    memset(garbage, 0xA5, sizeof(garbage));
    return m_preferences.putBytes("state2", garbage, sizeof(garbage)) == sizeof(garbage);
}

bool FlashRingBuffer::simulateIncompleteWrite() {
    if (!m_initialized) {
        return false;
    }
    USB_SERIAL_PRINTLN("INJECT: simulating power-loss during append");
    flush();
    if (!m_head_open) {
        if (!openNextHeadSector()) {
            return false;
        }
    }
    RecordHeader torn;
    torn.len = 224;
    torn.crc16 = 0x1234;  // wrong
    torn.meta = 0xDEAD;
    torn.reserved = 0;
    uint8_t partial[32];
    memset(partial, 0xB6, sizeof(partial));
    uint32_t offset = m_head_sector * SECTOR_SIZE + m_head_write_offset;
    bool ok = writeBytes(offset, &torn, sizeof(torn)) == ESP_OK &&
              writeBytes(offset + sizeof(torn), partial, sizeof(partial)) == ESP_OK;
    USB_SERIAL_PRINTLN("INJECT: torn record written - restart; boot recovery must rescue the sector");
    return ok;
}

bool FlashRingBuffer::corruptRingPointers() {
    if (!m_initialized) {
        return false;
    }
    USB_SERIAL_PRINTLN("INJECT: writing absurd (but CRC-valid) cursor state to NVS");
    FlashRingPersistedState state;
    state.version = 2;
    state.tail_sector = TOTAL_SECTORS - 1;
    state.tail_seq = 0xFFFFFFF0;   // will not match any scanned sector
    state.tail_offset = SECTOR_SIZE;
    state.tail_consumed = 0xFFFF;
    state.next_seq = 1;
    state.write_count = 0;
    state.crc = calculateCRC32((const uint8_t*)&state, sizeof(state) - sizeof(state.crc));
    m_preferences.putBytes("state2", &state, sizeof(state));
    USB_SERIAL_PRINTLN("INJECT: restart - boot must ignore the mismatched cursor and rescan");
    return true;
}

bool FlashRingBuffer::acceleratedWearTest(uint32_t cycles) {
    if (!m_initialized) {
        return false;
    }
    if (!isEmpty()) {
        USB_SERIAL_PRINTLN("INJECT: wear test refused - ring not empty");
        return false;
    }
    USB_SERIAL_PRINTF("INJECT: accelerated wear test, %u sector cycles\n", cycles);
    uint8_t data[200];
    uint32_t start = m_fn_millis ? m_fn_millis() : 0;
    for (uint32_t i = 0; i < cycles; i++) {
        for (int j = 0; j < 200; j++) {
            data[j] = (uint8_t)(j + i);
        }
        uint32_t start_sector = m_head_sector;
        // Fill until the head rotates to the next sector.
        do {
            if (!appendRecord(data, sizeof(data), (uint16_t)i)) {
                return false;
            }
            flush();
        } while (m_head_sector == start_sector || !m_head_open);
        // Drain to keep the ring from filling.
        uint16_t len, meta;
        uint8_t sink[200];
        while (peekOldestRecord(sink, sizeof(sink), len, meta)) {
            consumeOldestRecord();
        }
        if ((i % 10) == 0) {
            delay(1);
            USB_SERIAL_PRINTF("INJECT: wear cycle %u/%u\n", i, cycles);
        }
    }
    uint32_t duration = (m_fn_millis ? m_fn_millis() : 0) - start;
    USB_SERIAL_PRINTF("INJECT: wear test done in %u ms, lifetime program ops now %u\n",
                      duration, m_write_count);
    return true;
}

bool FlashRingBuffer::injectRandomCorruption(uint32_t num_sectors) {
    if (!m_initialized || num_sectors == 0 || num_sectors > TOTAL_SECTORS / 4) {
        return false;
    }
    USB_SERIAL_PRINTF("INJECT: corrupting %u random sectors\n", num_sectors);
    for (uint32_t i = 0; i < num_sectors; i++) {
        uint32_t sector = rand() % TOTAL_SECTORS;
        if (rand() & 1) {
            injectSectorCorruption(sector);
        } else {
            injectCRCCorruption(sector);
        }
    }
    return true;
}

bool FlashRingBuffer::simulatePartitionFailure() {
    if (!m_initialized) {
        return false;
    }
    USB_SERIAL_PRINTLN("INJECT: disabling partition access (restart to restore)");
    m_partition = nullptr;
    return true;
}

void FlashRingBuffer::enableFailureInjection() {
    USB_SERIAL_PRINTLN("WARNING: failure injection methods active (TESTING_MODE build)");
}

#endif // TESTING_MODE

#endif // BUILD_INCLUDE_FLASHRINGBUFFER_PART3
