/**
 * @file FlashRingBuffer_part2.cpp
 * @brief Diagnostics, POST, repair and reset for FlashRingBuffer.
 *
 * Compiled by inclusion from FlashRingBuffer.cpp (project idiom); the guard
 * below makes the standalone PlatformIO compile of this file a no-op.
 *
 * IMPORTANT: nothing in this file ever erases or rewrites a sector holding
 * readable records. The functions that write at all are POST/repairCorruption()
 * (erase sectors whose headers are invalid, i.e. that hold no readable data),
 * clearAllData() and factoryReset() (explicit user actions), and
 * performPowerOnSelfTest() with auto_repair - whose repair path may invoke
 * repairCorruption(), the torn-head journal rescue, and the NVS cursor save
 * inside the recovery rescan.
 */

#ifdef BUILD_INCLUDE_FLASHRINGBUFFER_PART2

// ---------------------------------------------------------------------------
// Power-on self-test: read-only structural scan
// ---------------------------------------------------------------------------

bool FlashRingBuffer::countNonblankInvalidHeaderSectors(bool deep_blank_check,
                                                        uint32_t& anomalies) const {
    anomalies = 0;

    uint8_t* image = nullptr;
    if (deep_blank_check) {
#ifdef TESTING_MODE
        image = m_seams.fail_diag_alloc ? nullptr : (uint8_t*)malloc(SECTOR_SIZE);
#else
        image = (uint8_t*)malloc(SECTOR_SIZE);
#endif
        if (!image) {
            // Resource exhaustion IS a failed verification - never silently
            // degrade a requested deep proof to a shallow check.
            USB_SERIAL_PRINTLN("FlashRingBuffer::countNonblankInvalidHeaderSectors() - deep verification buffer unavailable, verification INCOMPLETE");
            return false;
        }
    }

    uint32_t count = 0;
    for (uint32_t sector = 0; sector < RING_SECTORS; sector++) {
        SectorHeader header;
        if (!readSectorHeader(sector, header)) {
            count++;  // unreadable counts as an anomaly, never as "clean"
            continue;
        }
        if (headerValid(header)) {
            continue;
        }
        bool blank = true;
        const uint8_t* raw = (const uint8_t*)&header;
        for (uint32_t i = 0; i < SECTOR_HEADER_SIZE; i++) {
            if (raw[i] != 0xFF) {
                blank = false;
                break;
            }
        }
        if (blank && deep_blank_check) {
            // Header erased but the body may not be (failed/partial erase):
            // only a full-sector read proves blankness.
            if (readBytes(sector * SECTOR_SIZE, image, SECTOR_SIZE) != ESP_OK ||
                !regionIsErased(image, 0, SECTOR_SIZE)) {
                blank = false;
            }
        }
        if (!blank) {
            count++;
        }
        if ((sector & (deep_blank_check ? 0x3F : 0xFF)) == 0) {
            delay(1);
        }
    }

    if (image) {
        free(image);
    }
    anomalies = count;
    return true;  // verification completed at the requested level
}

bool FlashRingBuffer::verifyStructuralState() const {
    // Recheck every structural condition of POST's initial health result so
    // post-repair verification cannot pass while the original complaint (for
    // example a second open sector, or an open sector that is not the RAM
    // head) is still present. RAM and flash must AGREE on the open sector.
    uint32_t open_sectors = 0;
    uint32_t open_sector = RING_SECTORS;
    for (uint32_t sector = 0; sector < RING_SECTORS; sector++) {
        SectorHeader header;
        if (!readSectorHeader(sector, header) || !headerValid(header)) {
            continue;
        }
        if (!headerClosed(header)) {
            open_sectors++;
            open_sector = sector;
        }
        if ((sector & 0xFF) == 0) {
            delay(1);
        }
    }

    bool open_agrees = m_head_open
        ? (open_sectors == 1 && open_sector == m_head_sector)
        : (open_sectors == 0);

    bool consistent =
        m_head_sector < RING_SECTORS &&
        m_tail_sector < RING_SECTORS &&
        m_tail_offset <= SECTOR_SIZE &&
        m_head_write_offset >= SECTOR_HEADER_SIZE &&
        m_head_write_offset <= SECTOR_SIZE &&
        open_agrees;
    if (!consistent) {
        USB_SERIAL_PRINTF("FlashRingBuffer::verifyStructuralState() - INCONSISTENT (%u open on flash, open sector %u, RAM head %u %s)\n",
                          open_sectors, open_sector, m_head_sector,
                          m_head_open ? "open" : "closed");
    }
    return consistent;
}

bool FlashRingBuffer::performPowerOnSelfTest(bool auto_repair) {
    USB_SERIAL_PRINTLN("=== FlashRingBuffer Power-On Self-Test ===");
    uint32_t start_time = m_fn_millis ? m_fn_millis() : 0;

    FlashDiagnostics diag;
    memset(&diag, 0, sizeof(diag));
    diag.total_sectors = RING_SECTORS;
    diag.partition_found = (m_partition != nullptr);

    if (!diag.partition_found) {
        // Critical exit: a store with no partition must not stay active -
        // otherwise appendRecord() keeps accepting records into the RAM
        // assembly, reports them persisted, and loses them at reboot.
        USB_SERIAL_PRINTLN("POST: CRITICAL - flash partition not found");
        if (m_initialized) {
            enterFatalState("POST: partition not found");
        }
        return false;
    }

    // Accessibility is proven by reading - no test writes near live data.
    SectorHeader probe;
    diag.partition_accessible = readSectorHeader(0, probe);
    if (!diag.partition_accessible) {
        USB_SERIAL_PRINTLN("POST: CRITICAL - flash partition not readable");
        if (m_initialized) {
            enterFatalState("POST: partition not readable");
        }
        return false;
    }

    uint32_t invalid_nonblank = 0;
    bool open_nonhead = false;

    for (uint32_t sector = 0; sector < RING_SECTORS; sector++) {
        SectorHeader header;
        if (!readSectorHeader(sector, header)) {
            invalid_nonblank++;
            continue;
        }

        if (!headerValid(header)) {
            // Distinguish truly blank sectors from nonblank sectors whose
            // header is invalid. The latter were excluded from the ring by
            // recovery and their records are unreadable - report them even
            // after a restart, when they are no longer in the runtime map.
            const uint8_t* raw = (const uint8_t*)&header;
            bool blank = true;
            for (uint32_t i = 0; i < SECTOR_HEADER_SIZE; i++) {
                if (raw[i] != 0xFF) { blank = false; break; }
            }
            if (!blank) {
                USB_SERIAL_PRINTF("POST: WARNING - sector %u contains data but its header is invalid (records unreadable, excluded from ring)\n",
                                  sector);
                invalid_nonblank++;
            } else if (mapGet(sector)) {
                USB_SERIAL_PRINTF("POST: WARNING - sector %u in map but blank\n", sector);
                invalid_nonblank++;
            } else {
                diag.erased_sectors++;
            }
            continue;
        }

        diag.in_use_sectors++;
        if (headerClosed(header)) {
            diag.closed_sectors++;
            diag.total_records += header.closed_count;
        } else {
            diag.open_sectors++;
            if (sector != m_head_sector) {
                // Records in a stray open sector cannot be ordered or
                // counted; this is a health failure, not just a warning.
                USB_SERIAL_PRINTF("POST: sector %u is open but is not the head sector - structural corruption\n", sector);
                open_nonhead = true;
            }
        }

        if ((sector & 0xFF) == 0) {
            delay(1);
        }
    }

    // Consistency between the scan and the RAM state, including agreement on
    // the open sector: if RAM says the head is open there must be exactly one
    // open sector on flash (the head itself); if RAM says closed, none.
    bool state_consistent =
        m_head_sector < RING_SECTORS &&
        m_tail_sector < RING_SECTORS &&
        m_tail_offset <= SECTOR_SIZE &&
        m_head_write_offset >= SECTOR_HEADER_SIZE &&
        m_head_write_offset <= SECTOR_SIZE &&
        !open_nonhead &&
        (m_head_open ? diag.open_sectors == 1 : diag.open_sectors == 0);

    if (m_fn_millis) {
        diag.diagnostic_duration_ms = m_fn_millis() - start_time;
    }

    USB_SERIAL_PRINTLN("=== POST Report ===");
    USB_SERIAL_PRINTF("Sectors: %u in use (%u closed, %u open), %u free\n",
                      diag.in_use_sectors, diag.closed_sectors,
                      diag.open_sectors, RING_SECTORS - diag.in_use_sectors);
    USB_SERIAL_PRINTF("Records in closed sectors: %u\n", diag.total_records);
    USB_SERIAL_PRINTF("Unread records (incl. open head): %u\n", m_record_count);
    USB_SERIAL_PRINTF("State consistent: %s\n", state_consistent ? "Yes" : "NO");
    USB_SERIAL_PRINTF("Header anomalies: %u\n", invalid_nonblank);
    USB_SERIAL_PRINTF("Duration: %u ms\n", diag.diagnostic_duration_ms);

    // A ring in the fatal state is never healthy on the initial pass - with
    // auto_repair this forces the full rescan + verification below, which is
    // the supported path back to a trusted state.
    bool healthy = state_consistent && invalid_nonblank == 0 && !m_fatal;

    if (!healthy && auto_repair) {
        bool repair_ok = true;
        bool repair_modified_flash = false;
        if (invalid_nonblank > 0) {
            // Sectors with programmed bytes but no valid header hold no
            // readable records (recovery cannot order them), so erasing is
            // the only repair - it returns them to the free pool.
            USB_SERIAL_PRINTLN("POST: erasing nonblank sectors with invalid headers");
            repair_ok = eraseCorruptSectors(repair_modified_flash);
        }
        USB_SERIAL_PRINTLN("POST: anomalies found - re-running recovery scan");
        // TRANSACTIONAL: scanAndRecover() builds a complete candidate state
        // and publishes it only on success - the live state is never
        // pre-cleared or partially overwritten here. On failure it either
        // retains the previous trusted state (flash unmodified) or enters
        // the fatal state itself; both outcomes are decided centrally there.
        FlashDiagnostics rediag;
        memset(&rediag, 0, sizeof(rediag));
        bool scan_ok = scanAndRecover(&rediag, repair_modified_flash);

        healthy = repair_ok && scan_ok;
        // Verify, not assume: recount anomalies (with the full-sector blank
        // proof - a failed erase can leave an erased header over a programmed
        // body) and recheck the structural conditions of the initial result.
        // An INCOMPLETE verification is a FAILED verification.
        if (healthy) {
            uint32_t remaining = 0;
            if (!countNonblankInvalidHeaderSectors(true, remaining)) {
                USB_SERIAL_PRINTLN("POST: deep verification could not be completed - result is FAILED");
                healthy = false;
            } else if (remaining > 0) {
                USB_SERIAL_PRINTF("POST: %u header anomalies REMAIN after repair\n", remaining);
                healthy = false;
            }
        }
        if (healthy && !verifyStructuralState()) {
            USB_SERIAL_PRINTLN("POST: structural inconsistency REMAINS after repair");
            healthy = false;
        }

        if (healthy && m_fatal) {
            // A fully verified repair re-earns trust (e.g. POST rerun after a
            // previous fatal diagnostic).
            USB_SERIAL_PRINTLN("POST: verified repair complete - clearing fatal state");
            m_fatal = false;
        }
        if (!healthy && !m_fatal) {
            // Auto-repair is a trust boundary. If any destructive repair,
            // recovery, or final proof fails, normal operation must not resume
            // on bookkeeping captured before the transaction began.
            enterFatalState("POST repair/recovery/verification failed");
        }
        USB_SERIAL_PRINTF("POST: repair + re-scan + verification %s\n", healthy ? "OK" : "FAILED");
    }

    USB_SERIAL_PRINTF("=== POST Complete: %s ===\n", healthy ? "PASSED" : "FAILED");
    return healthy;
}

// ---------------------------------------------------------------------------
// Deep validation: CRC-check every record of every in-use sector (read-only)
// ---------------------------------------------------------------------------

bool FlashRingBuffer::performDeepSectorValidation() {
    USB_SERIAL_PRINTLN("=== FlashRingBuffer Deep Sector Validation (read-only) ===");

    if (!m_initialized) {
        USB_SERIAL_PRINTLN("Deep validation: not initialized");
        return false;
    }

    uint32_t start_time = m_fn_millis ? m_fn_millis() : 0;

#ifdef TESTING_MODE
    uint8_t* image = m_seams.fail_diag_alloc ? nullptr : (uint8_t*)malloc(SECTOR_SIZE);
#else
    uint8_t* image = (uint8_t*)malloc(SECTOR_SIZE);
#endif
    if (!image) {
        USB_SERIAL_PRINTLN("Deep validation: buffer allocation failed");
        return false;
    }

    bool passed = true;
    uint32_t sectors_checked = 0, sectors_with_issues = 0;
    uint32_t records_validated = 0, corrupted_records = 0;

    for (uint32_t sector = 0; sector < RING_SECTORS; sector++) {
        SectorHeader header;
        if (!readSectorHeader(sector, header)) {
            sectors_with_issues++;
            passed = false;
            continue;
        }
        if (!headerValid(header)) {
            // Not in use - but a nonblank sector with an invalid header holds
            // unreadable (excluded) records and must be reported.
            if (readBytes(sector * SECTOR_SIZE, image, SECTOR_SIZE) == ESP_OK &&
                !regionIsErased(image, 0, SECTOR_SIZE)) {
                USB_SERIAL_PRINTF("Deep validation: sector %u contains data but its header is invalid (records unreadable)\n",
                                  sector);
                sectors_with_issues++;
                passed = false;
            }
            continue;
        }

        sectors_checked++;

        if (readBytes(sector * SECTOR_SIZE, image, SECTOR_SIZE) != ESP_OK) {
            sectors_with_issues++;
            passed = false;
            continue;
        }

        bool closed = headerClosed(header);
        uint32_t expected_end = closed ? (SECTOR_HEADER_SIZE + header.closed_used)
                                       : SECTOR_SIZE;
        uint32_t count = 0;
        uint32_t extent = scanRecordExtent(image, expected_end, &count);

        records_validated += count;

        if (closed) {
            if (extent != expected_end || count != header.closed_count) {
                USB_SERIAL_PRINTF("Deep validation: sector %u closed marker says %u bytes/%u records, scan found %u/%u\n",
                                  sector, header.closed_used, header.closed_count,
                                  extent - SECTOR_HEADER_SIZE, count);
                corrupted_records += (header.closed_count > count)
                                         ? (header.closed_count - count) : 0;
                sectors_with_issues++;
                passed = false;
            }
            // Sentinel check: the fill between data end and sector end must
            // contain no erased (0xFF) words on a closed sector.
            for (uint32_t i = expected_end; i + 1 < SECTOR_SIZE; i += 2) {
                if (image[i] == 0xFF && image[i + 1] == 0xFF) {
                    USB_SERIAL_PRINTF("Deep validation: sector %u closed but sentinel fill missing at %u\n",
                                      sector, i);
                    sectors_with_issues++;
                    passed = false;
                    break;
                }
            }
        } else {
            // Open sector: everything beyond the last valid record must be erased.
            if (!regionIsErased(image, extent, SECTOR_SIZE)) {
                USB_SERIAL_PRINTF("Deep validation: sector %u open with programmed bytes after record end %u (torn append)\n",
                                  sector, extent);
                sectors_with_issues++;
                passed = false;
            }
        }

        if ((sector & 0x3F) == 0) {
            delay(1);
        }
    }

    free(image);

    uint32_t duration = m_fn_millis ? (m_fn_millis() - start_time) : 0;
    USB_SERIAL_PRINTF("Deep validation: %u sectors checked, %u with issues, %u records OK, %u corrupt (%u ms)\n",
                      sectors_checked, sectors_with_issues, records_validated,
                      corrupted_records, duration);
    USB_SERIAL_PRINTF("=== Deep Sector Validation: %s ===\n", passed ? "PASSED" : "FAILED");
    return passed;
}

bool FlashRingBuffer::performExtendedDiagnostics() {
    // Alias kept for API compatibility - the deep scan is the extended check.
    return performDeepSectorValidation();
}

// ---------------------------------------------------------------------------
// Repair and reset
// ---------------------------------------------------------------------------

bool FlashRingBuffer::repairCorruption() {
    // Public repair has the same all-or-fatal contract as POST. Keeping the
    // destructive helper private prevents callers from erasing sectors
    // without rebuilding and verifying the complete runtime state.
    return performPowerOnSelfTest(true);
}

bool FlashRingBuffer::eraseCorruptSectors(bool& flash_modified) {
    USB_SERIAL_PRINTLN("FlashRingBuffer::eraseCorruptSectors() - erasing unreadable sectors");

    flash_modified = false;

    if (!m_initialized) {
        return false;
    }

#ifdef TESTING_MODE
    uint8_t* image = m_seams.fail_diag_alloc ? nullptr : (uint8_t*)malloc(SECTOR_SIZE);
#else
    uint8_t* image = (uint8_t*)malloc(SECTOR_SIZE);
#endif
    if (!image) {
        return false;
    }

    uint32_t erased = 0;
    uint32_t unreadable = 0;
    uint32_t erase_failures = 0;

    // The repair journal sector is deliberately outside this loop.
    for (uint32_t sector = 0; sector < RING_SECTORS; sector++) {
        SectorHeader header;
        if (!readSectorHeader(sector, header)) {
            // A transient read failure is not proof the sector holds garbage
            // - never erase on failed evidence.
            USB_SERIAL_PRINTF("FlashRingBuffer::eraseCorruptSectors() - sector %u unreadable, NOT erasing\n", sector);
            unreadable++;
            continue;
        }

        if (headerValid(header)) {
            continue;  // holds (potentially) valid data - never touched here
        }

        // Invalid header: erase only if the sector is confirmed not blank.
        if (readBytes(sector * SECTOR_SIZE, image, SECTOR_SIZE) != ESP_OK) {
            USB_SERIAL_PRINTF("FlashRingBuffer::eraseCorruptSectors() - sector %u body unreadable, NOT erasing\n", sector);
            unreadable++;
            continue;
        }
        if (regionIsErased(image, 0, SECTOR_SIZE)) {
            continue;  // already blank
        }

        USB_SERIAL_PRINTF("FlashRingBuffer::eraseCorruptSectors() - erasing garbage sector %u\n", sector);
        if (eraseSector(sector) == ESP_OK) {
            erased++;
            flash_modified = true;
            mapSet(sector, false);
        } else {
            erase_failures++;  // leave the map untouched - nothing changed
        }

        if ((sector & 0x3F) == 0) {
            delay(1);
        }
    }

    free(image);
    USB_SERIAL_PRINTF("FlashRingBuffer::eraseCorruptSectors() - %u sectors erased, %u erase failures, %u unreadable\n",
                      erased, erase_failures, unreadable);
    return erase_failures == 0 && unreadable == 0;
}

bool FlashRingBuffer::clearAllData() {
    USB_SERIAL_PRINTLN("FlashRingBuffer::clearAllData() - erasing entire partition");

    if (!m_partition) {
        return false;
    }

    const uint32_t CHUNK_SIZE = 64 * 1024;
    uint32_t erased_bytes = 0;

    while (erased_bytes < RING_BUFFER_SIZE) {
        uint32_t chunk = (RING_BUFFER_SIZE - erased_bytes > CHUNK_SIZE)
                             ? CHUNK_SIZE : (RING_BUFFER_SIZE - erased_bytes);
        esp_err_t err = esp_partition_erase_range(m_partition, erased_bytes, chunk);
        if (err != ESP_OK) {
            USB_SERIAL_PRINTF("FlashRingBuffer::clearAllData() - erase failed at 0x%x: %s\n",
                              erased_bytes, esp_err_to_name(err));
            return false;
        }
        erased_bytes += chunk;
        delay(1);  // watchdog yield between 64KB chunks
    }

    uint32_t preserved_write_count = m_write_count;
    uint32_t preserved_next_seq = m_next_seq;  // keep seq monotonic across clears
    resetRuntimeState();
    m_write_count = preserved_write_count;
    m_next_seq = preserved_next_seq;
    // The whole partition is now blank - no sector needs a pre-open erase.
    memset(m_erased_map, 0xFF, sizeof(m_erased_map));
    savePersistedState();

    USB_SERIAL_PRINTF("FlashRingBuffer::clearAllData() - erased %u bytes\n", erased_bytes);
    return true;
}

bool FlashRingBuffer::factoryReset() {
    USB_SERIAL_PRINTLN("FlashRingBuffer::factoryReset() - full reset");

    if (!m_partition) {
        return false;
    }

    m_preferences.clear();

    if (!clearAllData()) {
        return false;
    }

    // clearAllData preserved counters; a factory reset zeroes them too.
    resetRuntimeState();
    savePersistedState();
    m_writes_disabled = false;

    USB_SERIAL_PRINTLN("FlashRingBuffer::factoryReset() - complete");
    return true;
}

#define BUILD_INCLUDE_FLASHRINGBUFFER_PART3

#include "FlashRingBuffer_part3.cpp"

#endif // BUILD_INCLUDE_FLASHRINGBUFFER_PART2
