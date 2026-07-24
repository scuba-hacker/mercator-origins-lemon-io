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
    if (!m_initialized || sector_index >= RING_SECTORS) {
        USB_SERIAL_PRINTF("INJECT: sector corruption rejected - sector must be 0-%u\n",
                          RING_SECTORS - 1);
        return false;
    }
    if (mapGet(sector_index)) {
        // Destroying a live header permanently excludes its records from the
        // ring - recovery cannot reconstruct a header. Refuse live targets.
        USB_SERIAL_PRINTF("INJECT: sector corruption REFUSED - sector %u is in use%s%s (records would be lost)\n",
                          sector_index,
                          (sector_index == m_head_sector) ? " [HEAD]" : "",
                          (sector_index == m_tail_sector) ? " [TAIL]" : "");
        return false;
    }
    USB_SERIAL_PRINTF("INJECT: corrupting magic of sector %u\n", sector_index);
    // Programming over the existing magic ANDs bits to garbage - exactly the
    // corruption class the header CRC must catch.
    uint32_t garbage = 0x00000000;
    return writeBytes(sector_index * SECTOR_SIZE, &garbage, sizeof(garbage)) == ESP_OK;
}

bool FlashRingBuffer::injectCRCCorruption(uint32_t sector_index) {
    if (!m_initialized || sector_index >= RING_SECTORS) {
        USB_SERIAL_PRINTF("INJECT: CRC corruption rejected - sector must be 0-%u\n",
                          RING_SECTORS - 1);
        return false;
    }
    if (mapGet(sector_index)) {
        USB_SERIAL_PRINTF("INJECT: CRC corruption REFUSED - sector %u is in use%s%s (records would be lost)\n",
                          sector_index,
                          (sector_index == m_head_sector) ? " [HEAD]" : "",
                          (sector_index == m_tail_sector) ? " [TAIL]" : "");
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
    if (!flush()) {
        USB_SERIAL_PRINTLN("INJECT: flush failed - aborting injection");
        return false;
    }
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
    // The injected bytes must fit inside the open head sector - a narrow
    // open head (< 40 bytes free) would push the write across the 4KB
    // boundary into the following sector. Rotate to a fresh sector instead.
    if ((SECTOR_SIZE - m_head_write_offset) < sizeof(torn) + sizeof(partial)) {
        USB_SERIAL_PRINTLN("INJECT: open head too full for torn record - rotating to a fresh sector");
        if (!closeHeadSector() || !openNextHeadSector()) {
            return false;
        }
    }
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
    state.tail_sector = RING_SECTORS - 1;
    state.tail_seq = 0xFFFFFFF0;   // will not match any scanned sector
    state.tail_offset = SECTOR_SIZE;
    state.tail_consumed = 0xFFFF;
    state.next_seq = 1;
    state.write_count = 0;
    state.crc = calculateCRC32((const uint8_t*)&state, sizeof(state) - sizeof(state.crc));
    if (m_preferences.putBytes("state2", &state, sizeof(state)) != sizeof(state)) {
        USB_SERIAL_PRINTLN("INJECT: NVS write failed - nothing injected");
        return false;
    }
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
    if (!m_initialized || num_sectors == 0 || num_sectors > RING_SECTORS / 4) {
        return false;
    }

    // Preselect unique, NOT-in-use targets so live telemetry is never
    // destroyed and the requested corruption count is actually delivered.
    uint32_t eligible = RING_SECTORS - mapCountInUse();
    if (num_sectors > eligible) {
        USB_SERIAL_PRINTF("INJECT: random corruption rejected - only %u free sectors available for %u requested\n",
                          eligible, num_sectors);
        return false;
    }

    USB_SERIAL_PRINTF("INJECT: corrupting %u unique random free sectors\n", num_sectors);
    uint32_t injected = 0, failed = 0;
    uint32_t attempts = 0;
    const uint32_t max_attempts = num_sectors * 50 + 100;
    while (injected + failed < num_sectors && attempts++ < max_attempts) {
        uint32_t sector = rand() % RING_SECTORS;
        if (mapGet(sector) || sector == JOURNAL_SECTOR) {
            continue;  // in use - never a target
        }
        SectorHeader probe;
        if (readSectorHeader(sector, probe) && !headerValid(probe)) {
            // Already blank-invalid or previously corrupted - pick another so
            // every injection produces a NEW detectable anomaly.
            const uint8_t* raw = (const uint8_t*)&probe;
            bool blank = true;
            for (uint32_t b = 0; b < SECTOR_HEADER_SIZE; b++) {
                if (raw[b] != 0xFF) { blank = false; break; }
            }
            if (!blank) {
                continue;
            }
        }
        bool ok = (rand() & 1) ? injectSectorCorruption(sector)
                               : injectCRCCorruption(sector);
        if (ok) {
            injected++;
        } else {
            failed++;
        }
    }

    USB_SERIAL_PRINTF("INJECT: random corruption result - %u injected, %u failed writes\n",
                      injected, failed);
    return injected == num_sectors;
}

bool FlashRingBuffer::simulatePartitionFailure() {
    if (!m_initialized) {
        return false;
    }
    USB_SERIAL_PRINTLN("INJECT: disabling partition access and entering fatal state");
    m_partition = nullptr;
    enterFatalState("injected partition failure");
    return true;
}

bool FlashRingBuffer::testRestorePartitionAccess() {
    return m_partition != nullptr || findPartition();
}

void FlashRingBuffer::enableFailureInjection() {
    USB_SERIAL_PRINTLN("WARNING: failure injection methods active (TESTING_MODE build)");
}

// ---------------------------------------------------------------------------
// Deterministic review-test matrix (sol-code-review-5.md)
//
// Every case asserts observable post-state - fatal flags, admission results,
// record counts, canonical state fields, on-flash headers - not log output.
// Fault seams make each failure deterministic. Cases clean up via
// matrixReset(), which erases only the low sectors the cases touch.
// ---------------------------------------------------------------------------

void FlashRingBuffer::matrixCase(const char* name, bool passed, bool& all_passed) {
    USB_SERIAL_PRINTF("MATRIX %-64s %s\n", name, passed ? "PASS" : "FAIL");
    if (!passed) {
        all_passed = false;
    }
}

bool FlashRingBuffer::matrixReset() {
    clearFaultSeams();
    if (!m_partition && !findPartition()) {
        return false;
    }
    m_fatal = false;           // allow the cleanup writes below
    m_writes_disabled = false;
    // The cases only ever touch the low sectors (the ring starts virgin at
    // sector 0 after each reset) plus the journal - a full-partition erase
    // per case would take minutes and add pointless wear.
    bool erased = true;
    for (uint32_t s = 0; s < RING_SECTORS; s++) {
        if ((s <= 15 || mapGet(s)) && eraseSector(s) != ESP_OK) {
            erased = false;
        }
    }
    if (eraseSector(JOURNAL_SECTOR) != ESP_OK) {
        erased = false;
    }
    if (!erased) {
        return false;
    }
    resetRuntimeState();
    m_preferences.clear();
    teardown();
    return init(m_fn_millis);
}

bool FlashRingBuffer::runReviewTestMatrix() {
    USB_SERIAL_PRINTLN("=== FlashRingBuffer deterministic review-test matrix ===");

    if (!m_initialized) {
        USB_SERIAL_PRINTLN("MATRIX: refused - not initialized");
        return false;
    }
    if (!isEmpty() || m_fatal) {
        USB_SERIAL_PRINTF("MATRIX: refused - ring must be empty and trusted (%u records, fatal=%d)\n",
                          getRecordCount(), (int)m_fatal);
        return false;
    }

    bool all = true;
    uint8_t rec[200];
    uint8_t readback[200];
    uint16_t rlen = 0, rmeta = 0;

    // --- Case 1 (matrix 1 / R12): partition disappears -> POST goes fatal,
    // append refuses, verified POST repair recovers once it returns.
    {
        bool injected = simulatePartitionFailure();
        bool immediate_fatal = m_fatal;
        bool post_failed = !performPowerOnSelfTest(true);
        bool fatal_now = m_fatal;
        memset(rec, 0x11, 32);
        bool append_refused = !appendRecord(rec, 32, 1);
        bool restored = testRestorePartitionAccess();
        bool recovered = restored && performPowerOnSelfTest(true) && !m_fatal;
        matrixCase("1 injected partition loss: immediate fatal, refusal, recovery",
                   injected && immediate_fatal && post_failed && fatal_now &&
                   append_refused && recovered, all);
        if (!matrixReset()) return false;
    }

    // --- Case 2 (matrix 2 / R12): the accessibility probe read fails ->
    // fatal, nothing accepted into the assembly afterwards.
    {
        m_seams.fail_read_countdown = 1;  // fires on POST's probe read
        bool post_failed = !performPowerOnSelfTest(true);
        bool fatal_now = m_fatal;
        memset(rec, 0x22, 32);
        bool append_refused = !appendRecord(rec, 32, 2);
        bool assembly_empty = (m_assembly_count == 0);
        clearFaultSeams();
        bool recovered = performPowerOnSelfTest(true) && !m_fatal;
        matrixCase("2 probe read fails: fatal, assembly stays empty, recovers",
                   post_failed && fatal_now && append_refused && assembly_empty && recovered, all);
        if (!matrixReset()) return false;
    }

    // --- Case 3 (matrix 3+4): recovery scan fails mid-flight with flash
    // unmodified -> the previous trusted state is retained WHOLESALE (no
    // partial candidate becomes live, not fatal), and the records remain
    // readable. (The modified-flash failure branch goes fatal instead - that
    // is exercised by the journal cases 5 and 6.)
    {
        for (uint8_t i = 0; i < 5; i++) {
            memset(rec, 0x30 + i, 100);
            appendRecord(rec, 100, i);
        }
        flush();
        bool five_stored = (m_record_count == 5);
        m_seams.fail_read_countdown = 5;   // journal header read + 4 sector headers
        bool scan_failed = !scanAndRecover(nullptr);
        clearFaultSeams();
        bool state_kept = (m_record_count == 5) && !m_fatal && !m_ring_virgin;
        bool readable = true;
        for (uint8_t i = 0; i < 5 && readable; i++) {
            readable = peekOldestRecord(readback, sizeof(readback), rlen, rmeta) &&
                       rlen == 100 && rmeta == i && consumeOldestRecord();
        }
        matrixCase("3 mid-scan failure (unmodified): previous state kept, data readable",
                   five_stored && scan_failed && state_kept && readable, all);
        if (!matrixReset()) return false;
    }

    // --- Case 4 (matrix 5 / R13): an erase followed by recovery failure is
    // fatal (never stale-trusted). A later complete recovery publishes the
    // canonical empty state and a new record survives "reboot".
    {
        for (uint8_t i = 0; i < 3; i++) {
            memset(rec, 0x40 + i, 100);
            appendRecord(rec, 100, i);
        }
        flush();
        uint32_t old_head = m_head_sector;
        uint32_t garbage = 0;
        writeBytes(old_head * SECTOR_SIZE, &garbage, 4);  // destroy own header
        m_seams.fail_recovery_before_sector_scan = true;
        bool first_post_failed = !performPowerOnSelfTest(true);
        bool failed_fatal = m_fatal;
        memset(rec, 0x4F, 32);
        bool append_refused = !appendRecord(rec, 32, 0x4F);
        bool post_ok = performPowerOnSelfTest(true);      // now canonical empty
        bool canonical = !m_head_open &&
                         m_head_write_offset == SECTOR_HEADER_SIZE &&
                         m_ring_virgin &&
                         m_record_count == 0 &&
                         m_assembly_count == 0;
        // canonical state must support a fresh write cycle that survives re-init
        for (uint16_t j = 0; j < 64; j++) rec[j] = (uint8_t)(j ^ 0xE7);
        bool wrote = appendRecord(rec, 64, 0x77) && flush();
        SectorHeader stamped;
        bool header_stamped = readSectorHeader(m_head_sector, stamped) && headerValid(stamped);
        teardown();
        bool reinit_ok = init(m_fn_millis);
        bool survived = reinit_ok && getRecordCount() == 1 &&
                        peekOldestRecord(readback, sizeof(readback), rlen, rmeta) &&
                        rlen == 64 && rmeta == 0x77 &&
                        memcmp(rec, readback, 64) == 0 &&
                        consumeOldestRecord();
        matrixCase("4 repair then pre-scan failure: fatal; retry canonical + durable",
                   first_post_failed && failed_fatal && append_refused && post_ok &&
                   canonical && wrote && header_stamped && survived, all);
        if (!matrixReset()) return false;
    }

    // Build one valid 40-byte record slot image (32B payload) for the
    // journal cases.
    uint8_t slot_image[40];
    {
        RecordHeader rh;
        rh.len = 32;
        rh.meta = 0x1234;
        rh.reserved = 0;
        uint8_t payload[32];
        for (uint16_t j = 0; j < 32; j++) payload[j] = (uint8_t)(j ^ 0xC3);
        rh.crc16 = recordCRC(rh.meta, payload, 32);
        memcpy(slot_image, &rh, RECORD_HEADER_SIZE);
        memcpy(slot_image + RECORD_HEADER_SIZE, payload, 32);
    }

    // --- Case 5 (matrix 6 / R14): committed journal replays but its clear
    // fails -> fatal, no writes possible; once the clear succeeds the replay
    // completes and the journaled record is served.
    {
        bool journaled = writeRepairJournal(5, 1, slot_image, sizeof(slot_image));
        enterFatalState("matrix case 5 (forced, to enter POST repair)");
        m_seams.fail_journal_erase_countdown = 1;
        bool post_failed = !performPowerOnSelfTest(true);
        bool still_fatal = m_fatal;
        memset(rec, 0x55, 32);
        bool append_refused = !appendRecord(rec, 32, 5);
        clearFaultSeams();
        bool recovered = performPowerOnSelfTest(true) && !m_fatal;
        bool replayed = (getRecordCount() == 1) &&
                        peekOldestRecord(readback, sizeof(readback), rlen, rmeta) &&
                        rlen == 32 && rmeta == 0x1234 && consumeOldestRecord();
        matrixCase("5 journal clear fails: fatal + refusal, then replay completes",
                   journaled && post_failed && still_fatal && append_refused &&
                   recovered && replayed, all);
        if (!matrixReset()) return false;
    }

    // --- Case 6 (matrix 7 / R14): journal data CRC mismatch AND discard
    // erase fails -> disposition unresolved -> fatal; once the erase works
    // the journal is discarded and the target stays untouched.
    {
        bool journaled = writeRepairJournal(5, 1, slot_image, sizeof(slot_image));
        uint32_t zeros = 0;  // corrupt the journal DATA (header stays valid)
        writeBytes(JOURNAL_SECTOR * SECTOR_SIZE + JOURNAL_HEADER_SIZE + 8, &zeros, 4);
        enterFatalState("matrix case 6 (forced, to enter POST repair)");
        m_seams.fail_journal_erase_countdown = 1;
        bool post_failed = !performPowerOnSelfTest(true);
        bool still_fatal = m_fatal;
        clearFaultSeams();
        bool recovered = performPowerOnSelfTest(true) && !m_fatal;
        SectorHeader target;
        bool target_untouched = readSectorHeader(5, target) && !headerValid(target);
        bool empty_after = (getRecordCount() == 0);
        matrixCase("6 journal CRC bad + clear fails: fatal until resolved, target untouched",
                   journaled && post_failed && still_fatal && recovered &&
                   target_untouched && empty_after, all);
        if (!matrixReset()) return false;
    }

    // --- Case 7 (matrix 8+9 / R15): two open sectors -> POST cannot pass,
    // recovery refuses to guess, accounting not reduced.
    {
        memset(rec, 0x70, 100);
        appendRecord(rec, 100, 7);
        flush();                                   // head open, 1 record
        uint32_t s2 = (m_head_sector + 1) % RING_SECTORS;
        eraseSector(s2);
        SectorHeader newer;                        // second open, HIGHER seq
        memset(&newer, 0xFF, sizeof(newer));
        newer.magic = SECTOR_MAGIC;
        newer.seq = m_head_seq + 1;
        newer.hdr_crc = calculateCRC32((const uint8_t*)&newer, 8);
        writeBytes(s2 * SECTOR_SIZE, &newer, sizeof(newer));
        // Recovery must refuse to guess between open sectors: no destructive
        // repair, fatal fallback, count untouched.
        bool scan_failed = !scanAndRecover(nullptr);
        bool went_fatal = m_fatal;
        bool count_not_reduced = (m_record_count == 1);
        // POST cannot pass while the structure is unresolved.
        bool post_failed = !performPowerOnSelfTest(true);
        matrixCase("7 multiple open sectors: recovery refuses, fatal, POST cannot pass",
                   scan_failed && went_fatal && count_not_reduced && post_failed, all);
        if (!matrixReset()) return false;
    }

    // --- Case 8 (matrix 10 / R18): deep-verification buffer unavailable ->
    // POST fails and the fatal state is NOT cleared.
    {
        enterFatalState("matrix case 8 (forced)");
        m_seams.fail_diag_alloc = true;
        bool post_failed = !performPowerOnSelfTest(true);
        bool still_fatal = m_fatal;
        m_seams.fail_diag_alloc = false;
        bool recovered = performPowerOnSelfTest(true) && !m_fatal;
        matrixCase("8 deep verify alloc fails: POST fails, fatal retained",
                   post_failed && still_fatal && recovered, all);
        if (!matrixReset()) return false;
    }

    // --- Case 9 (matrix 11 / R11): header program fails on a known-erased
    // sector -> erased bit cleared, the next open erases before programming.
    {
        eraseSector(m_head_sector);               // known erased this boot
        bool was_marked = mapGetErased(m_head_sector);
        m_seams.fail_program_countdown = 1;       // fail the header program
        memset(rec, 0x90, 32);
        appendRecord(rec, 32, 9);
        bool flush_failed = !flush();
        bool bit_cleared = !mapGetErased(m_head_sector);
        clearFaultSeams();
        bool flushed = flush();                   // must erase, then program
        SectorHeader stamped;
        bool header_ok = readSectorHeader(m_head_sector, stamped) && headerValid(stamped);
        bool served = peekOldestRecord(readback, sizeof(readback), rlen, rmeta) &&
                      rlen == 32 && rmeta == 9 && consumeOldestRecord();
        matrixCase("9 program fail after skip-erase: bit cleared, retry erases",
                   was_marked && flush_failed && bit_cleared && flushed &&
                   header_ok && served, all);
        if (!matrixReset()) return false;
    }

    // --- Case 10 (matrix 16 partial): each persistence step of shutdown
    // fails individually -> NOT SAFE, writes stay enabled, retry succeeds.
    {
        memset(rec, 0xA0, 32);
        appendRecord(rec, 32, 10);
        m_seams.fail_program_countdown = 1;       // flush fails
        bool not_safe_flush = !prepareForShutdown() && !m_writes_disabled;
        clearFaultSeams();
        bool interim_flush = flush();             // drain cleanly so the next
        m_seams.fail_nvs_save_countdown = 1;      // prepare hits ONLY the NVS save
        bool not_safe_nvs = !prepareForShutdown() && !m_writes_disabled;
        clearFaultSeams();
        bool safe_retry = prepareForShutdown() && m_writes_disabled;
        matrixCase("10 shutdown: flush/NVS failures NOT SAFE + retryable",
                   not_safe_flush && interim_flush && not_safe_nvs && safe_retry, all);
        if (!matrixReset()) return false;
    }

    // --- Case 11 (matrix 17 partial): pending assembly records survive a
    // SUCCESSFUL runtime recovery and remain durable afterwards.
    {
        memset(rec, 0xB0, 48);
        appendRecord(rec, 48, 11);
        appendRecord(rec, 48, 12);
        bool staged = (m_assembly_count == 2);
        enterFatalState("matrix case 11 (forced)");
        bool recovered = performPowerOnSelfTest(true) && !m_fatal;
        bool retained = (m_assembly_count == 2);  // recovery must not touch assembly
        bool flushed = flush() && m_record_count == 2;
        teardown();
        bool reinit_ok = init(m_fn_millis);
        bool durable = reinit_ok && getRecordCount() == 2;
        matrixCase("11 assembly retained across successful recovery, then durable",
                   staged && recovered && retained && flushed && durable, all);
        if (!matrixReset()) return false;
    }

    // --- Case 12 (matrix 18): the pre-existing regression suite still passes.
    {
        bool self_ok = performSelfTest();
        bool recovery_ok = performPowerLossRecoveryTest();
        bool stress_ok = performStressTest(50);
        matrixCase("12 existing self/recovery/stress suite still passes",
                   self_ok && recovery_ok && stress_ok, all);
        if (!matrixReset()) return false;
    }

    USB_SERIAL_PRINTF("=== FlashRingBuffer review-test matrix: %s ===\n",
                      all ? "ALL PASSED" : "FAILURES (see above)");
    return all;
}

#endif // TESTING_MODE

#endif // BUILD_INCLUDE_FLASHRINGBUFFER_PART3
