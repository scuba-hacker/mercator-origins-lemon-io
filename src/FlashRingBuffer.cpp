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

    m_tail_cache_sector = 0;
    m_tail_cache_extent = 0;
    m_tail_cache_valid = false;

    m_peek_valid = false;
    m_peek_len = 0;
    m_peek_meta = 0;
    m_peek_offset = 0;

    memset(m_sector_map, 0, sizeof(m_sector_map));
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
    if (m_initialized) {
        // Persist anything still pending so a deliberate teardown loses nothing.
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
    if (sector_index >= TOTAL_SECTORS || !m_partition) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = esp_partition_erase_range(m_partition, sector_index * SECTOR_SIZE, SECTOR_SIZE);
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::eraseSector() - sector %u failed: %s\n",
                          sector_index, esp_err_to_name(err));
    }
    return err;
}

esp_err_t FlashRingBuffer::writeBytes(uint32_t offset, const void* src, size_t len) {
    if (!m_partition || offset + len > RING_BUFFER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = esp_partition_write(m_partition, offset, src, len);
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::writeBytes() - offset 0x%x len %u failed: %s\n",
                          offset, (unsigned)len, esp_err_to_name(err));
    }
    return err;
}

esp_err_t FlashRingBuffer::readBytes(uint32_t offset, void* dst, size_t len) const {
    if (!m_partition || offset + len > RING_BUFFER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = esp_partition_read(m_partition, offset, dst, len);
    if (err != ESP_OK) {
        USB_SERIAL_PRINTF("FlashRingBuffer::readBytes() - offset 0x%x len %u failed: %s\n",
                          offset, (unsigned)len, esp_err_to_name(err));
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
    for (uint32_t step = 1; step <= TOTAL_SECTORS; step++) {
        uint32_t sector = (from + step) % TOTAL_SECTORS;
        if (mapGet(sector)) {
            return sector;
        }
    }
    return TOTAL_SECTORS;
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
    if (length < MIN_MESSAGE_SIZE || length > MAX_MESSAGE_SIZE) {
        USB_SERIAL_PRINTF("FlashRingBuffer::appendRecord() - invalid length %u (must be %u-%u)\n",
                          length, MIN_MESSAGE_SIZE, MAX_MESSAGE_SIZE);
        return false;
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

    uint32_t now = m_fn_millis ? m_fn_millis() : 0;
    if (m_first_unflushed_ms == 0) {
        m_first_unflushed_ms = now ? now : 1;
    }

    // Age-based flush bounds RAM data loss on an unplanned power cut.
    if (now && (now - m_first_unflushed_ms) >= m_max_unflushed_ms) {
        flush();
    }

    return true;
}

bool FlashRingBuffer::flush() {
    if (!m_initialized) {
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
        closeHeadSector();
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
        next = (m_head_sector + 1) % TOTAL_SECTORS;
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

    if (eraseSector(next) != ESP_OK) {
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
    if (next == TOTAL_SECTORS) {
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
    if (!m_initialized || !payload) {
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
    if (!m_initialized || !m_peek_valid) {
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
    if (next == TOTAL_SECTORS) {
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

bool FlashRingBuffer::scanAndRecover(FlashDiagnostics* diag) {
    USB_SERIAL_PRINTLN("FlashRingBuffer::scanAndRecover() - scanning sector headers");

    FlashRingPersistedState nvs;
    bool have_nvs = loadPersistedState(nvs);
    if (have_nvs) {
        m_next_seq = (nvs.next_seq > 0) ? nvs.next_seq : 1;
        m_write_count = nvs.write_count;
    }

    uint32_t lowest_seq = UINT32_MAX, highest_seq = 0;
    uint32_t head_candidate = TOTAL_SECTORS, tail_candidate = TOTAL_SECTORS;
    SectorHeader head_header, tail_header;
    uint32_t in_use = 0;
    uint32_t closed_records = 0;

    for (uint32_t sector = 0; sector < TOTAL_SECTORS; sector++) {
        SectorHeader header;
        if (!readSectorHeader(sector, header) || !headerValid(header)) {
            continue;
        }

        mapSet(sector, true);
        in_use++;

        if (headerClosed(header)) {
            closed_records += header.closed_count;
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
        if (header.seq >= m_next_seq) {
            m_next_seq = header.seq + 1;
        }

        if ((sector & 0xFF) == 0) {
            delay(1);  // watchdog yield
        }
    }

    if (diag) {
        diag->in_use_sectors = in_use;
        diag->erased_sectors = TOTAL_SECTORS - in_use;
    }

    if (in_use == 0) {
        USB_SERIAL_PRINTLN("FlashRingBuffer::scanAndRecover() - empty partition, lazy fresh start (no writes)");
        m_ring_virgin = true;
        m_head_sector = 0;
        m_tail_sector = 0;
        m_record_count = 0;
        return true;
    }

    // --- Head sector ---
    m_head_sector = head_candidate;
    m_head_seq = head_header.seq;
    m_ring_virgin = false;

    if (headerClosed(head_header)) {
        m_head_open = false;
        m_head_used = head_header.closed_used;
        m_head_record_count = head_header.closed_count;
        m_head_write_offset = SECTOR_HEADER_SIZE + m_head_used;
        m_record_count = closed_records;
    } else {
        // Open head sector: find the true end of valid data.
        if (readBytes(head_candidate * SECTOR_SIZE, m_tail_cache, SECTOR_SIZE) != ESP_OK) {
            return false;
        }
        uint32_t open_count = 0;
        uint32_t extent = scanRecordExtent(m_tail_cache, SECTOR_SIZE, &open_count);
        bool torn = !regionIsErased(m_tail_cache, extent, SECTOR_SIZE);

        if (torn) {
            USB_SERIAL_PRINTF("FlashRingBuffer::scanAndRecover() - torn append in head sector %u (valid to %u), rescuing\n",
                              head_candidate, extent);
            if (!rescueTornHeadSector(head_candidate, extent, open_count)) {
                return false;
            }
            if (diag) {
                diag->torn_head_repairs++;
            }
        }

        m_head_open = true;
        m_head_write_offset = extent;
        m_head_used = extent - SECTOR_HEADER_SIZE;
        m_head_record_count = open_count;
        m_record_count = closed_records + open_count;
        m_tail_cache_valid = false;  // scratch use
    }

    // --- Tail cursor ---
    m_tail_sector = tail_candidate;
    m_tail_seq = tail_header.seq;
    m_tail_offset = SECTOR_HEADER_SIZE;
    m_tail_consumed = 0;

    if (have_nvs && nvs.tail_sector == tail_candidate && nvs.tail_seq == tail_header.seq) {
        uint32_t sector_end;
        if (tail_candidate == m_head_sector && m_head_open) {
            sector_end = m_head_write_offset;
        } else if (headerClosed(tail_header)) {
            sector_end = SECTOR_HEADER_SIZE + tail_header.closed_used;
        } else {
            sector_end = SECTOR_HEADER_SIZE;
        }
        bool offset_ok = nvs.tail_offset >= SECTOR_HEADER_SIZE &&
                         nvs.tail_offset <= sector_end &&
                         (nvs.tail_offset & 3u) == 0;
        if (offset_ok && nvs.tail_consumed <= m_record_count) {
            m_tail_offset = nvs.tail_offset;
            m_tail_consumed = nvs.tail_consumed;
            m_record_count -= nvs.tail_consumed;
            if (diag) {
                diag->nvs_state_applied = true;
            }
            USB_SERIAL_PRINTF("FlashRingBuffer::scanAndRecover() - tail cursor restored from NVS: sector %u @ %u (%u consumed)\n",
                              m_tail_sector, m_tail_offset, m_tail_consumed);
        }
    }

    USB_SERIAL_PRINTF("FlashRingBuffer::scanAndRecover() - %u sectors in use (seq %u..%u), %u unread records\n",
                      in_use, lowest_seq, highest_seq, m_record_count);

    savePersistedState();
    return true;
}

bool FlashRingBuffer::rescueTornHeadSector(uint32_t sector, uint32_t valid_end,
                                           uint32_t valid_count) {
    // m_tail_cache holds the sector image. Rebuild: erase, restamp the header
    // with the SAME seq, rewrite the rescued records, leave the sector open.
    (void)valid_count;

    if (eraseSector(sector) != ESP_OK) {
        return false;
    }

    SectorHeader header;
    memset(&header, 0xFF, sizeof(header));
    header.magic = SECTOR_MAGIC;
    memcpy(&header.seq, m_tail_cache + 4, 4);  // original seq from the image
    header.hdr_crc = calculateCRC32((const uint8_t*)&header, 8);

    if (writeBytes(sector * SECTOR_SIZE, &header, 12) != ESP_OK) {
        return false;
    }
    m_write_count++;

    if (valid_end > SECTOR_HEADER_SIZE) {
        if (writeBytes(sector * SECTOR_SIZE + SECTOR_HEADER_SIZE,
                       m_tail_cache + SECTOR_HEADER_SIZE,
                       valid_end - SECTOR_HEADER_SIZE) != ESP_OK) {
            return false;
        }
        m_write_count++;
    }

    USB_SERIAL_PRINTF("FlashRingBuffer::rescueTornHeadSector() - sector %u rebuilt with %u bytes of rescued records\n",
                      sector, valid_end - SECTOR_HEADER_SIZE);
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
    if (state.tail_sector >= TOTAL_SECTORS || state.tail_offset > SECTOR_SIZE) {
        return false;
    }
    out = state;
    return true;
}

bool FlashRingBuffer::savePersistedState() {
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
    return mapCountInUse() == TOTAL_SECTORS &&
           (!m_head_open || (SECTOR_SIZE - m_head_write_offset) < MIN_SLOT_SIZE);
}

void FlashRingBuffer::printStatus() const {
    USB_SERIAL_PRINTLN("=== FlashRingBuffer Status ===");
    USB_SERIAL_PRINTF("Initialized: %s%s\n", m_initialized ? "Yes" : "No",
                      m_writes_disabled ? " (writes disabled - shutdown prepared)" : "");
    USB_SERIAL_PRINTF("Head: sector %u seq %u %s, append offset %u (%u bytes, %u records)\n",
                      m_head_sector, m_head_seq, m_head_open ? "OPEN" : "closed",
                      m_head_write_offset, m_head_used, m_head_record_count);
    USB_SERIAL_PRINTF("Tail: sector %u seq %u, offset %u, consumed %u\n",
                      m_tail_sector, m_tail_seq, m_tail_offset, m_tail_consumed);
    USB_SERIAL_PRINTF("Records: %u flushed + %u in RAM assembly = %u total\n",
                      m_record_count, m_assembly_count, getRecordCount());
    USB_SERIAL_PRINTF("Assembly buffer: %u bytes pending\n", m_assembly_used);
    USB_SERIAL_PRINTF("Sectors in use: %u / %u\n", mapCountInUse(), TOTAL_SECTORS);
    USB_SERIAL_PRINTF("Space: %u used / %u free\n", getUsedSpace(), getFreeSpace());
    USB_SERIAL_PRINTF("Next seq: %u, lifetime program ops: %u\n", m_next_seq, m_write_count);
    USB_SERIAL_PRINTLN("=============================");
}

void FlashRingBuffer::prepareForShutdown() {
    USB_SERIAL_PRINTLN("FlashRingBuffer::prepareForShutdown() - flushing and locking writes");
    if (!m_initialized) {
        return;
    }
    flush();
    savePersistedState();
    m_writes_disabled = true;
    USB_SERIAL_PRINTLN("FlashRingBuffer::prepareForShutdown() - SAFE TO POWER OFF");
}

void FlashRingBuffer::emergencyFlush() {
    if (!m_initialized) {
        return;
    }
    flush();
    savePersistedState();
    USB_SERIAL_PRINTLN("FlashRingBuffer::emergencyFlush() - complete");
}

#define BUILD_INCLUDE_FLASHRINGBUFFER_PART2

#include "FlashRingBuffer_part2.cpp"
