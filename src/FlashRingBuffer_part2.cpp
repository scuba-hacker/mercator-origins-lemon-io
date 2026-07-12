/**
 * @file FlashRingBuffer_part2.cpp
 * @brief Diagnostics, POST, repair and reset for FlashRingBuffer.
 *
 * Compiled by inclusion from FlashRingBuffer.cpp (project idiom); the guard
 * below makes the standalone PlatformIO compile of this file a no-op.
 *
 * IMPORTANT: every routine in this file is READ-ONLY against stored telemetry.
 * The only functions that erase anything are repairCorruption() (erases
 * sectors whose headers are invalid, i.e. that hold no readable data),
 * clearAllData() and factoryReset() - all explicit user actions.
 */

#ifdef BUILD_INCLUDE_FLASHRINGBUFFER_PART2

// ---------------------------------------------------------------------------
// Power-on self-test: read-only structural scan
// ---------------------------------------------------------------------------

bool FlashRingBuffer::performPowerOnSelfTest(bool auto_repair) {
    USB_SERIAL_PRINTLN("=== FlashRingBuffer Power-On Self-Test (read-only) ===");
    uint32_t start_time = m_fn_millis ? m_fn_millis() : 0;

    FlashDiagnostics diag;
    memset(&diag, 0, sizeof(diag));
    diag.total_sectors = TOTAL_SECTORS;
    diag.partition_found = (m_partition != nullptr);

    if (!diag.partition_found) {
        USB_SERIAL_PRINTLN("POST: CRITICAL - flash partition not found");
        return false;
    }

    // Accessibility is proven by reading - no test writes near live data.
    SectorHeader probe;
    diag.partition_accessible = readSectorHeader(0, probe);
    if (!diag.partition_accessible) {
        USB_SERIAL_PRINTLN("POST: CRITICAL - flash partition not readable");
        return false;
    }

    uint32_t invalid_nonblank = 0;

    for (uint32_t sector = 0; sector < TOTAL_SECTORS; sector++) {
        SectorHeader header;
        if (!readSectorHeader(sector, header)) {
            invalid_nonblank++;
            continue;
        }

        if (!headerValid(header)) {
            diag.erased_sectors++;
            // Distinguish blank from garbage only for in-map anomalies; a
            // garbage sector outside the ring gets erased when the head
            // reaches it, so it is not an operational problem.
            if (mapGet(sector)) {
                USB_SERIAL_PRINTF("POST: WARNING - sector %u in map but header invalid\n", sector);
                invalid_nonblank++;
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
                USB_SERIAL_PRINTF("POST: WARNING - sector %u is open but is not the head sector\n", sector);
            }
        }

        if ((sector & 0xFF) == 0) {
            delay(1);
        }
    }

    // Consistency between the scan and the RAM state.
    bool state_consistent =
        m_head_sector < TOTAL_SECTORS &&
        m_tail_sector < TOTAL_SECTORS &&
        m_tail_offset <= SECTOR_SIZE &&
        m_head_write_offset >= SECTOR_HEADER_SIZE &&
        m_head_write_offset <= SECTOR_SIZE &&
        diag.open_sectors <= 1;

    if (m_fn_millis) {
        diag.diagnostic_duration_ms = m_fn_millis() - start_time;
    }

    USB_SERIAL_PRINTLN("=== POST Report ===");
    USB_SERIAL_PRINTF("Sectors: %u in use (%u closed, %u open), %u free\n",
                      diag.in_use_sectors, diag.closed_sectors,
                      diag.open_sectors, TOTAL_SECTORS - diag.in_use_sectors);
    USB_SERIAL_PRINTF("Records in closed sectors: %u\n", diag.total_records);
    USB_SERIAL_PRINTF("Unread records (incl. open head): %u\n", m_record_count);
    USB_SERIAL_PRINTF("State consistent: %s\n", state_consistent ? "Yes" : "NO");
    USB_SERIAL_PRINTF("Header anomalies: %u\n", invalid_nonblank);
    USB_SERIAL_PRINTF("Duration: %u ms\n", diag.diagnostic_duration_ms);

    bool healthy = state_consistent && invalid_nonblank == 0;

    if (!healthy && auto_repair) {
        USB_SERIAL_PRINTLN("POST: anomalies found - re-running recovery scan");
        // Recovery scan is read-only (except torn-head rescue) and rebuilds
        // all RAM state from flash, which resolves map/state drift.
        memset(m_sector_map, 0, sizeof(m_sector_map));
        uint32_t saved_write_count = m_write_count;
        m_record_count = 0;
        m_tail_cache_valid = false;
        invalidatePeek();
        FlashDiagnostics rediag;
        memset(&rediag, 0, sizeof(rediag));
        healthy = scanAndRecover(&rediag);
        m_write_count = saved_write_count;
        USB_SERIAL_PRINTF("POST: recovery re-scan %s\n", healthy ? "OK" : "FAILED");
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

    uint8_t* image = (uint8_t*)malloc(SECTOR_SIZE);
    if (!image) {
        USB_SERIAL_PRINTLN("Deep validation: buffer allocation failed");
        return false;
    }

    bool passed = true;
    uint32_t sectors_checked = 0, sectors_with_issues = 0;
    uint32_t records_validated = 0, corrupted_records = 0;

    for (uint32_t sector = 0; sector < TOTAL_SECTORS; sector++) {
        SectorHeader header;
        if (!readSectorHeader(sector, header) || !headerValid(header)) {
            continue;  // not in use
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
    USB_SERIAL_PRINTLN("FlashRingBuffer::repairCorruption() - erasing unreadable sectors");

    if (!m_initialized) {
        return false;
    }

    uint8_t* image = (uint8_t*)malloc(SECTOR_SIZE);
    if (!image) {
        return false;
    }

    uint32_t erased = 0;

    for (uint32_t sector = 0; sector < TOTAL_SECTORS; sector++) {
        SectorHeader header;
        bool readable = readSectorHeader(sector, header);

        if (readable && headerValid(header)) {
            continue;  // holds (potentially) valid data - never touched here
        }

        // Invalid header: erase only if the sector is not already blank.
        bool blank = false;
        if (readable && readBytes(sector * SECTOR_SIZE, image, SECTOR_SIZE) == ESP_OK) {
            blank = regionIsErased(image, 0, SECTOR_SIZE);
        }

        if (!blank) {
            USB_SERIAL_PRINTF("FlashRingBuffer::repairCorruption() - erasing garbage sector %u\n", sector);
            if (eraseSector(sector) == ESP_OK) {
                erased++;
            }
            mapSet(sector, false);
        }

        if ((sector & 0x3F) == 0) {
            delay(1);
        }
    }

    free(image);
    USB_SERIAL_PRINTF("FlashRingBuffer::repairCorruption() - complete, %u sectors erased\n", erased);
    return true;
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
