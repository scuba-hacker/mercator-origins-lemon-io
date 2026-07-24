/**
 * @file FlashRingBuffer.cpp
 * @brief Core implementation: append path, peek/consume path, recovery, state.
 *
 * See FlashRingBuffer.h for the on-flash format and the design rules.
 *
 * The golden rules enforced here:
 *  - one erase per sector per ring cycle (when the sector is opened as head)
 *  - programming only ever targets erased (0xFF) bytes
 *  - records leave flash only after consumeOldestRecord() (post-MQTT-ack)
 *  - boot recovery is read-only unless a torn append has to be rescued
 */

#include "FlashRingBuffer.h"
#include "SerialConfig.h"
#include <Arduino.h>
#include <cstring>

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------

static bool regionIsErased(const uint8_t* data, uint32_t from, uint32_t to) {
    for (uint32_t i = from; i < to; i++) {
        if (data[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

FlashRingBuffer::FlashRingBuffer() :
    m_partition(nullptr),
    m_initialized(false),
    m_writes_disabled(false),
    m_fn_millis(nullptr),
    m_assembly(nullptr),
    m_tail_cache(nullptr) {
    resetRuntimeState();
#ifdef TESTING_MODE
    clearFaultSeams();
#endif
}

FlashRingBuffer::~FlashRingBuffer() {
    teardown();
}

void FlashRingBuffer::resetRuntimeState() {
    m_head_sector = 0;
    m_head_seq = 0;
    m_head_open = false;
    m_head_write_offset = SECTOR_HEADER_SIZE;
    m_head_used = 0;
    m_head_record_count = 0;
    m_ring_virgin = true;

    m_tail_sector = 0;
    m_tail_offset = SECTOR_HEADER_SIZE;
    m_tail_consumed = 0;
    m_tail_seq = 0;

    m_record_count = 0;
    m_next_seq = 1;
    m_write_count = 0;

    m_assembly_used = 0;
    m_assembly_count = 0;
    m_first_unflushed_ms = 0;
    m_max_unflushed_ms = DEFAULT_MAX_UNFLUSHED_MS;

    m_fatal = false;

    m_tail_cache_sector = 0;
    m_tail_cache_extent = 0;
    m_tail_cache_valid = false;

    m_peek_valid = false;
    m_peek_len = 0;
    m_peek_meta = 0;
    m_peek_offset = 0;

    memset(m_sector_map, 0, sizeof(m_sector_map));
    memset(m_erased_map, 0, sizeof(m_erased_map));
}

// ---------------------------------------------------------------------------
// Init / teardown
// ---------------------------------------------------------------------------

bool FlashRingBuffer::init(long unsigned int (*fn_millis)(void)) {
    USB_SERIAL_PRINTLN("FlashRingBuffer::init() - starting");

    if (m_initialized) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::init() - already initialized");
        return true;
    }

    m_fn_millis = fn_millis;
    m_writes_disabled = false;

    m_assembly = (uint8_t*)malloc(SECTOR_SIZE);
    m_tail_cache = (uint8_t*)malloc(SECTOR_SIZE);
    if (!m_assembly || !m_tail_cache) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::init() - buffer allocation failed");
        teardown();
        return false;
    }

    if (!findPartition()) {
        teardown();
        return false;
    }

    if (!m_preferences.begin("flashring", false)) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::init() - NVS namespace open failed");
        teardown();
        return false;
    }

    // Discard state written by the pre-2026 on-flash format (incompatible).
    if (m_preferences.isKey("state")) {
        m_preferences.remove("state");
        USB_SERIAL_PRINTLN("FlashRingBuffer::init() - removed legacy v1 NVS state");
    }

    resetRuntimeState();

    FlashDiagnostics diag;
    memset(&diag, 0, sizeof(diag));
    if (!scanAndRecover(&diag)) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::init() - recovery scan failed");
        teardown();
        return false;
    }

    m_initialized = true;

    USB_SERIAL_PRINTF("FlashRingBuffer::init() - ready: %u records, head sector %u (seq %u, %s), tail sector %u @ %u\n",
                      m_record_count, m_head_sector, m_head_seq,
                      m_head_open ? "open" : "closed",
                      m_tail_sector, m_tail_offset);
    if (diag.torn_head_repairs) {
        USB_SERIAL_PRINTF("FlashRingBuffer::init() - rescued a torn head sector (%u repair)\n",
                          diag.torn_head_repairs);
    }
    return true;
}

void FlashRingBuffer::teardown() {
    if (m_initialized && !m_fatal) {
        // Persist anything still pending so a deliberate teardown loses
        // nothing. Skipped entirely in the fatal state - the bookkeeping is
        // untrusted and saving its cursor to NVS would poison the next boot.
        if (m_assembly_used > 0 && !m_writes_disabled) {
            flush();
        }
        savePersistedState();
    }

    m_preferences.end();

    if (m_assembly) {
        free(m_assembly);
        m_assembly = nullptr;
    }
    if (m_tail_cache) {
        free(m_tail_cache);
        m_tail_cache = nullptr;
    }

    m_partition = nullptr;
    m_initialized = false;
    resetRuntimeState();
}

// ---------------------------------------------------------------------------
// Low-level flash access
// ---------------------------------------------------------------------------

bool FlashRingBuffer::findPartition() {
    m_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                           (esp_partition_subtype_t)0x40, "flashbuf");
    if (!m_partition) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::findPartition() - partition 'flashbuf' not found");
        return false;
    }

    USB_SERIAL_PRINTF("FlashRingBuffer::findPartition() - found: size=%u bytes @ 0x%x\n",
                      m_partition->size, m_partition->address);

    if (m_partition->size < RING_BUFFER_SIZE) {
        USB_SERIAL_PRINTF("FlashRingBuffer::findPartition() - partition too small: %u < %u\n",
                          m_partition->size, RING_BUFFER_SIZE);
        return false;
    }
    return true;
}

esp_err_t FlashRingBuffer::eraseSector(uint32_t sector_index) {
    if (sector_index >= TOTAL_SECTORS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!m_partition) {
        // Central partition-loss policy: a store that cannot erase must not
        // keep accepting or serving telemetry.
        if (m_initialized) {
            enterFatalState("partition unavailable (erase)");
        }
        return ESP_ERR_INVALID_ARG;
    }
#ifdef TESTING_MODE
    if (sector_index == JOURNAL_SECTOR
            ? seamFire(m_seams.fail_journal_erase_countdown)
            : seamFire(m_seams.fail_erase_countdown)) {
        USB_SERIAL_PRINTF("SEAM: injected erase failure, sector %u\n", sector_index);
        return ESP_FAIL;
    }
#endif
    esp_err_t err = esp_partition_erase_range(m_partition, sector_index * SECTOR_SIZE, SECTOR_SIZE);
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::eraseSector() - sector %u failed: %s\n",
                          sector_index, esp_err_to_name(err));
        if (m_initialized) {
            enterFatalState("partition erase failed");
        }
    } else {
        mapSetErased(sector_index, true);
    }
    return err;
}

esp_err_t FlashRingBuffer::writeBytes(uint32_t offset, const void* src, size_t len) {
    if (offset + len > RING_BUFFER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!m_partition) {
        if (m_initialized) {
            enterFatalState("partition unavailable (program)");
        }
        return ESP_ERR_INVALID_ARG;
    }
    // Clear the known-erased bit BEFORE programming: a failed write may still
    // have flipped bits, so failure does not prove the sector is still blank.
    // (Writes never span sectors in this design.)
    mapSetErased(offset / SECTOR_SIZE, false);
#ifdef TESTING_MODE
    if (seamFire(m_seams.fail_program_countdown)) {
        USB_SERIAL_PRINTF("SEAM: injected program failure @ 0x%x\n", offset);
        return ESP_FAIL;
    }
#endif
    esp_err_t err = esp_partition_write(m_partition, offset, src, len);
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::writeBytes() - offset 0x%x len %u failed: %s\n",
                          offset, (unsigned)len, esp_err_to_name(err));
        if (m_initialized) {
            enterFatalState("partition program failed");
        }
    }
    return err;
}

esp_err_t FlashRingBuffer::readBytes(uint32_t offset, void* dst, size_t len) const {
    if (offset + len > RING_BUFFER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!m_partition) {
        if (m_initialized) {
            const_cast<FlashRingBuffer*>(this)->enterFatalState("partition unavailable (read)");
        }
        return ESP_ERR_INVALID_ARG;
    }
#ifdef TESTING_MODE
    if (seamFire(m_seams.fail_read_countdown)) {
        USB_SERIAL_PRINTF("SEAM: injected read failure @ 0x%x\n", offset);
        return ESP_FAIL;
    }
#endif
    esp_err_t err = esp_partition_read(m_partition, offset, dst, len);
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::readBytes() - offset 0x%x len %u failed: %s\n",
                          offset, (unsigned)len, esp_err_to_name(err));
        if (m_initialized) {
            const_cast<FlashRingBuffer*>(this)->enterFatalState("partition read failed");
        }
    }
    return err;
}

// ---------------------------------------------------------------------------
// CRC
// ---------------------------------------------------------------------------

uint16_t FlashRingBuffer::crc16Update(uint16_t crc, const uint8_t* data, size_t length) const {
    while (length--) {
        crc ^= (uint16_t)(*data++) << 8;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t FlashRingBuffer::recordCRC(uint16_t meta, const uint8_t* payload, uint16_t length) const {
    uint8_t meta_bytes[2] = { (uint8_t)(meta & 0xFF), (uint8_t)(meta >> 8) };
    uint16_t crc = crc16Update(0xFFFF, meta_bytes, 2);
    return crc16Update(crc, payload, length);
}

uint32_t FlashRingBuffer::calculateCRC32(const uint8_t* data, size_t length) const {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : (crc >> 1);
        }
    }
    return ~crc;
}

// ---------------------------------------------------------------------------
// Sector map
// ---------------------------------------------------------------------------

void FlashRingBuffer::mapSet(uint32_t sector, bool in_use) {
    if (sector >= TOTAL_SECTORS) return;
    if (in_use) {
        m_sector_map[sector >> 3] |= (uint8_t)(1u << (sector & 7));
    } else {
        m_sector_map[sector >> 3] &= (uint8_t)~(1u << (sector & 7));
    }
}

bool FlashRingBuffer::mapGet(uint32_t sector) const {
    if (sector >= TOTAL_SECTORS) return false;
    return (m_sector_map[sector >> 3] >> (sector & 7)) & 1u;
}

uint32_t FlashRingBuffer::mapCountInUse() const {
    uint32_t count = 0;
    for (uint32_t i = 0; i < sizeof(m_sector_map); i++) {
        uint8_t b = m_sector_map[i];
        while (b) {
            count += b & 1u;
            b >>= 1;
        }
    }
    return count;
}

uint32_t FlashRingBuffer::mapNextInUse(uint32_t from) const {
    for (uint32_t step = 1; step <= RING_SECTORS; step++) {
        uint32_t sector = (from + step) % RING_SECTORS;
        if (mapGet(sector)) {
            return sector;
        }
    }
    return RING_SECTORS;
}

void FlashRingBuffer::enterFatalState(const char* context) {
    if (!m_fatal) {
        USB_SERIAL_PRINTF("FlashRingBuffer::enterFatalState() - %s: bookkeeping untrusted, "
                          "all flash data operations disabled (recover via verified POST repair, factory reset, or reboot)\n",
                          context);
    }
    m_fatal = true;
    m_tail_cache_valid = false;
    invalidatePeek();
}

void FlashRingBuffer::mapSetErased(uint32_t sector, bool erased) {
    if (sector >= TOTAL_SECTORS) return;
    if (erased) {
        m_erased_map[sector >> 3] |= (uint8_t)(1u << (sector & 7));
    } else {
        m_erased_map[sector >> 3] &= (uint8_t)~(1u << (sector & 7));
    }
}

bool FlashRingBuffer::mapGetErased(uint32_t sector) const {
    if (sector >= TOTAL_SECTORS) return false;
    return (m_erased_map[sector >> 3] >> (sector & 7)) & 1u;
}

// ---------------------------------------------------------------------------
// Header helpers
// ---------------------------------------------------------------------------

bool FlashRingBuffer::readSectorHeader(uint32_t sector, SectorHeader& header) const {
    return readBytes(sector * SECTOR_SIZE, &header, sizeof(header)) == ESP_OK;
}

bool FlashRingBuffer::headerValid(const SectorHeader& header) const {
    if (header.magic != SECTOR_MAGIC) {
        return false;
    }
    return calculateCRC32((const uint8_t*)&header, 8) == header.hdr_crc;
}

bool FlashRingBuffer::headerClosed(const SectorHeader& header) const {
    if (header.closed_used == 0xFFFF && header.closed_count == 0xFFFF &&
        header.closed_crc == 0xFFFFFFFF) {
        return false;  // still open
    }
    uint8_t close_fields[4] = {
        (uint8_t)(header.closed_used & 0xFF),  (uint8_t)(header.closed_used >> 8),
        (uint8_t)(header.closed_count & 0xFF), (uint8_t)(header.closed_count >> 8)
    };
    if (calculateCRC32(close_fields, 4) != header.closed_crc) {
        return false;  // torn close marker - treat as open, records get re-scanned
    }
    return header.closed_used <= USABLE_SECTOR_SIZE &&
           header.closed_count <= USABLE_SECTOR_SIZE / MIN_SLOT_SIZE;
}

// ---------------------------------------------------------------------------
// Write path: append -> assembly buffer -> flush -> head sector
// ---------------------------------------------------------------------------

bool FlashRingBuffer::appendRecord(const uint8_t* payload, uint16_t length, uint16_t meta) {
    if (!m_initialized || !payload) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::appendRecord() - not initialized or null payload");
        return false;
    }
    if (m_writes_disabled) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::appendRecord() - writes disabled (shutdown prepared)");
        return false;
    }
    if (m_fatal) {
        return false;  // markFatal() already logged; caller falls back to PSRAM
    }
    if (length < MIN_MESSAGE_SIZE || length > MAX_MESSAGE_SIZE) {
        USB_SERIAL_PRINTF("FlashRingBuffer::appendRecord() - invalid length %u (must be %u-%u)\n",
                          length, MIN_MESSAGE_SIZE, MAX_MESSAGE_SIZE);
        return false;
    }

    // Age-based flush bounds RAM data loss on an unplanned power cut. Run it
    // BEFORE accepting the new record so a flash failure propagates to the
    // caller (which falls back to PSRAM) instead of being swallowed by an
    // unconditional success return.
    uint32_t now = m_fn_millis ? m_fn_millis() : 0;
    if (m_first_unflushed_ms != 0 && now &&
        (now - m_first_unflushed_ms) >= m_max_unflushed_ms) {
        if (!flush()) {
            USB_SERIAL_PRINTLN("FlashRingBuffer::appendRecord() - age-triggered flush FAILED");
            return false;
        }
    }

    const uint32_t slot = alignSlot(RECORD_HEADER_SIZE + length);

    // Space available for assembly data in the current (or next) head sector.
    uint32_t sector_remaining = m_head_open ? (SECTOR_SIZE - m_head_write_offset)
                                            : USABLE_SECTOR_SIZE;

    if (m_assembly_used + slot > sector_remaining) {
        // Current sector cannot take the pending data plus this record:
        // flush what we have, close the sector, start accumulating for the next.
        if (!flush()) {
            USB_SERIAL_PRINTLN("FlashRingBuffer::appendRecord() - flush failed");
            return false;
        }
        if (m_head_open && (SECTOR_SIZE - m_head_write_offset) < slot) {
            if (!closeHeadSector()) {
                return false;
            }
        }
    }

    RecordHeader header;
    header.len = length;
    header.crc16 = recordCRC(meta, payload, length);
    header.meta = meta;
    header.reserved = 0;

    memcpy(m_assembly + m_assembly_used, &header, RECORD_HEADER_SIZE);
    memcpy(m_assembly + m_assembly_used + RECORD_HEADER_SIZE, payload, length);
    // zero the alignment padding so flushes program deterministic bytes
    uint32_t pad_from = m_assembly_used + RECORD_HEADER_SIZE + length;
    uint32_t pad_to = m_assembly_used + slot;
    if (pad_to > pad_from) {
        memset(m_assembly + pad_from, 0, pad_to - pad_from);
    }

    m_assembly_used += slot;
    m_assembly_count++;

    if (m_first_unflushed_ms == 0) {
        m_first_unflushed_ms = now ? now : 1;
    }

    return true;
}

bool FlashRingBuffer::flush() {
    if (!m_initialized || m_fatal) {
        return false;
    }
    if (m_assembly_used == 0) {
        return true;  // nothing pending - not an error
    }

    if (!m_head_open) {
        if (!openNextHeadSector()) {
            return false;
        }
    }

    // Defensive: if the assembly somehow exceeds the open sector's remaining
    // space (should be prevented in appendRecord), rotate to a fresh sector.
    if (m_head_write_offset + m_assembly_used > SECTOR_SIZE) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::flush() - assembly exceeds sector space, rotating");
        if (!closeHeadSector() || !openNextHeadSector()) {
            return false;
        }
    }

    uint32_t offset = m_head_sector * SECTOR_SIZE + m_head_write_offset;
    if (writeBytes(offset, m_assembly, m_assembly_used) != ESP_OK) {
        return false;
    }
    m_write_count++;

    USB_SERIAL_PRINTF("FlashRingBuffer::flush() - %u bytes / %u records -> sector %u @ %u\n",
                      m_assembly_used, m_assembly_count, m_head_sector, m_head_write_offset);

    m_head_write_offset += m_assembly_used;
    m_head_used += m_assembly_used;
    m_head_record_count += m_assembly_count;
    m_record_count += m_assembly_count;

    m_assembly_used = 0;
    m_assembly_count = 0;
    m_first_unflushed_ms = 0;

    // The tail cache image of this sector is now stale (peek re-loads it).
    if (m_tail_cache_valid && m_tail_cache_sector == m_head_sector) {
        m_tail_cache_valid = false;
    }

    // No room for even a minimum record? Seal the sector now.
    if ((SECTOR_SIZE - m_head_write_offset) < MIN_SLOT_SIZE) {
        if (!closeHeadSector()) {
            return false;
        }
    }

    return true;
}

bool FlashRingBuffer::openNextHeadSector() {
    if (m_head_open) {
        return true;
    }

    uint32_t next;
    if (m_ring_virgin && !mapGet(m_head_sector)) {
        next = m_head_sector;  // very first use: claim the current position
    } else {
        next = (m_head_sector + 1) % RING_SECTORS;
    }

    if (mapGet(next)) {
        // Ring is full - the next sector is the oldest data. Drop it (spec:
        // prefer continuous capture over blocking).
        USB_SERIAL_PRINTF("FlashRingBuffer::openNextHeadSector() - ring full, dropping oldest sector %u\n", next);
        if (next != m_tail_sector) {
            USB_SERIAL_PRINTF("FlashRingBuffer::openNextHeadSector() - WARNING: full-ring successor %u != tail %u\n",
                              next, m_tail_sector);
            m_tail_sector = next;  // resynchronise before dropping
            m_tail_offset = SECTOR_HEADER_SIZE;
            m_tail_consumed = 0;
        }
        if (!dropOldestSector()) {
            return false;
        }
    }

    // One erase per reuse cycle: skip if advanceTailSector() already erased
    // this sector this boot (the erased map is RAM-only, so the first cycle
    // after power-on still erases conservatively).
    if (mapGetErased(next)) {
        USB_SERIAL_PRINTF("FlashRingBuffer::openNextHeadSector() - sector %u already erased, skipping erase\n", next);
    } else if (eraseSector(next) != ESP_OK) {
        return false;
    }

    SectorHeader header;
    memset(&header, 0xFF, sizeof(header));
    header.magic = SECTOR_MAGIC;
    header.seq = m_next_seq;
    header.hdr_crc = calculateCRC32((const uint8_t*)&header, 8);

    // Program only the open portion (12 bytes); close fields stay erased.
    if (writeBytes(next * SECTOR_SIZE, &header, 12) != ESP_OK) {
        return false;
    }
    m_write_count++;

    m_head_sector = next;
    m_head_seq = m_next_seq;
    m_next_seq++;
    m_head_open = true;
    m_head_write_offset = SECTOR_HEADER_SIZE;
    m_head_used = 0;
    m_head_record_count = 0;
    m_ring_virgin = false;
    mapSet(next, true);

    if (m_record_count == 0) {
        // Ring was empty: the tail cursor follows the new head sector.
        m_tail_sector = next;
        m_tail_offset = SECTOR_HEADER_SIZE;
        m_tail_consumed = 0;
        m_tail_seq = m_head_seq;
        m_tail_cache_valid = false;
        invalidatePeek();
    }

    USB_SERIAL_PRINTF("FlashRingBuffer::openNextHeadSector() - opened sector %u seq %u\n",
                      m_head_sector, m_head_seq);

    savePersistedState();
    return true;
}

bool FlashRingBuffer::closeHeadSector() {
    if (!m_head_open) {
        return true;
    }

    uint32_t base = m_head_sector * SECTOR_SIZE;
    uint32_t fill = SECTOR_SIZE - m_head_write_offset;

    // Sentinel-fill the unused remainder (spec requirement: trailing sentinels
    // mark a completely written sector). Uses the tail cache as scratch.
    if (fill > 0) {
        if (m_tail_cache_valid && m_tail_cache_sector == m_head_sector) {
            m_tail_cache_valid = false;
        }
        for (uint32_t i = 0; i + 1 < fill; i += 2) {
            m_tail_cache[i] = 0xAA;
            m_tail_cache[i + 1] = 0x55;
        }
        if (fill & 1) {
            m_tail_cache[fill - 1] = 0xAA;
        }
        if (writeBytes(base + m_head_write_offset, m_tail_cache, fill) != ESP_OK) {
            return false;
        }
        m_tail_cache_valid = false;  // scratch destroyed the cached image
        m_write_count++;
    }

    // Program the close marker (8 bytes at header offset 12, previously 0xFF).
    struct __attribute__((packed)) {
        uint16_t closed_used;
        uint16_t closed_count;
        uint32_t closed_crc;
    } marker;
    marker.closed_used = (uint16_t)m_head_used;
    marker.closed_count = (uint16_t)m_head_record_count;
    uint8_t close_fields[4] = {
        (uint8_t)(marker.closed_used & 0xFF),  (uint8_t)(marker.closed_used >> 8),
        (uint8_t)(marker.closed_count & 0xFF), (uint8_t)(marker.closed_count >> 8)
    };
    marker.closed_crc = calculateCRC32(close_fields, 4);

    if (writeBytes(base + 12, &marker, sizeof(marker)) != ESP_OK) {
        return false;
    }
    m_write_count++;
    m_head_open = false;

    USB_SERIAL_PRINTF("FlashRingBuffer::closeHeadSector() - sector %u closed: %u bytes, %u records\n",
                      m_head_sector, m_head_used, m_head_record_count);

    savePersistedState();
    return true;
}

bool FlashRingBuffer::dropOldestSector() {
    uint32_t victim = m_tail_sector;

    SectorHeader header;
    uint32_t total_in_sector = 0;
    if (readSectorHeader(victim, header) && headerValid(header) && headerClosed(header)) {
        total_in_sector = header.closed_count;
    } else if (victim == m_head_sector && m_head_open) {
        total_in_sector = m_head_record_count;  // pathological; guarded by caller
    }

    uint32_t remaining = (total_in_sector > m_tail_consumed)
                             ? (total_in_sector - m_tail_consumed) : 0;
    m_record_count = (m_record_count > remaining) ? (m_record_count - remaining) : 0;

    USB_SERIAL_PRINTF("FlashRingBuffer::dropOldestSector() - sector %u dropped, %u unread records lost\n",
                      victim, remaining);

    mapSet(victim, false);
    if (m_tail_cache_valid && m_tail_cache_sector == victim) {
        m_tail_cache_valid = false;
    }
    invalidatePeek();

    uint32_t next = mapNextInUse(victim);
    if (next == RING_SECTORS) {
        m_tail_sector = m_head_sector;
        m_tail_seq = m_head_seq;
    } else {
        m_tail_sector = next;
        SectorHeader next_header;
        m_tail_seq = (readSectorHeader(next, next_header) && headerValid(next_header))
                         ? next_header.seq : 0;
    }
    m_tail_offset = SECTOR_HEADER_SIZE;
    m_tail_consumed = 0;
    return true;
}

// ---------------------------------------------------------------------------
// Read path: tail cache, peek, consume
// ---------------------------------------------------------------------------

uint32_t FlashRingBuffer::scanRecordExtent(const uint8_t* sector_image, uint32_t limit,
                                           uint32_t* record_count_out) const {
    uint32_t offset = SECTOR_HEADER_SIZE;
    uint32_t count = 0;

    while (offset + RECORD_HEADER_SIZE <= limit) {
        RecordHeader header;
        memcpy(&header, sector_image + offset, RECORD_HEADER_SIZE);

        if (header.len == 0xFFFF) {
            break;  // erased space - clean end
        }
        if (header.len < MIN_MESSAGE_SIZE || header.len > MAX_MESSAGE_SIZE) {
            break;
        }
        uint32_t slot = alignSlot(RECORD_HEADER_SIZE + header.len);
        if (offset + slot > limit) {
            break;
        }
        uint16_t crc = recordCRC(header.meta, sector_image + offset + RECORD_HEADER_SIZE,
                                 header.len);
        if (crc != header.crc16) {
            break;
        }
        offset += slot;
        count++;
    }

    if (record_count_out) {
        *record_count_out = count;
    }
    return offset;
}

bool FlashRingBuffer::loadTailCache() {
    if (readBytes(m_tail_sector * SECTOR_SIZE, m_tail_cache, SECTOR_SIZE) != ESP_OK) {
        m_tail_cache_valid = false;
        return false;
    }
    m_tail_cache_sector = m_tail_sector;

    if (m_tail_sector == m_head_sector && m_head_open) {
        // RAM bookkeeping is authoritative for the open head sector.
        m_tail_cache_extent = m_head_write_offset;
    } else {
        SectorHeader header;
        memcpy(&header, m_tail_cache, sizeof(header));
        if (headerValid(header) && headerClosed(header)) {
            m_tail_cache_extent = SECTOR_HEADER_SIZE + header.closed_used;
        } else if (headerValid(header)) {
            // Unexpectedly open non-head sector (torn close): trust a record scan.
            m_tail_cache_extent = scanRecordExtent(m_tail_cache, SECTOR_SIZE, nullptr);
        } else {
            m_tail_cache_extent = SECTOR_HEADER_SIZE;  // nothing readable
        }
    }

    m_tail_cache_valid = true;
    return true;
}

uint32_t FlashRingBuffer::tailSectorDataEnd() const {
    if (m_tail_sector == m_head_sector && m_head_open) {
        return m_head_write_offset;
    }
    return m_tail_cache_valid ? m_tail_cache_extent : SECTOR_HEADER_SIZE;
}

bool FlashRingBuffer::peekOldestRecord(uint8_t* payload, uint16_t max_length,
                                       uint16_t& actual_length, uint16_t& meta) {
    if (!m_initialized || m_fatal || !payload) {
        return false;
    }

    // Re-serve a pending peek (stable until consumed).
    if (m_peek_valid) {
        if (!m_tail_cache_valid || m_tail_cache_sector != m_tail_sector) {
            if (!loadTailCache()) {
                return false;
            }
        }
        if (m_peek_len > max_length) {
            actual_length = m_peek_len;
            return false;
        }
        memcpy(payload, m_tail_cache + m_peek_offset + RECORD_HEADER_SIZE, m_peek_len);
        actual_length = m_peek_len;
        meta = m_peek_meta;
        return true;
    }

    uint32_t guard = 0;
    while (m_record_count > 0 && guard++ <= TOTAL_SECTORS) {
        if (!m_tail_cache_valid || m_tail_cache_sector != m_tail_sector) {
            if (!loadTailCache()) {
                return false;
            }
        }
        // Pick up records flushed into the open head sector since the last load.
        if (m_tail_sector == m_head_sector && m_head_open &&
            m_tail_cache_extent < m_head_write_offset) {
            if (!loadTailCache()) {
                return false;
            }
        }

        uint32_t end = tailSectorDataEnd();

        if (m_tail_offset >= end) {
            // This sector is exhausted.
            if (m_tail_sector == m_head_sector) {
                // Caught up with the head - nothing flushed left to read.
                return false;
            }
            if (!advanceTailSector()) {
                return false;
            }
            continue;
        }

        RecordHeader header;
        memcpy(&header, m_tail_cache + m_tail_offset, RECORD_HEADER_SIZE);

        bool valid = header.len >= MIN_MESSAGE_SIZE && header.len <= MAX_MESSAGE_SIZE;
        uint32_t slot = valid ? alignSlot(RECORD_HEADER_SIZE + header.len) : 0;
        valid = valid && (m_tail_offset + slot <= end);
        if (valid) {
            uint16_t crc = recordCRC(header.meta,
                                     m_tail_cache + m_tail_offset + RECORD_HEADER_SIZE,
                                     header.len);
            valid = (crc == header.crc16);
        }

        if (!valid) {
            // Corruption mid-sector: skip the remainder of this sector.
            SectorHeader sector_header;
            memcpy(&sector_header, m_tail_cache, sizeof(sector_header));
            uint32_t expected = 0;
            if (m_tail_sector == m_head_sector && m_head_open) {
                expected = m_head_record_count;
            } else if (headerValid(sector_header) && headerClosed(sector_header)) {
                expected = sector_header.closed_count;
            }
            uint32_t lost = (expected > m_tail_consumed) ? (expected - m_tail_consumed) : 0;
            m_record_count = (m_record_count > lost) ? (m_record_count - lost) : 0;

            USB_SERIAL_PRINTF("FlashRingBuffer::peekOldestRecord() - corrupt record in sector %u @ %u, skipping %u records\n",
                              m_tail_sector, m_tail_offset, lost);

            if (m_tail_sector == m_head_sector && m_head_open) {
                m_tail_offset = m_head_write_offset;  // skip to live append point
                return false;
            }
            if (!advanceTailSector()) {
                return false;
            }
            continue;
        }

        if (header.len > max_length) {
            USB_SERIAL_PRINTF("FlashRingBuffer::peekOldestRecord() - record %u bytes exceeds caller buffer %u\n",
                              header.len, max_length);
            actual_length = header.len;
            return false;
        }

        memcpy(payload, m_tail_cache + m_tail_offset + RECORD_HEADER_SIZE, header.len);
        actual_length = header.len;
        meta = header.meta;

        m_peek_valid = true;
        m_peek_len = header.len;
        m_peek_meta = header.meta;
        m_peek_offset = m_tail_offset;
        return true;
    }

    return false;
}

bool FlashRingBuffer::consumeOldestRecord() {
    if (!m_initialized || m_fatal || !m_peek_valid) {
        return false;
    }

    m_tail_offset = m_peek_offset + alignSlot(RECORD_HEADER_SIZE + m_peek_len);
    m_tail_consumed++;
    if (m_record_count > 0) {
        m_record_count--;
    }
    invalidatePeek();

    // Reclaim the sector as soon as its last record is consumed - unless it is
    // the open head sector, which is still receiving appends.
    bool tail_is_open_head = (m_tail_sector == m_head_sector && m_head_open);
    if (!tail_is_open_head && m_tail_offset >= tailSectorDataEnd()) {
        advanceTailSector();
    }
    return true;
}

bool FlashRingBuffer::peekOldestAssemblyRecord(uint8_t* payload, uint16_t max_length,
                                               uint16_t& length, uint16_t& meta) const {
    // RAM only: deliberately admitted in FATAL so accepted records can move
    // to fallback storage without touching untrusted flash state.
    if (!m_assembly || m_assembly_count == 0 || !payload) {
        return false;
    }

    RecordHeader header;
    memcpy(&header, m_assembly, RECORD_HEADER_SIZE);
    if (header.len < MIN_MESSAGE_SIZE || header.len > MAX_MESSAGE_SIZE ||
        header.len > max_length) {
        return false;
    }

    memcpy(payload, m_assembly + RECORD_HEADER_SIZE, header.len);
    length = header.len;
    meta = header.meta;

    uint32_t slot = alignSlot(RECORD_HEADER_SIZE + header.len);
    return slot <= m_assembly_used;
}

bool FlashRingBuffer::consumeOldestAssemblyRecord() {
    if (!m_assembly || m_assembly_count == 0) {
        return false;
    }

    RecordHeader header;
    memcpy(&header, m_assembly, RECORD_HEADER_SIZE);
    if (header.len < MIN_MESSAGE_SIZE || header.len > MAX_MESSAGE_SIZE) {
        return false;
    }

    uint32_t slot = alignSlot(RECORD_HEADER_SIZE + header.len);
    if (slot > m_assembly_used) {
        return false;
    }
    memmove(m_assembly, m_assembly + slot, m_assembly_used - slot);
    m_assembly_used -= slot;
    m_assembly_count--;
    if (m_assembly_count == 0) {
        m_assembly_used = 0;
        m_first_unflushed_ms = 0;
    }
    return true;
}

bool FlashRingBuffer::drainOldestAssemblyRecord(uint8_t* payload, uint16_t max_length,
                                                uint16_t& length, uint16_t& meta) {
    return peekOldestAssemblyRecord(payload, max_length, length, meta) &&
           consumeOldestAssemblyRecord();
}

bool FlashRingBuffer::advanceTailSector() {
    uint32_t old = m_tail_sector;
    bool old_was_head = (old == m_head_sector);

    if (old_was_head && m_head_open) {
        // Never erase the sector still being appended to.
        return false;
    }

    if (eraseSector(old) != ESP_OK) {
        return false;
    }
    mapSet(old, false);
    if (m_tail_cache_valid && m_tail_cache_sector == old) {
        m_tail_cache_valid = false;
    }
    invalidatePeek();

    USB_SERIAL_PRINTF("FlashRingBuffer::advanceTailSector() - reclaimed sector %u\n", old);

    if (old_was_head) {
        // The entire ring has been consumed.
        m_record_count = 0;
        m_tail_sector = m_head_sector;
        m_tail_seq = m_head_seq;
        m_tail_offset = SECTOR_HEADER_SIZE;
        m_tail_consumed = 0;
        savePersistedState();
        return true;
    }

    uint32_t next = mapNextInUse(old);
    if (next == RING_SECTORS) {
        // No in-use sector left (count drift) - reset to empty at the head.
        m_record_count = 0;
        m_tail_sector = m_head_sector;
        m_tail_seq = m_head_seq;
    } else {
        m_tail_sector = next;
        SectorHeader header;
        m_tail_seq = (readSectorHeader(next, header) && headerValid(header))
                         ? header.seq : 0;
    }
    m_tail_offset = SECTOR_HEADER_SIZE;
    m_tail_consumed = 0;
    savePersistedState();
    return true;
}

// ---------------------------------------------------------------------------
// Boot recovery
// ---------------------------------------------------------------------------

void FlashRingBuffer::canonicalEmptyState(RecoveredState& s) const {
    memset(&s, 0, sizeof(s));
    s.head_sector = 0;
    s.head_seq = 0;
    s.head_open = false;
    s.head_write_offset = SECTOR_HEADER_SIZE;
    s.head_used = 0;
    s.head_record_count = 0;
    s.ring_virgin = true;
    s.tail_sector = 0;
    s.tail_seq = 0;
    s.tail_offset = SECTOR_HEADER_SIZE;
    s.tail_consumed = 0;
    s.record_count = 0;
    s.next_seq = (m_next_seq > 0) ? m_next_seq : 1;  // seq stays monotonic
}

void FlashRingBuffer::applyRecoveredState(const RecoveredState& s) {
    memcpy(m_sector_map, s.sector_map, sizeof(m_sector_map));
    m_head_sector = s.head_sector;
    m_head_seq = s.head_seq;
    m_head_open = s.head_open;
    m_head_write_offset = s.head_write_offset;
    m_head_used = s.head_used;
    m_head_record_count = s.head_record_count;
    m_ring_virgin = s.ring_virgin;
    m_tail_sector = s.tail_sector;
    m_tail_seq = s.tail_seq;
    m_tail_offset = s.tail_offset;
    m_tail_consumed = s.tail_consumed;
    m_record_count = s.record_count;
    m_next_seq = s.next_seq;
    m_tail_cache_valid = false;
    invalidatePeek();
}

bool FlashRingBuffer::buildRecoveredState(RecoveredState& out, FlashDiagnostics* diag,
                                          bool& flash_modified, bool& keep_old_state_ok) {
    flash_modified = false;
    keep_old_state_ok = true;
    canonicalEmptyState(out);

    // Resolve any committed journal FIRST. An unresolved disposition means
    // normal operation must not resume on any path.
    if (!replayRepairJournal(flash_modified)) {
        keep_old_state_ok = false;
        return false;
    }

    FlashRingPersistedState nvs;
    bool have_nvs = loadPersistedState(nvs);
    if (have_nvs) {
        if (nvs.next_seq > out.next_seq) {
            out.next_seq = nvs.next_seq;
        }
        if (!m_initialized) {
            m_write_count = nvs.write_count;  // wear statistic; boot seed only
        }
    }

#ifdef TESTING_MODE
    if (m_seams.fail_recovery_before_sector_scan) {
        m_seams.fail_recovery_before_sector_scan = false;
        USB_SERIAL_PRINTLN("SEAM: injected recovery failure before sector scan");
        return false;
    }
#endif

    uint32_t lowest_seq = UINT32_MAX, highest_seq = 0;
    uint32_t head_candidate = RING_SECTORS, tail_candidate = RING_SECTORS;
    SectorHeader head_header, tail_header;
    uint32_t in_use = 0;
    uint32_t closed_records = 0;
    uint32_t open_count = 0, open_sector = RING_SECTORS;

    for (uint32_t sector = 0; sector < RING_SECTORS; sector++) {
        SectorHeader header;
        if (!readSectorHeader(sector, header)) {
            // Cannot prove this sector's disposition - the candidate would be
            // incomplete. Fail the scan; the caller keeps the old state (if
            // flash is unmodified) or goes fatal.
            USB_SERIAL_PRINTF("FlashRingBuffer::buildRecoveredState() - sector %u header unreadable, aborting scan\n",
                              sector);
            return false;
        }
        if (!headerValid(header)) {
            continue;
        }

        out.sector_map[sector >> 3] |= (uint8_t)(1u << (sector & 7));
        in_use++;

        if (headerClosed(header)) {
            closed_records += header.closed_count;
        } else {
            open_count++;
            open_sector = sector;
        }

        if (header.seq >= highest_seq) {
            highest_seq = header.seq;
            head_candidate = sector;
            head_header = header;
        }
        if (header.seq <= lowest_seq) {
            lowest_seq = header.seq;
            tail_candidate = sector;
            tail_header = header;
        }
        if (header.seq >= out.next_seq) {
            out.next_seq = header.seq + 1;
        }

        if ((sector & 0xFF) == 0) {
            delay(1);  // watchdog yield
        }
    }

    if (diag) {
        diag->in_use_sectors = in_use;
        diag->erased_sectors = RING_SECTORS - in_use;
    }

    if (in_use == 0) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::buildRecoveredState() - empty partition, canonical fresh state (no writes)");
        return true;  // 'out' is the complete canonical empty state
    }

    // Structural invariant: any open sector must be exactly the highest-seq
    // (head) sector. The append protocol closes a sector before opening the
    // next, so any other arrangement is corruption; records in a stray open
    // sector cannot be ordered or counted, and guessing would risk data. No
    // destructive auto-repair here - refuse and let the caller go fatal /
    // fail init.
    if (open_count > 1 || (open_count == 1 && open_sector != head_candidate)) {
        USB_SERIAL_PRINTF("FlashRingBuffer::buildRecoveredState() - structural corruption: %u open sectors, open %u vs head %u\n",
                          open_count, open_sector, head_candidate);
        keep_old_state_ok = false;
        return false;
    }

    // --- Head sector ---
    out.ring_virgin = false;
    out.head_sector = head_candidate;
    out.head_seq = head_header.seq;

    if (headerClosed(head_header)) {
        out.head_open = false;
        out.head_used = head_header.closed_used;
        out.head_record_count = head_header.closed_count;
        out.head_write_offset = SECTOR_HEADER_SIZE + out.head_used;
        out.record_count = closed_records;
    } else {
        // Open head sector: find the true end of valid data.
        if (readBytes(head_candidate * SECTOR_SIZE, m_tail_cache, SECTOR_SIZE) != ESP_OK) {
            return false;
        }
        uint32_t open_records = 0;
        uint32_t extent = scanRecordExtent(m_tail_cache, SECTOR_SIZE, &open_records);
        bool torn = !regionIsErased(m_tail_cache, extent, SECTOR_SIZE);

        if (torn) {
            USB_SERIAL_PRINTF("FlashRingBuffer::buildRecoveredState() - torn append in head sector %u (valid to %u), rescuing\n",
                              head_candidate, extent);
            // Set BEFORE attempting: a failed rescue may already have erased.
            flash_modified = true;
            keep_old_state_ok = false;
            if (!rescueTornHeadSector(head_candidate, extent, open_records)) {
                return false;
            }
            if (diag) {
                diag->torn_head_repairs++;
            }
        }

        out.head_open = true;
        out.head_write_offset = extent;
        out.head_used = extent - SECTOR_HEADER_SIZE;
        out.head_record_count = open_records;
        out.record_count = closed_records + open_records;
        m_tail_cache_valid = false;  // scratch use
    }

    // --- Tail cursor ---
    out.tail_sector = tail_candidate;
    out.tail_seq = tail_header.seq;
    out.tail_offset = SECTOR_HEADER_SIZE;
    out.tail_consumed = 0;

    if (have_nvs && nvs.tail_sector == tail_candidate && nvs.tail_seq == tail_header.seq) {
        uint32_t sector_end;
        if (tail_candidate == out.head_sector && out.head_open) {
            sector_end = out.head_write_offset;
        } else if (headerClosed(tail_header)) {
            sector_end = SECTOR_HEADER_SIZE + tail_header.closed_used;
        } else {
            sector_end = SECTOR_HEADER_SIZE;
        }
        bool offset_ok = nvs.tail_offset >= SECTOR_HEADER_SIZE &&
                         nvs.tail_offset <= sector_end &&
                         (nvs.tail_offset & 3u) == 0;
        if (offset_ok && nvs.tail_consumed <= out.record_count) {
            out.tail_offset = nvs.tail_offset;
            out.tail_consumed = nvs.tail_consumed;
            out.record_count -= nvs.tail_consumed;
            if (diag) {
                diag->nvs_state_applied = true;
            }
            USB_SERIAL_PRINTF("FlashRingBuffer::buildRecoveredState() - tail cursor restored from NVS: sector %u @ %u (%u consumed)\n",
                              out.tail_sector, out.tail_offset, out.tail_consumed);
        }
    }

    USB_SERIAL_PRINTF("FlashRingBuffer::buildRecoveredState() - %u sectors in use (seq %u..%u), %u unread records\n",
                      in_use, lowest_seq, highest_seq, out.record_count);
    return true;
}

bool FlashRingBuffer::scanAndRecover(FlashDiagnostics* diag, bool flash_already_modified) {
    USB_SERIAL_PRINTLN("FlashRingBuffer::scanAndRecover() - scanning sector headers");

    RecoveredState candidate;
    bool recovery_modified = false;
    bool keep_old_state_ok = true;

    if (!buildRecoveredState(candidate, diag, recovery_modified, keep_old_state_ok)) {
        bool flash_modified = flash_already_modified || recovery_modified;
        if (m_initialized) {
            // Runtime entry (POST). At boot, m_initialized is false and the
            // failed init tears down - nothing trusted exists yet.
            if (flash_modified || !keep_old_state_ok) {
                enterFatalState("recovery failed with flash modified or structure untrusted");
            } else {
                USB_SERIAL_PRINTLN("FlashRingBuffer::scanAndRecover() - scan failed, flash unmodified: previous trusted state retained");
            }
        }
        return false;
    }

    // Publish the verified candidate as one deliberate transition.
    applyRecoveredState(candidate);

    if (!candidate.ring_virgin) {
        // Non-critical: a failed cursor save only means harmless re-upload.
        savePersistedState();
    }
    return true;
}

bool FlashRingBuffer::rescueTornHeadSector(uint32_t sector, uint32_t valid_end,
                                           uint32_t valid_count) {
    // m_tail_cache holds the damaged sector's image. Durable copy-on-write:
    // journal the rescued records first (commit = journal header, written
    // last), only then erase and rebuild the damaged sector, finally clear
    // the journal. Power loss at ANY point is recoverable - boot replays a
    // committed journal (replayRepairJournal), and the replay is idempotent
    // because no other write can reach the target sector until the journal
    // has been durably cleared (a failed clear fails the whole rescue, and
    // with it initialization - see below).
    (void)valid_count;

    uint32_t seq;
    memcpy(&seq, m_tail_cache + 4, 4);  // original seq from the image

    uint32_t data_len = valid_end - SECTOR_HEADER_SIZE;
    const uint8_t* records = m_tail_cache + SECTOR_HEADER_SIZE;

    if (!writeRepairJournal(sector, seq, records, data_len)) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::rescueTornHeadSector() - journal write FAILED, sector left untouched");
        return false;
    }
    if (!rebuildSectorFromRecords(sector, seq, records, data_len)) {
        return false;
    }
    if (eraseSector(JOURNAL_SECTOR) != ESP_OK) {
        // FATAL: if the committed journal survives while the rebuilt sector
        // goes on to receive new appends, a later boot would replay the stale
        // snapshot over them. Failing here fails init, so no new record can
        // ever be written ahead of an uncleared journal.
        USB_SERIAL_PRINTLN("FlashRingBuffer::rescueTornHeadSector() - journal clear FAILED - refusing to continue (stale journal would destroy future records)");
        return false;
    }

    USB_SERIAL_PRINTF("FlashRingBuffer::rescueTornHeadSector() - sector %u rebuilt with %u bytes of rescued records\n",
                      sector, data_len);
    return true;
}

bool FlashRingBuffer::writeRepairJournal(uint32_t target_sector, uint32_t seq,
                                         const uint8_t* records, uint32_t data_len) {
    if (data_len > SECTOR_SIZE - JOURNAL_HEADER_SIZE) {
        return false;
    }
    if (eraseSector(JOURNAL_SECTOR) != ESP_OK) {
        return false;
    }

    uint32_t base = JOURNAL_SECTOR * SECTOR_SIZE;

    // Data first; the header is the commit point and is written last.
    if (data_len > 0) {
        if (writeBytes(base + JOURNAL_HEADER_SIZE, records, data_len) != ESP_OK) {
            return false;
        }
        m_write_count++;
    }

    JournalHeader journal;
    memset(&journal, 0xFF, sizeof(journal));
    journal.magic = JOURNAL_MAGIC;
    journal.target_sector = target_sector;
    journal.seq = seq;
    journal.data_len = data_len;
    journal.data_crc = calculateCRC32(records, data_len);
    journal.hdr_crc = calculateCRC32((const uint8_t*)&journal, 20);

    if (writeBytes(base, &journal, sizeof(journal)) != ESP_OK) {
        return false;
    }
    m_write_count++;

    USB_SERIAL_PRINTF("FlashRingBuffer::writeRepairJournal() - journal committed for sector %u (%u bytes)\n",
                      target_sector, data_len);
    return true;
}

bool FlashRingBuffer::rebuildSectorFromRecords(uint32_t target_sector, uint32_t seq,
                                               const uint8_t* records, uint32_t data_len) {
    if (eraseSector(target_sector) != ESP_OK) {
        return false;
    }

    SectorHeader header;
    memset(&header, 0xFF, sizeof(header));
    header.magic = SECTOR_MAGIC;
    header.seq = seq;
    header.hdr_crc = calculateCRC32((const uint8_t*)&header, 8);

    if (writeBytes(target_sector * SECTOR_SIZE, &header, 12) != ESP_OK) {
        return false;
    }
    m_write_count++;

    if (data_len > 0) {
        if (writeBytes(target_sector * SECTOR_SIZE + SECTOR_HEADER_SIZE,
                       records, data_len) != ESP_OK) {
            return false;
        }
        m_write_count++;
    }
    return true;
}

bool FlashRingBuffer::replayRepairJournal(bool& flash_modified) {
    JournalHeader journal;
    if (readBytes(JOURNAL_SECTOR * SECTOR_SIZE, &journal, sizeof(journal)) != ESP_OK) {
        return false;  // cannot even determine whether a journal exists
    }

    if (journal.magic != JOURNAL_MAGIC ||
        calculateCRC32((const uint8_t*)&journal, 20) != journal.hdr_crc) {
        return true;  // no committed journal (blank or torn journal write) - nothing pending
    }

    // From this point a COMMITTED journal exists. Every exit below must fully
    // resolve its disposition: either replayed-and-cleared or
    // discarded-and-cleared, with the erase VERIFIED. Returning true while a
    // committed journal survives would let normal writes reach the target
    // sector and a later boot replay the stale snapshot over them.

    if (journal.target_sector >= RING_SECTORS ||
        journal.data_len > SECTOR_SIZE - JOURNAL_HEADER_SIZE) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::replayRepairJournal() - journal fields out of range, discarding");
        if (eraseSector(JOURNAL_SECTOR) != ESP_OK) {
            USB_SERIAL_PRINTLN("FlashRingBuffer::replayRepairJournal() - discard erase FAILED - journal disposition unresolved");
            return false;
        }
        return true;  // target untouched, journal verifiably gone
    }

    USB_SERIAL_PRINTF("FlashRingBuffer::replayRepairJournal() - interrupted repair found for sector %u, replaying\n",
                      journal.target_sector);

    // m_tail_cache is free at this point (recovery scan has not started).
    if (readBytes(JOURNAL_SECTOR * SECTOR_SIZE + JOURNAL_HEADER_SIZE,
                  m_tail_cache, journal.data_len) != ESP_OK) {
        return false;  // disposition unresolved
    }
    if (calculateCRC32(m_tail_cache, journal.data_len) != journal.data_crc) {
        // Header committed but data does not verify (transient read fault or
        // flash corruption). The journal cannot be replayed; it must still be
        // VERIFIABLY cleared before operation resumes, because a later boot
        // might read it successfully and replay the stale snapshot.
        USB_SERIAL_PRINTLN("FlashRingBuffer::replayRepairJournal() - ERROR: journal data CRC mismatch, discarding journal");
        if (eraseSector(JOURNAL_SECTOR) != ESP_OK) {
            USB_SERIAL_PRINTLN("FlashRingBuffer::replayRepairJournal() - discard erase FAILED - journal disposition unresolved");
            return false;
        }
        return true;  // target untouched, journal verifiably gone
    }

    flash_modified = true;  // about to modify the target ring sector
    if (!rebuildSectorFromRecords(journal.target_sector, journal.seq,
                                  m_tail_cache, journal.data_len)) {
        return false;
    }
    if (eraseSector(JOURNAL_SECTOR) != ESP_OK) {
        // Normal operation must never resume while a committed journal
        // remains, or the next boot's replay would erase records appended
        // after this rebuild.
        USB_SERIAL_PRINTLN("FlashRingBuffer::replayRepairJournal() - journal clear FAILED - refusing to continue (stale journal would destroy future records)");
        return false;
    }

    USB_SERIAL_PRINTF("FlashRingBuffer::replayRepairJournal() - sector %u repair completed\n",
                      journal.target_sector);
    return true;
}

// ---------------------------------------------------------------------------
// Persistent state (NVS)
// ---------------------------------------------------------------------------

bool FlashRingBuffer::loadPersistedState(FlashRingPersistedState& out) const {
    // Preferences::isKey is non-const in some cores; cast is safe (read-only op).
    Preferences& prefs = const_cast<Preferences&>(m_preferences);

    if (!prefs.isKey("state2")) {
        return false;
    }
    if (prefs.getBytesLength("state2") != sizeof(FlashRingPersistedState)) {
        return false;
    }
    FlashRingPersistedState state;
    if (prefs.getBytes("state2", &state, sizeof(state)) != sizeof(state)) {
        return false;
    }
    uint32_t crc = calculateCRC32((const uint8_t*)&state,
                                  sizeof(state) - sizeof(state.crc));
    if (crc != state.crc || state.version != 2) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::loadPersistedState() - stored state invalid, ignoring");
        return false;
    }
    if (state.tail_sector >= RING_SECTORS || state.tail_offset > SECTOR_SIZE) {
        return false;
    }
    out = state;
    return true;
}

bool FlashRingBuffer::savePersistedState() {
#ifdef TESTING_MODE
    if (seamFire(m_seams.fail_nvs_save_countdown)) {
        USB_SERIAL_PRINTLN("SEAM: injected NVS save failure");
        return false;
    }
#endif
    FlashRingPersistedState state;
    state.version = 2;
    state.tail_sector = m_tail_sector;
    state.tail_seq = m_tail_seq;
    state.tail_offset = m_tail_offset;
    state.tail_consumed = m_tail_consumed;
    state.next_seq = m_next_seq;
    state.write_count = m_write_count;
    state.crc = calculateCRC32((const uint8_t*)&state,
                               sizeof(state) - sizeof(state.crc));

    size_t written = m_preferences.putBytes("state2", &state, sizeof(state));
    if (written != sizeof(state)) {
        USB_SERIAL_PRINTF("FlashRingBuffer::savePersistedState() - NVS write failed (%u bytes)\n",
                          (unsigned)written);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Status / shutdown
// ---------------------------------------------------------------------------

uint32_t FlashRingBuffer::getUsedSpace() const {
    if (!m_initialized) {
        return 0;
    }
    return mapCountInUse() * SECTOR_SIZE + m_assembly_used;
}

uint32_t FlashRingBuffer::getFreeSpace() const {
    uint32_t used = getUsedSpace();
    return (used >= RING_BUFFER_SIZE) ? 0 : (RING_BUFFER_SIZE - used);
}

bool FlashRingBuffer::isFull() const {
    if (!m_initialized) {
        return false;
    }
    // Drop-oldest policy means appends never block; report "full" only in the
    // transient state where every sector is in use and the head has no room.
    return mapCountInUse() == RING_SECTORS &&
           (!m_head_open || (SECTOR_SIZE - m_head_write_offset) < MIN_SLOT_SIZE);
}

void FlashRingBuffer::printStatus() const {
    USB_SERIAL_PRINTLN("=== FlashRingBuffer Status ===");
    USB_SERIAL_PRINTF("Initialized: %s%s%s\n", m_initialized ? "Yes" : "No",
                      m_writes_disabled ? " (writes disabled - shutdown prepared)" : "",
                      m_fatal ? " (FATAL - recovery failed, data ops disabled)" : "");
    USB_SERIAL_PRINTF("Head: sector %u seq %u %s, append offset %u (%u bytes, %u records)\n",
                      m_head_sector, m_head_seq, m_head_open ? "OPEN" : "closed",
                      m_head_write_offset, m_head_used, m_head_record_count);
    USB_SERIAL_PRINTF("Tail: sector %u seq %u, offset %u, consumed %u\n",
                      m_tail_sector, m_tail_seq, m_tail_offset, m_tail_consumed);
    USB_SERIAL_PRINTF("Records: %u flushed + %u in RAM assembly = %u total\n",
                      m_record_count, m_assembly_count, getRecordCount());
    USB_SERIAL_PRINTF("Assembly buffer: %u bytes pending\n", m_assembly_used);
    USB_SERIAL_PRINTF("Sectors in use: %u / %u (last sector reserved for repair journal)\n",
                      mapCountInUse(), RING_SECTORS);
    USB_SERIAL_PRINTF("Space: %u used / %u free\n", getUsedSpace(), getFreeSpace());
    USB_SERIAL_PRINTF("Next seq: %u, lifetime program ops: %u\n", m_next_seq, m_write_count);
    USB_SERIAL_PRINTLN("=============================");
}

bool FlashRingBuffer::prepareForShutdown() {
    USB_SERIAL_PRINTLN("FlashRingBuffer::prepareForShutdown() - flushing and locking writes");
    if (!m_initialized) {
        return true;  // nothing pending, nothing to lose
    }

    bool flushed = flush();
    bool saved = savePersistedState();

    if (!flushed || !saved) {
        // Writes stay ENABLED so the operation can be retried.
        USB_SERIAL_PRINTF("FlashRingBuffer::prepareForShutdown() - NOT SAFE TO POWER OFF "
                          "(flush %s, state save %s) - %u records still in RAM, retry or expect loss\n",
                          flushed ? "ok" : "FAILED", saved ? "ok" : "FAILED",
                          m_assembly_count);
        return false;
    }

    m_writes_disabled = true;
    USB_SERIAL_PRINTLN("FlashRingBuffer::prepareForShutdown() - SAFE TO POWER OFF");
    return true;
}

bool FlashRingBuffer::emergencyFlush() {
    if (!m_initialized) {
        return true;
    }
    bool flushed = flush();
    bool saved = savePersistedState();
    USB_SERIAL_PRINTF("FlashRingBuffer::emergencyFlush() - %s\n",
                      (flushed && saved) ? "complete" : "FAILED (data may remain in RAM)");
    return flushed && saved;
}

#define BUILD_INCLUDE_FLASHRINGBUFFER_PART2

#include "FlashRingBuffer_part2.cpp"
