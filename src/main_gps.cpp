#ifdef BUILD_INCLUDE_MAIN_GPS

// u-blox SAM-M10Q Data Sheet

#include "SerialConfig.h"
// --- GPS Module Type Detection ---

enum GpsModuleType {
  GPS_MODULE_UNKNOWN = 0,
  GPS_MODULE_SAM_M10Q,
  GPS_MODULE_MTK_CLONE
};
struct GpsModuleInfo {
  GpsModuleType type;
  String swVersion;
  String hwVersion;
  int protocolVersion;
};

static GpsModuleInfo detectedModule = {GPS_MODULE_UNKNOWN, "", "", 0};

const char* okAck  = "OK - UBLOX - ";
const char* badAck = "BAD - UBLOX - ";

// ---- UBX helper: compute checksum (CK_A, CK_B) over class,id,len,payload ----
void ubxChecksum(uint8_t cls_, uint8_t id_, const uint8_t *payload, uint16_t len, uint8_t &cka, uint8_t &ckb) {
  cka = 0; ckb = 0;
  auto add = [&](uint8_t b){ cka = (uint8_t)(cka + b); ckb = (uint8_t)(ckb + cka); };
  add(cls_); add(id_);
  add((uint8_t)(len & 0xFF)); add((uint8_t)(len >> 8));
  for (uint16_t i=0;i<len;i++) add(payload[i]);
}

// ---- Send UBX and (optionally) wait for ACK-ACK for this (cls,id) ----
bool sendUBX(uint8_t cls_, uint8_t id_, const uint8_t *payload, uint16_t len, const char* ubxCommand, bool waitAck = true) {
  uint8_t cka, ckb;
  ubxChecksum(cls_, id_, payload, len, cka, ckb);

  // Debug: Show command being sent
  BUFFER_LOG_PRINTF("Sending UBX cmd 0x%02X 0x%02X (%s) len=%d: ", cls_, id_, ubxCommand, len);
  for (int i = 0; i < len && i < 20; i++) {  // Show first 20 bytes
    BUFFER_LOG_PRINTF("%02X ", payload[i]);
  }
  if (len > 20) BUFFER_LOG_PRINTF("...");
  BUFFER_LOG_PRINTF(" CK=%02X%02X\n", cka, ckb);

  // UBX header + body
  // 0xB5 0x62 is the U-BLOX Header
  serial_gps.write(0xB5); serial_gps.write(0x62);     // Verified page 134 of u-Blox protocol spec
  serial_gps.write(cls_); serial_gps.write(id_);
  serial_gps.write((uint8_t)(len & 0xFF));
  serial_gps.write((uint8_t)(len >> 8));
  if (len && payload) serial_gps.write(payload, len);
  serial_gps.write(cka); serial_gps.write(ckb);
  serial_gps.flush();

  if (!waitAck) return true;

  // ---- Dynamic timeouts based on command type ----
  uint32_t UBLOX_ACK_TIMEOUT_MS = 50;

  // CFG-VALSET commands need longer timeouts (especially multi-parameter ones)
  if (cls_ == 0x06 && id_ == 0x8A) {
    UBLOX_ACK_TIMEOUT_MS = 100;
    BUFFER_LOG_PRINTF(" [CFG-VALSET detected, using 100ms timeout, instead of 50ms]");
  }

  // Expect: B5 62 05 01 02 00 <cls> <id> CK_A CK_B
  uint8_t buf[10]; uint8_t idx = 0;
  int bytesSkippedForAck = 0;
  int attempts = 0;
  const int maxAttempts = 3;

  while (idx == 0 && attempts < maxAttempts)
  {
    attempts++;
  
    BUFFER_LOG_PRINTF("Waiting for ACK for cmd 0x%02X 0x%02X  (%s) attempt %i...", cls_, id_, ubxCommand,attempts);

    const uint32_t deadline = millis() + UBLOX_ACK_TIMEOUT_MS;

    while (millis() < deadline)
    {
      while (serial_gps.available()) {
        uint8_t b = serial_gps.read();
        if (idx == 0 && b != 0xB5) {
          bytesSkippedForAck++; // count NMEA bytes while looking for ACK
          continue;
        }
        buf[idx++] = b;

        if (idx == sizeof(buf)) {
          // Check for ACK (0x05 0x01)
          bool isAck = (buf[0]==0xB5 && buf[1]==0x62 && buf[2]==0x05 && buf[3]==0x01 &&
                        buf[4]==0x02 && buf[5]==0x00 && buf[6]==cls_ && buf[7]==id_);
          
          // Check for NACK (0x05 0x00)
          bool isNack = (buf[0]==0xB5 && buf[1]==0x62 && buf[2]==0x05 && buf[3]==0x00 &&
                        buf[4]==0x02 && buf[5]==0x00 && buf[6]==cls_ && buf[7]==id_);
          
          if (isAck || isNack) {
            // Verify ACK/NACK checksum before trusting it
            uint8_t expectedCK_A, expectedCK_B;
            ubxChecksum(0x05, isAck ? 0x01 : 0x00, &buf[6], 2, expectedCK_A, expectedCK_B);
            
            if (buf[8] == expectedCK_A && buf[9] == expectedCK_B) {
              // Valid checksum - process ACK/NACK
              if (isAck) {
                BUFFER_LOG_PRINTF(" ACK received %s (skipped %d NMEA bytes)\n", ubxCommand, bytesSkippedForAck);
                BUFFER_LOG_PRINTF("UBX frame accepted - GOOD ACK FOR %s\n", ubxCommand);
                return true;
              } else {
                BUFFER_LOG_PRINTF(" NACK received %s - command rejected (skipped %d NMEA bytes)\n", ubxCommand, bytesSkippedForAck);
                BUFFER_LOG_PRINTF(" UBX frame rejected: ");
                for (int i = 0; i < 10; i++) {
                  BUFFER_LOG_PRINTF("%02X ", buf[i]);
                }
                BUFFER_LOG_PRINTF("\n");
                return false;
              }
            } else {
                // Invalid checksum - slide window and continue searching
                BUFFER_LOG_PRINTF(" Invalid ACK/NACK checksum: got %02X%02X, expected %02X%02X - sliding window\n",
                                buf[8], buf[9], expectedCK_A, expectedCK_B);
            }
          } else {
            // Log other UBX responses we're seeing
            if (buf[0]==0xB5 && buf[1]==0x62) {
                BUFFER_LOG_PRINTF(" Other UBX response: %02X %02X len=%d\n", buf[2], buf[3], buf[4] | (buf[5] << 8));
            }
          }
          
          // Slide window to resync (for all cases: bad checksum, other UBX, garbage)
          memmove(buf, buf+1, idx-1);
          idx--;
        }
      }
    }
  }
  BUFFER_LOG_PRINTF(" TIMEOUT (skipped %d NMEA bytes, no ACK received)\n", bytesSkippedForAck);
  return false; // timeout
}

// --- Check if frame was a UBX-ACK-NAK and print it ---
bool checkUBXNak(uint8_t cls_, uint8_t id_, const uint8_t *payload, uint16_t len)
{
  bool nakMsg = false;
  if (cls_ == 0x05 && id_ == 0x00) { // ACK-NAK
    if (len == 2) {
      uint8_t nakCls = payload[0];
      uint8_t nakId  = payload[1];
      BUFFER_LOG_PRINTF("UBX NAK received for cls=0x%02X id=0x%02X\n",
                        nakCls, nakId);
      nakMsg = true;
    } else {
      BUFFER_LOG_PRINTF("UBX NAK received with unexpected length=%u\n", len);
      for (uint16_t i = 0; i < len; i++) {
        BUFFER_LOG_PRINTF(" %02X", payload[i]);
      }
      BUFFER_LOG_PRINTF("\n");
    }
  }

  return nakMsg;
}

// --- Read a single UBX frame (returns payload and class/id). Verifies checksum. ---
bool readUBXFrame(uint8_t &cls_, uint8_t &id_, uint8_t *payload, uint16_t &len,
                  uint32_t timeout_ms)
{
  enum { HDR0=0xB5, HDR1=0x62 };
  uint32_t deadline = millis() + timeout_ms;
  int bytesSkipped = 0;

  // sync on header
  int state = 0;
  while (millis() < deadline) {
    if (!serial_gps.available()) {
      delay(1); // small delay to avoid busy waiting
      continue;
    }
    uint8_t b = serial_gps.read();

    if (state == 0) {
      if (b == HDR0) {
        state = 1;
      } else {
        bytesSkipped++; // count NMEA bytes we skip
      }
    }
    else if (state == 1) {
      if (b == HDR1) {
          state = 2;
          break;
      } else if (b == HDR0) {
          state = 1;  // ✅ Stay in state 1, potential new sync start
      } else {
          state = 0;  // Reset to looking for 0xB5
          bytesSkipped++;
      }
    }
  }

  if (state != 2) {
    BUFFER_LOG_PRINTF("UBX sync timeout (skipped %d bytes)\n", bytesSkipped);
    return false;
  }

  // read class, id, length (LE) with proper timeout checking
  for (int i = 0; i < 4; i++) {
    while (!serial_gps.available()) {
      if (millis() > deadline) {
        BUFFER_LOG_PRINTF("UBX header timeout at byte %d\n", i);
        return false;
      }
      delay(1);
    }
    uint8_t b = serial_gps.read();
    switch(i) {
      case 0: cls_ = b; break;
      case 1: id_ = b; break;
      case 2: len = b; break; // lenL
      case 3: len |= (uint16_t)b << 8; break; // lenH
    }
  }

  if (len > 400) {  // sanity check
    BUFFER_LOG_PRINTF("UBX length too large: %d\n", len);
    return false;
  }

  // read payload
  for (uint16_t i=0; i<len; ++i) {
    while (!serial_gps.available()) {
      if (millis() > deadline) {
        BUFFER_LOG_PRINTF("UBX payload timeout at byte %d/%d\n", i, len);
        return false;
      }
      delay(1);
    }
    payload[i] = serial_gps.read();
  }

  // read checksum
  uint8_t cka, ckb;
  for (int i = 0; i < 2; i++) {
    while (!serial_gps.available()) {
      if (millis() > deadline) {
        BUFFER_LOG_PRINTF("UBX checksum timeout at byte %d\n", i);
        return false;
      }
      delay(1);
    }
    if (i == 0) cka = serial_gps.read();
    else ckb = serial_gps.read();
  }

  // verify checksum
  uint8_t vcka, vckb;
  ubxChecksum(cls_, id_, payload, len, vcka, vckb);
  bool checksumOk = (cka == vcka && ckb == vckb);

  BUFFER_LOG_PRINTF("UBX frame received: cls=0x%02X id=0x%02X len=%d chk=%s (skipped %d NMEA bytes)\n",
                    cls_, id_, len, checksumOk ? "OK" : "FAIL", bytesSkipped);

  checkUBXNak(cls_, id_, payload, len);

  return checksumOk;
}

// ---- Send UBX VALSET and wait for ACK-ACK specifically for VALSET commands ----
bool sendUBXValSet(uint8_t cls_, uint8_t id_, const uint8_t *payload, uint16_t len, const char* ubxCommand, bool waitAck = true) {
  uint8_t cka, ckb;
  ubxChecksum(cls_, id_, payload, len, cka, ckb);

  // Debug: Show command being sent
  BUFFER_LOG_PRINTF("Sending UBX VALSET cmd 0x%02X 0x%02X (%s) len=%d: ", cls_, id_, ubxCommand, len);
  for (int i = 0; i < len && i < 20; i++) {  // Show first 20 bytes
    BUFFER_LOG_PRINTF("%02X ", payload[i]);
  }
  if (len > 20) BUFFER_LOG_PRINTF("...");
  BUFFER_LOG_PRINTF(" CK=%02X%02X\n", cka, ckb);

  // UBX header + body
  serial_gps.write(0xB5); serial_gps.write(0x62);
  serial_gps.write(cls_); serial_gps.write(id_);
  serial_gps.write((uint8_t)(len & 0xFF));
  serial_gps.write((uint8_t)(len >> 8));
  if (len && payload) serial_gps.write(payload, len);
  serial_gps.write(cka); serial_gps.write(ckb);
  serial_gps.flush();

  if (!waitAck) return true;

  // VALSET commands typically need longer timeouts
  uint32_t UBLOX_ACK_TIMEOUT_MS = 150;
  
  uint8_t buf[10]; uint8_t idx = 0;
  int bytesSkippedForAck = 0;
  int attempts = 0;
  const int maxAttempts = 3;

  while (idx == 0 && attempts < maxAttempts) {
    attempts++;
    BUFFER_LOG_PRINTF("Waiting for VALSET ACK for cmd 0x%02X 0x%02X (%s) attempt %i...", cls_, id_, ubxCommand, attempts);

    const uint32_t deadline = millis() + UBLOX_ACK_TIMEOUT_MS;

    while (millis() < deadline) {
      while (serial_gps.available()) {
        uint8_t b = serial_gps.read();
        if (idx == 0 && b != 0xB5) {
          bytesSkippedForAck++;
          continue;
        }
        buf[idx++] = b;

        if (idx == sizeof(buf)) {
          // Check for both 0x05 (ACK class) and 0x06 (CFG class) ACKs
          bool isAck = ((buf[0]==0xB5 && buf[1]==0x62 && buf[2]==0x05 && buf[3]==0x01) ||
                       (buf[0]==0xB5 && buf[1]==0x62 && buf[2]==0x06 && buf[3]==0x01)) &&
                       buf[4]==0x02 && buf[5]==0x00 && buf[6]==cls_ && buf[7]==id_;
          
          // Check for both 0x05 (ACK class) and 0x06 (CFG class) NACKs
          bool isNack = ((buf[0]==0xB5 && buf[1]==0x62 && buf[2]==0x05 && buf[3]==0x00) ||
                        (buf[0]==0xB5 && buf[1]==0x62 && buf[2]==0x06 && buf[3]==0x00)) &&
                        buf[4]==0x02 && buf[5]==0x00 && buf[6]==cls_ && buf[7]==id_;
          
          if (isAck || isNack) {
            // Verify checksum
            uint8_t expectedCK_A, expectedCK_B;
            uint8_t ackClass = buf[2];
            uint8_t ackId = isAck ? 0x01 : 0x00;
            ubxChecksum(ackClass, ackId, &buf[6], 2, expectedCK_A, expectedCK_B);
            
            if (buf[8] == expectedCK_A && buf[9] == expectedCK_B) {
              if (isAck) {
                BUFFER_LOG_PRINTF(" VALSET ACK received (class 0x%02X) %s (skipped %d NMEA bytes)\n", ackClass, ubxCommand, bytesSkippedForAck);
                return true;
              } else {
                BUFFER_LOG_PRINTF(" VALSET NACK received (class 0x%02X) %s - command rejected (skipped %d NMEA bytes)\n", ackClass, ubxCommand, bytesSkippedForAck);
                return false;
              }
            } else {
              BUFFER_LOG_PRINTF(" Invalid VALSET ACK/NACK checksum: got %02X%02X, expected %02X%02X\n",
                              buf[8], buf[9], expectedCK_A, expectedCK_B);
            }
          }
          
          // Slide window to resync
          memmove(buf, buf+1, idx-1);
          idx--;
        }
      }
    }
  }
  BUFFER_LOG_PRINTF(" VALSET TIMEOUT (skipped %d NMEA bytes, no ACK received)\n", bytesSkippedForAck);
  return false;
}

// ---- Unified function that handles both regular UBX and VALSET commands ----
bool sendUBXUnified(uint8_t cls_, uint8_t id_, const uint8_t *payload, uint16_t len, const char* ubxCommand, bool waitAck = true) {
  uint8_t cka, ckb;
  ubxChecksum(cls_, id_, payload, len, cka, ckb);

  // Debug: Show command being sent
  BUFFER_LOG_PRINTF("Sending UBX cmd 0x%02X 0x%02X (%s) len=%d: ", cls_, id_, ubxCommand, len);
  for (int i = 0; i < len && i < 20; i++) {  // Show first 20 bytes
    BUFFER_LOG_PRINTF("%02X ", payload[i]);
  }
  if (len > 20) BUFFER_LOG_PRINTF("...");
  BUFFER_LOG_PRINTF(" CK=%02X%02X\n", cka, ckb);

  // UBX header + body
  serial_gps.write(0xB5); serial_gps.write(0x62);
  serial_gps.write(cls_); serial_gps.write(id_);
  serial_gps.write((uint8_t)(len & 0xFF));
  serial_gps.write((uint8_t)(len >> 8));
  if (len && payload) serial_gps.write(payload, len);
  serial_gps.write(cka); serial_gps.write(ckb);
  serial_gps.flush();

  if (!waitAck) return true;

  // ---- Dynamic timeouts based on command type ----
  uint32_t UBLOX_ACK_TIMEOUT_MS = 50;
  bool isValSetCommand = (cls_ == 0x06 && (id_ == 0x8A || id_ == 0x8B)); // VALSET or VALGET

  if (isValSetCommand) {
    UBLOX_ACK_TIMEOUT_MS = 150;
    BUFFER_LOG_PRINTF(" [VAL command detected, using 150ms timeout]");
  }

  uint8_t buf[10]; uint8_t idx = 0;
  int bytesSkippedForAck = 0;
  int attempts = 0;
  const int maxAttempts = 3;

  while (idx == 0 && attempts < maxAttempts) {
    attempts++;
    BUFFER_LOG_PRINTF("Waiting for ACK for cmd 0x%02X 0x%02X (%s) attempt %i...", cls_, id_, ubxCommand, attempts);

    const uint32_t deadline = millis() + UBLOX_ACK_TIMEOUT_MS;

    while (millis() < deadline) {
      while (serial_gps.available()) {
        uint8_t b = serial_gps.read();
        if (idx == 0 && b != 0xB5) {
          bytesSkippedForAck++;
          continue;
        }
        buf[idx++] = b;

        if (idx == sizeof(buf)) {
          // For VALSET/VALGET: check both 0x05 (ACK class) and 0x06 (CFG class)
          // For other commands: check only 0x05 (ACK class)
          bool checkCfgClass = isValSetCommand;
          
          bool isAck = (buf[0]==0xB5 && buf[1]==0x62 && buf[2]==0x05 && buf[3]==0x01 &&
                       buf[4]==0x02 && buf[5]==0x00 && buf[6]==cls_ && buf[7]==id_) ||
                      (checkCfgClass && buf[0]==0xB5 && buf[1]==0x62 && buf[2]==0x06 && buf[3]==0x01 &&
                       buf[4]==0x02 && buf[5]==0x00 && buf[6]==cls_ && buf[7]==id_);
          
          bool isNack = (buf[0]==0xB5 && buf[1]==0x62 && buf[2]==0x05 && buf[3]==0x00 &&
                        buf[4]==0x02 && buf[5]==0x00 && buf[6]==cls_ && buf[7]==id_) ||
                       (checkCfgClass && buf[0]==0xB5 && buf[1]==0x62 && buf[2]==0x06 && buf[3]==0x00 &&
                        buf[4]==0x02 && buf[5]==0x00 && buf[6]==cls_ && buf[7]==id_);
          
          if (isAck || isNack) {
            // Verify checksum
            uint8_t expectedCK_A, expectedCK_B;
            uint8_t ackClass = buf[2];
            uint8_t ackId = isAck ? 0x01 : 0x00;
            ubxChecksum(ackClass, ackId, &buf[6], 2, expectedCK_A, expectedCK_B);
            
            if (buf[8] == expectedCK_A && buf[9] == expectedCK_B) {
              if (isAck) {
                BUFFER_LOG_PRINTF(" ACK received (class 0x%02X) %s (skipped %d NMEA bytes)\n", ackClass, ubxCommand, bytesSkippedForAck);
                BUFFER_LOG_PRINTF("UBX frame accepted - GOOD ACK FOR %s\n", ubxCommand);
                return true;
              } else {
                BUFFER_LOG_PRINTF(" NACK received (class 0x%02X) %s - command rejected (skipped %d NMEA bytes)\n", ackClass, ubxCommand, bytesSkippedForAck);
                BUFFER_LOG_PRINTF(" UBX frame rejected: ");
                for (int i = 0; i < 10; i++) {
                  BUFFER_LOG_PRINTF("%02X ", buf[i]);
                }
                BUFFER_LOG_PRINTF("\n");
                return false;
              }
            } else {
              BUFFER_LOG_PRINTF(" Invalid ACK/NACK checksum: got %02X%02X, expected %02X%02X - sliding window\n",
                              buf[8], buf[9], expectedCK_A, expectedCK_B);
            }
          } else {
            // Log other UBX responses we're seeing
            if (buf[0]==0xB5 && buf[1]==0x62) {
              BUFFER_LOG_PRINTF(" Other UBX response: %02X %02X len=%d\n", buf[2], buf[3], buf[4] | (buf[5] << 8));
            }
          }
          
          // Slide window to resync
          memmove(buf, buf+1, idx-1);
          idx--;
        }
      }
    }
  }
  BUFFER_LOG_PRINTF(" TIMEOUT (skipped %d NMEA bytes, no ACK received)\n", bytesSkippedForAck);
  return false;
}


// =================== Configuration payloads ===================

// --- Minimal struct for CFG-PRT decoded data (UART style) ---
struct UbxPortCfg {
  uint8_t  portID;        // 1 = UART1
  uint32_t baud;          // little-endian in payload
  uint16_t inProtoMask;   // bit0=UBX, bit1=NMEA
  uint16_t outProtoMask;  // bit0=UBX, bit1=NMEA
};

// UBX-CFG-PRT poll message (no payload)
const uint8_t UBX_CFG_PRT_POLL[] = { 
  0xB5,0x62, 
  0x06,0x00, 
  0x00,0x00, 
  0x06,0x18 };

  // Set to 115000 baud
// Build a UBX-CFG-VALSET that writes to RAM (+ optionally BBR/Flash)
// Header: version=0x00, layers=RAM(0x01) [or 0x07 for RAM|BBR|Flash], 2 reserved bytes
uint8_t UBX_CFG_BAUD_SET_VALSET[] = {
  0x00, 0x01, 0x00, 0x00,                             // version, layers, rsvd
  0x01,0x00,0x52,0x40,    // key 0x40520001
  0x00,0xC2,0x01,0x00     // value 115200
};

// 4) CFG-RATE (0x06 0x08) — len=6: [measRateLE][navRateLE][timeRefLE]
// measRate=1000ms (E8 03), navRate=1, timeRef=GPS
const uint8_t CFG_RATE_1HZ[6] = { 0xE8,0x03, 0x01,0x00, 0x01,0x00 };


// UBX-CFG-INFMSG-NMEA_UART1_VALSET (0x06 0x8A)
// header: version=0x00, layers=0x01 (RAM), rsvd[2]=0
uint8_t UBX_CFG_INFMSG_NMEA_UART1_VALSET[] = {
  0x00, 0x01, 0x00, 0x00,   // version 0, layers=0x01(RAM), 2 bytes reserved
  0x07, 0x00, 0x92, 0x20,   // key 0x20920007 (little-endian): 20 92 00 07
  0x00                      // value - disable
};

// UBX-CFG-VALSET (0x06 0x8A)
// header: version=0x00, layers=0x01 (RAM), rsvd[2]=0
uint8_t UBX_CFG_PEDESTRIAN_MODEL_VALSET[] = {
  0x00, 0x01, 0x00, 0x00,   // version 0, layers=0x01(RAM), 2 bytes reserved
  // key 0x20110021 (little-endian): 21 00 11 20, value E1=0x03 (Pedestrian)
  0x21, 0x00, 0x11, 0x20, 0x03
};

// UBX-CFG-VALSET (0x06 0x8A)
// header: version=0x00, layers=0x01 (RAM), rsvd[2]=0
// Emit no fix messages when there is no fix, instead of no message at all
// Simplified NMEA configuration - just disable position filtering
uint8_t UBX_CFG_NMEA_EMIT_NO_FIX[] = {
  0x00, 0x01, 0x00, 0x00,             // version=0, layers=0x01(RAM), 2 bytes reserved
  // Disable position filtering (allow invalid positions through)
  0x22, 0x00, 0x93, 0x10, 0x00        // CFG-NMEA-FILT_POS = false (key 0x10930022)
};

// UBX-CFG-VALSET (0x06 0x8A)
// header: version=0x00, layers=0x01 (RAM), rsvd[2]=0
uint8_t UBX_CFG_RATE_VALSET[] = {
  0x00, 0x01, 0x00, 0x00,   // version 0, layers=0x01(RAM), 2 bytes reserved
  0x01, 0x00, 0x21, 0x30,   // key 0x30210001 (little-endian):
  0xE3, 0x03, 0x00, 0x00    // value - 1 Hz
};

// UBX-CFG-RST (0x06 0x04)
// Cold Start - Clear all aiding data (ephemeris, almanac, position, time)
uint8_t UBX_CFG_RST_COLDSTART[] = {
  0xFF, 0xFF,  // navBbrMask = 0xFFFF (clear all BBR data)
  0x02,        // resetMode = 0x02 (controlled GNSS restart)
  0x00         // reserved1 = 0x00 
};

// UBX-CFG-RST (0x06 0x04)
// Warm start: clear only ephemeris
uint8_t UBX_CFG_RST_WARMSTART[] = {
  0x01, 0x00,  // navBbrMask = 0x0001 (clear eph only)
  0x02,        // resetMode  = 0x02 (controlled GNSS restart)
  0x00        // reserved1  = 0x00
};

// HOT START - Keep everything, just restart
uint8_t UBX_CFG_RST_HOTSTART[] = {
    0x00, 0x00, // navBbrMask = 0x0000 (clear nothing)
    0x02,       // resetMode = 0x02 (controlled GNSS restart)
    0x00
};

// Set 20 satellites for fix 
uint8_t UBX_CFG_VALSET_INFIL_MINSV_20[] = {
  0x00, 0x01, 0x00, 0x00, // version=0, layer=BBR(0x01), rsvd[2]
  0xA1, 0x00, 0x11, 0x20, // key = 0x201100a1 (CFG-NAVSPG-INFIL_MINSVS)
  0x14,                   // value = 20 satellites
};

// Set 3 satellites for fix 
uint8_t UBX_CFG_VALSET_INFIL_MINSV_3[] = {
  0x00, 0x01, 0x00, 0x00, // version=0, layer=BBR(0x01), rsvd[2]
  0xA1, 0x00, 0x11, 0x20, // key = 0x201100a1 (CFG-NAVSPG-INFIL_MINSVS)
  0x3,                   // value = 3
};

// RAW - get number of satellites for fix
uint8_t EX_UBX_CFG_VALGET_INFIL_MINSV[] = {
  0xB5, 0x62,             // Sync chars
  0x06, 0x8B,             // Class=CFG(0x06), ID=VALGET(0x8B)
  0x08, 0x00,             // Length = 8 bytes

  // Payload (8 bytes)
  0x00, 0x00, 0x00, 0x00, // version=0, layer=0x00 RAM, rsvd[2]
  0xA1, 0x00, 0x11, 0x20, // key = 0x201100a1 (CFG-NAVSPG-INFIL_MINSVS)
  0x6B, 0x57              // CK_A, CK_B
};

// Get number of satellites for fix
uint8_t UBX_CFG_VALGET_INFIL_MINSV[] = {
  0x06, 0x8B,             // Class=CFG(0x06), ID=VALGET(0x88)
  0x08, 0x00,             // Length = 8 bytes

  // Payload (8 bytes)
  0x00, 0x00, 0x00, 0x00, // version=0, layer=0x00 RAM, rsvd[2]
  0xA1, 0x00, 0x11, 0x20, // key = 0x201100a1 (CFG-NAVSPG-INFIL_MINSVS)
};

// UBX-CFG-VALSET (0x06 0x8B)
uint8_t UBX_CFG_SIGNAL_SBAS_ENA_ENABLE[] = {
    0x00, 0x1, 0x00, 0x00, // version=0, layers=RAM, reserved
    0x20, 0x00, 0x31, 0x10, // key = 0x10310020 (CFG-SIGNAL-SBAS_ENA)
    0x01                    // value = 1 (enable SBAS) ← MISSING FROM YOUR ARRAY
};

// UBX-CFG-VALGET (0x06 0x8B)
// Get SBAS enable status from Default (RAM)
uint8_t EX_UBX_CFG_VALGET_SBAS_ENA[] = {
    0xB5, 0x62,             // Sync chars
    0x06, 0x8B,             // Class=CFG(0x06), ID=VALGET(0x8B)
    0x08, 0x00,             // Length = 8 bytes
    // Payload (8 bytes)
    0x00,                   // version = 0
    0x00,                   // layer = 0x00 RAM for SAM-M10Q

    0x00, 0x00,             // position = 0 (reserved)
    0x20, 0x00, 0x31, 0x10, // key = 0x10310020 (CFG-SIGNAL-SBAS_ENA)
    0xFA, 0x83              // Checksum
};
//    0x07,                   // layer = default (will default to Ram for SAM-M10Q)
//    0x01, 0xB4              // Checksum for 0x07 layer

// UBX-MON-VER poll message (no payload)
const uint8_t UBX_MON_VER_POLL[] = { 
  0xB5,0x62, 
  0x0A,0x04, 
  0x00,0x00, 
  0x0E,0x34 };

const int maxPayloadSize = 400;
uint8_t payload[maxPayloadSize];  // MON-VER can be quite long - keep array off the heap

// --- Poll MON-VER to get firmware version ---
bool pollMON_VER(uint32_t timeout_ms = 1000)
{
  // UBX-MON-VER poll message (no payload)
  while (serial_gps.available()) serial_gps.read();  // drain old bytes
  serial_gps.write(UBX_MON_VER_POLL, sizeof(UBX_MON_VER_POLL));
  serial_gps.flush();

  // Read frames until timeout; collect class=0x0A id=0x04
  uint8_t cls,id;
  uint16_t len;

  uint32_t endBy = millis() + timeout_ms;
  while (millis() < endBy) {
    if (!readUBXFrame(cls, id, payload, len, /*timeout per frame*/ 500)) break;
    if (cls == 0x0A && id == 0x04) {
      BUFFER_LOG_PRINTF("MON-VER response: len=%d bytes\n", len);

      // Show raw payload for debugging
      BUFFER_LOG_PRINTF("Raw payload: ");
      for (int i = 0; i < len && i < 40; i++) {
        BUFFER_LOG_PRINTF("%02X ", payload[i]);
      }
      BUFFER_LOG_PRINTF("\n");

      // MON-VER structure: swVersion[30] + hwVersion[10] + extension strings
      char swVer[31] = {0};
      char hwVer[11] = {0};

      if (len > maxPayloadSize) {
        BUFFER_LOG_PRINTF("UBX payload too large: %d > %d\n", len, maxPayloadSize);
        return false;
      }

      if (len >= 30) {
        // Extract software version (first 30 bytes)
        memcpy(swVer, payload, 30);
        swVer[30] = '\0';  // ensure null terminated

        if (len >= 40) {
          // Extract hardware version (next 10 bytes)
          memcpy(hwVer, payload + 30, 10);
          hwVer[10] = '\0';  // ensure null terminated
          BUFFER_LOG_PRINTF("GPS Firmware: SW=[%s] HW=[%s]\n", swVer, hwVer);
        } else {
          BUFFER_LOG_PRINTF("GPS Firmware: SW=[%s] (HW version unavailable - short response)\n", swVer);
        }

        String extensions;

        if (len > 40) {
          extensions.reserve(len - 40 + 8); // a little extra
          for (int i = 40; i < len; i += 30) {
            char ext[31] = {0};
            int copyLen = (len - i < 30) ? (len - i) : 30;
            memcpy(ext, payload + i, copyLen);
            ext[30] = '\0';
            extensions += String(ext);
            if (i + 30 < len) extensions += " "; // separator
          }
        }

        BUFFER_LOG_PRINTF("Extensions: %s\n", extensions.c_str());

        // Detect module type based on firmware version
        detectedModule.swVersion = String(swVer);
        detectedModule.hwVersion = String(hwVer);

        if (strstr(extensions.c_str(), "SAM-M10Q"))
        {
          detectedModule.type = GPS_MODULE_SAM_M10Q;
          BUFFER_LOG_PRINTF("Module Type: SAM M10Q\n");
          detectedModule.protocolVersion = 34;
        }
        else if (strstr(swVer, "T3,RomFw")) {
          detectedModule.type = GPS_MODULE_MTK_CLONE;
          BUFFER_LOG_PRINTF("Module Type: MTK Clone (fake u-blox)\n");
          detectedModule.protocolVersion = 23;
        } 
        else 
        {
          detectedModule.type = GPS_MODULE_UNKNOWN;
          BUFFER_LOG_PRINTF("Module Type: Unknown - please check compatibility\n");
        }

        return true;
      } else {
        BUFFER_LOG_PRINTF("MON-VER response too short: %d bytes (need at least 30)\n", len);
        return false;
      }
    }
  }
  return false;
}

// --- Poll CFG-PRT and collect up to maxPorts port records. Returns true if any. ---
bool pollCFG_PRT(UbxPortCfg *ports, size_t maxPorts, size_t &count, uint32_t timeout_ms = 300)
{
  count = 0;

  // UBX-CFG-PRT poll message (no payload)
  while (serial_gps.available()) serial_gps.read();  // drain old bytes
  serial_gps.write(UBX_CFG_PRT_POLL, sizeof(UBX_CFG_PRT_POLL));
  serial_gps.flush();

  // Read frames until timeout gap; collect only class=0x06 id=0x00 with len=20
  uint8_t cls,id;
  uint8_t payload[32];
  uint16_t len;

  uint32_t endBy = millis() + timeout_ms;
  while (millis() < endBy) {
    if (!readUBXFrame(cls, id, payload, len, /*timeout per frame*/ 100)) break;
    if (cls == 0x06 && id == 0x00 && len == 20) {
      if (count < maxPorts) {
        UbxPortCfg &o = ports[count++];
        o.portID = payload[0];

        // Debug: dump raw payload bytes
        BUFFER_LOG_PRINTF("Raw CFG-PRT payload (%d bytes): ", len);
        for (int i = 0; i < len; i++) {
          BUFFER_LOG_PRINTF("%02X ", payload[i]);
        }
        BUFFER_LOG_PRINTF("\n");

        // Parse CFG-PRT based on detected module type
        o.baud = (uint32_t)payload[8] | ((uint32_t)payload[9]<<8) | ((uint32_t)payload[10]<<16) | ((uint32_t)payload[11]<<24);
        o.inProtoMask  = (uint16_t)payload[12] | ((uint16_t)payload[13]<<8);
        o.outProtoMask = (uint16_t)payload[14] | ((uint16_t)payload[15]<<8);

        BUFFER_LOG_PRINTF("CFG-PRT: portID=%d baud=%lu in=0x%04X out=0x%04X\n",
                         o.portID, o.baud, o.inProtoMask, o.outProtoMask);
        BUFFER_LOG_PRINTF("  Expected: in=0x0003 (UBX+NMEA) out=0x0002 (NMEA) or 0x0003 (UBX+NMEA)\n");

        if (o.inProtoMask == 0x0000 && o.outProtoMask == 0x0000) {
          BUFFER_LOG_PRINTF("  WARNING: Both protocol masks are 0 - GPS may need CFG-PRT configuration\n");
        }
      }
      // extend endBy slightly to allow more port records in the same burst
      endBy = millis() + 40;
    }
    // ignore unrelated frames (e.g., NMEA or other UBX) and keep reading until gap
  }

  return (count > 0);
}

// Returns true if payload parsed. keyOut/valueOut are valid on success.
bool parseValgetPayload(const uint8_t* payload, size_t len,
                        uint32_t& keyOut, uint32_t& valueOut) {
    if (!payload || len < 9) return false;  // Min 9 bytes for v1 response
    
    if (payload[0] == 0x01) {  // Version 1 response format
        keyOut = (uint32_t)payload[4] | ((uint32_t)payload[5] << 8) |
                 ((uint32_t)payload[6] << 16) | ((uint32_t)payload[7] << 24);
        valueOut = payload[8];  // Single byte value
        return true;
    }
    return false;  // Version 0 shouldn't appear in responses
}

constexpr uint32_t KEY_SBAS_ENA = 0x10310020;

// Example: check SBAS enable
bool isSbasEnabledFromValget(bool& enabled, const uint8_t* payload, size_t len)
{
  enabled = false;

  uint32_t key, val;  
  if (!parseValgetPayload(payload, len, key, val)) 
  {
    BUFFER_LOG_PRINTF("isSbasEnabledFromValget: failed to parseValgetPayload\n");
    return false; // failed to parse reply payload
  }

  if (key != KEY_SBAS_ENA) 
  {
    BUFFER_LOG_PRINTF("isSbasEnabledFromValget: Valget payload has wrong key\n");
    return false;   // wrong key in reply
  }

  enabled = (val & 0x01u) != 0;               // 1 = enabled, 0 = disabled

  BUFFER_LOG_PRINTF("isSbasEnabledFromValget: Good response message. SBAS is %s\n", enabled ? "enabled" : "disabled");

  return true;
}

bool pollCFG_SBAS(bool& enabled, uint32_t timeout_ms = 300)
{
  bool responseError = true;
  enabled = false;

  while (serial_gps.available()) serial_gps.read();  // drain old bytes
  serial_gps.write(EX_UBX_CFG_VALGET_SBAS_ENA, sizeof(EX_UBX_CFG_VALGET_SBAS_ENA));
  serial_gps.flush();

  BUFFER_LOG_PRINTF("pollCFG_SBAS: Send Get SBAS status request\n");

  // Read frames until timeout gap; collect only class=0x06 id=0x00 with len=20
  uint8_t cls,id;
  uint8_t payload[32];
  uint16_t len;

  uint32_t endBy = millis() + timeout_ms;
  while (millis() < endBy) {
    if (!readUBXFrame(cls, id, payload, len, /*timeout per frame*/ 100)) 
    {
      BUFFER_LOG_PRINTF("pollCFG_SBAS: readUBXFrame returned false\n");
      break;
    }

    if (cls == 0x06 && id == 0x8B && len == 9) 
    {
      responseError = !isSbasEnabledFromValget(enabled, payload,len);
      break;
    }
    else
    {
      BUFFER_LOG_PRINTF("pollCFG_SBAS: Unexpected ubx message response: class 0x%02X id 0x%02X len=%d, expected: class=0x06 id=0x8B len=9\nUnexpected Response (without checksum): ",cls,id,len);

      int len0 = len & 0x00FF;
      int len1 = len >> 8;
      BUFFER_LOG_PRINTF("0xB5 0x62 0x%02X 0x%02X 0x%02X 0x%02X ",cls,id,len0,len1);
      for (int i=0; i<len; i++)
        BUFFER_LOG_PRINTF("0x%02X ",payload[i]);

      BUFFER_LOG_PRINTF("\nFrom sent message: ");

      for (int i=0; i<sizeof(EX_UBX_CFG_VALGET_SBAS_ENA); i++)
        BUFFER_LOG_PRINTF("0x%02X ",EX_UBX_CFG_VALGET_SBAS_ENA[i]);

      BUFFER_LOG_PRINTF("\n");

      if (checkUBXNak(cls, id, payload, len))
        responseError = true;
    }
    // ignore unrelated frames (e.g., NMEA or other UBX) and keep reading until gap
  }
  
  BUFFER_LOG_PRINTF("pollCFG_SBAS: Parsing of response %s\n",responseError ? "BAD" : "OK");

  if (!responseError)
    BUFFER_LOG_PRINTF("pollCFG_SBAS: SBAS is %s\n",enabled ? "enabled" : "disabled");

  return !responseError;
}

constexpr uint32_t KEY_MINSVS_ENA = 0x201100a1;

bool getMinSatCountFromValget(int& satCount, const uint8_t* payload, size_t len)
{
  satCount = -1;

  uint32_t key, val;  
  if (!parseValgetPayload(payload, len, key, val)) 
  {
    BUFFER_LOG_PRINTF("getMinSatCountFromValget: failed to parseValgetPayload\n");
    return false; // failed to parse reply payload
  }

  if (key != KEY_MINSVS_ENA) 
  {
    BUFFER_LOG_PRINTF("getMinSatCountFromValget: Valget payload has wrong key\n");
    return false;   // wrong key in reply
  }

  satCount = val;

  BUFFER_LOG_PRINTF("getMinSatCountFromValget: Good response message. satCount is %d\n", satCount);

  return true;
}

bool pollCFG_MinSatellites(int& satCount, uint32_t timeout_ms = 3000)
{
  bool responseError = true;
  satCount = -1;

  while (serial_gps.available()) serial_gps.read();  // drain old bytes
  serial_gps.write(EX_UBX_CFG_VALGET_INFIL_MINSV, sizeof(EX_UBX_CFG_VALGET_INFIL_MINSV));
  serial_gps.flush();

  BUFFER_LOG_PRINTF("pollCFG_MinSatellites: Send Get MINSV status request\n");

  // Read frames until timeout gap; collect only class=0x06 id=0x00 with len=20
  uint8_t cls,id;
  uint8_t payload[32];
  uint16_t len;

  uint32_t endBy = millis() + timeout_ms;
  while (millis() < endBy) {
    if (!readUBXFrame(cls, id, payload, len, 1000)) // timeout per frame = 1000ms
    {
      BUFFER_LOG_PRINTF("pollCFG_MinSatellites: readUBXFrame returned false\n");
      break;
    }

    // ADD THIS: Better logging of what we received
    BUFFER_LOG_PRINTF("Received frame: cls=0x%02X id=0x%02X len=%d\n", cls, id, len);
  
    // This class and id is the CFG-VALGET response message
    if (cls == 0x06 && id == 0x8B && len >= 9) 
    {
      responseError = !getMinSatCountFromValget(satCount, payload,len);
      break;
    }
    else if (cls == 0x06 && id == 0x00) // CFG-NAK
    {
        if (len >= 2) {
            BUFFER_LOG_PRINTF("pollCFG_MinSatellites: Received CFG-NAK for class 0x%02X id 0x%02X\n", payload[0], payload[1]);
        } else {
            BUFFER_LOG_PRINTF("pollCFG_MinSatellites: Received CFG-NAK (malformed, len=%d)\n", len);
        }
        responseError = true;
        break;
    }
    else if (cls == 0x06 && id == 0x01) // CFG-ACK
    {
        if (len >= 2) {
            BUFFER_LOG_PRINTF("pollCFG_MinSatellites: Received unexpected CFG-ACK for class 0x%02X id 0x%02X\n", payload[0], payload[1]);
        }
        // Continue waiting for actual VALGET response
    }
    else
    {
      BUFFER_LOG_PRINTF("pollCFG_MinSatellites: Unexpected ubx message response: class 0x%02X id 0x%02X len=%d, expected: class=0x06 id=0x8B len=9\nUnexpected Response (without checksum): ",cls,id,len);

      int len0 = len & 0x00FF;
      int len1 = len >> 8;
      BUFFER_LOG_PRINTF("0xB5 0x62 0x%02X 0x%02X 0x%02X 0x%02X ",cls,id,len0,len1);
      for (int i=0; i<len; i++)
        BUFFER_LOG_PRINTF("0x%02X ",payload[i]);

      BUFFER_LOG_PRINTF("\nFrom sent message: ");

      for (int i=0; i<sizeof(EX_UBX_CFG_VALGET_INFIL_MINSV); i++)
        BUFFER_LOG_PRINTF("0x%02X ",EX_UBX_CFG_VALGET_INFIL_MINSV[i]);

      BUFFER_LOG_PRINTF("\n");

      if (checkUBXNak(cls, id, payload, len))
        responseError = true;
    }
    // ignore unrelated frames (e.g., NMEA or other UBX) and keep reading until gap
  }
  
  // ADD THIS: Better timeout messaging
  if (millis() >= endBy) {
      BUFFER_LOG_PRINTF("pollCFG_MinSatellites: Timeout occurred after %dms, no valid response received\n", timeout_ms);
  }
    
  BUFFER_LOG_PRINTF("pollCFG_MinSatellites: Parsing of response %s\n",responseError ? "BAD" : "OK");

  if (!responseError)
    BUFFER_LOG_PRINTF("pollCFG_MinSatellites: Minimum visible satellites for fix is %d\n",satCount);
  else
    BUFFER_LOG_PRINTF("pollCFG_MinSatellites: Coult not get Minimum visible satellites for fix\n");

  return !responseError;
}

// ------------------- Module-specific command helpers -------------------
bool sendCFG_MSG_enable(uint8_t msgClass, uint8_t msgId, const char* msgName, int retry=0) {
  int attempt=0;
  while (retry >= 0)
  {
    retry--;
    attempt++;
    if (detectedModule.type == GPS_MODULE_SAM_M10Q) {
    // ZED-F9R: 8-byte format, rate1=UART1
      uint8_t payload[8] = { msgClass, msgId, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };
      bool result = sendUBX(0x06, 0x01, payload, 8,msgName);
      BUFFER_LOG_PRINTF("%s - enable %s - (attempt %i)\n", result ? okAck : badAck,msgName, attempt);
      if (result)
        return result;
    } else {
      // Other u-blox series: 3-byte format
      uint8_t payload[3] = { msgClass, msgId, 0x01 };
      bool result = sendUBX(0x06, 0x01, payload, 3,msgName);
      BUFFER_LOG_PRINTF("%s - enable %s - (attempt %i)\n", result ? okAck : badAck,msgName, attempt);
      if (result)
        return result;
    }
    delay(100);
  }
  return false;
}

bool sendCFG_MSG_disable(uint8_t msgClass, uint8_t msgId, const char* msgName, int retry=0) {
  int attempt=0;
  while (retry >= 0)
  {
    retry--;
    attempt++;
    
    if (detectedModule.type == GPS_MODULE_SAM_M10Q) {
      // ZED-F9R: 8-byte format, all rates = 0
      uint8_t payload[8] = { msgClass, msgId, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
      bool result = sendUBX(0x06, 0x01, payload, 8,msgName);
      BUFFER_LOG_PRINTF("%s - disable %s - (attempt %i)\n", result ? okAck : badAck,msgName, attempt);
      if (result)
        return result;
    } else {
      // Other u-blox series: 3-byte format
      uint8_t payload[3] = { msgClass, msgId, 0x00 };
      bool result = sendUBX(0x06, 0x01, payload, 3,msgName);
      BUFFER_LOG_PRINTF("%s - disable %s - (attempt %i)\n", result ? okAck : badAck,msgName, attempt);
      if (result)
        return result;
    }
    delay(100);
  }

  return false;
}

// ------------------- Apply configuration -------------------
bool configureUBLOXGps() 
{
  bool ok = true;

  const int maxAttempts=5;

  bool MON_VER_ok = false;
  bool try115200 = true;
  bool baud_ok = false;
  bool skip_later_baud_change = false;

  while (!MON_VER_ok || try115200)
  {
    int attempts = 0;
    while (attempts < maxAttempts)
    {
      BUFFER_LOG_PRINTF("Poll MON-VER (firmware version) attempt %i\n",attempts);
      MON_VER_ok = pollMON_VER();

      if (!MON_VER_ok) 
      {
        BUFFER_LOG_PRINTLN("Failed to get GPS firmware version at default Serial port baud");
        attempts++;
        delay(50);
      }
      else
      {
        MON_VER_ok = true;
        ok &= MON_VER_ok;
    //    BUFFER_LOG_PRINTF("After doing pollMon_Ver the 'ok' combined result is %d\n", ok);
        break;
      }
    }

    if (try115200)
    {
      // set baud rate to 115200 (it defaults to 9600 and it has no flash so needs replying every boot)
      bool baud_ok = false;
      baud_ok = sendUBX(0x06, 0x8A, UBX_CFG_BAUD_SET_VALSET, sizeof(UBX_CFG_BAUD_SET_VALSET),"SET BAUD TO 115200",false);
      delay(100);
      BUFFER_LOG_PRINTF("%s Set Baud Rate to 115200\n", (baud_ok ? okAck : badAck));
      try115200 = false;
      if (baud_ok)
      {
        serial_gps.end();
        delay(100);
        // UART1 for receiving data from GPS
        serial_gps.setRxBufferSize(GPS_RX_BUFFER_SIZE); // must set before begin
        serial_gps.begin(115200, SERIAL_8N1, GPS_RX_WHITE_GPIO, GPS_TX_GREY_GPIO);
        skip_later_baud_change = true;
      }
    }
    else
    {
      break;
    }
  }

  int attempts=0;

  UbxPortCfg ports[6];
  size_t n=0;

  bool gpsAt115200 = false;
  while (attempts < maxAttempts)
  {
    BUFFER_LOG_PRINTF("Poll CFG-PRT Attempt %i\n",attempts);
    if (pollCFG_PRT(ports, 6, n)) {
      BUFFER_LOG_PRINTF("Got %i port configs\n",n); 

      for (size_t i=0; i<n; ++i) {
        UbxPortCfg p = ports[i];
        // UART1
        // Expect defaults: inProtoMask 0x0003 (UBX+NMEA), outProtoMask 0x0002 (NMEA)
        // baud is whatever it's set to (e.g., 9600 or 115200)
        BUFFER_LOG_PRINTF("portID=%i baud=%i in=0x%04X out=0x%04X\n",p.portID, p.baud, p.inProtoMask, p.outProtoMask);
        if (p.baud == 115200)
        {
          // Assume Lemon has been rebooted and the GPS still retains previous configuration in RAM.
          gpsAt115200 = true;          
        }
      }
      break;
    }
    else
    {
      BUFFER_LOG_PRINTLN("Got 0 port configs"); 
      attempts++;
      delay(50);
    }
  }

    if (detectedModule.type == GPS_MODULE_SAM_M10Q)
    {
      // set baud rate to 115200 (it defaults to 9600 and it has no flash so needs replying every boot)
      bool baud_ok = true;
      if (!gpsAt115200)
      {
        baud_ok = sendUBX(0x06, 0x8A, UBX_CFG_BAUD_SET_VALSET, sizeof(UBX_CFG_BAUD_SET_VALSET),"SET BAUD TO 115200",false);
        delay(100);
        BUFFER_LOG_PRINTF("%s Set Baud Rate to 115200\n", (baud_ok ? okAck : badAck));
      }

      bool pollCFG_PRT_Result = false;

      if (baud_ok)
      {
        serial_gps.end();
        delay(100);
        // UART1 for receiving data from GPS
        serial_gps.setRxBufferSize(GPS_RX_BUFFER_SIZE); // must set before begin
        serial_gps.begin(115200, SERIAL_8N1, GPS_RX_WHITE_GPIO, GPS_TX_GREY_GPIO);

        attempts=0;
        
        while (attempts < maxAttempts)
        {
          BUFFER_LOG_PRINTF("Poll CFG-PRT Attempt %i\n",attempts);
          pollCFG_PRT_Result = pollCFG_PRT(ports, 6, n);

          if (pollCFG_PRT_Result) {
            BUFFER_LOG_PRINTF("Got %i port configs\n",n); 

            for (size_t i=0; i<n; ++i) {
              UbxPortCfg p = ports[i];
              // UART1
              // Expect defaults: inProtoMask 0x0003 (UBX+NMEA), outProtoMask 0x0002 (NMEA)
              // baud is whatever it's set to (e.g., 9600 or 115200)
              BUFFER_LOG_PRINTF("portID=%i baud=%i in=0x%04X out=0x%04X\n",p.portID, p.baud, p.inProtoMask, p.outProtoMask);
            }
            break;
          }
          else
          {
            BUFFER_LOG_PRINTLN("Got 0 port configs"); 
            attempts++;
            delay(50);
          }
        }
      }

      ok &= pollCFG_PRT_Result;
  //    BUFFER_LOG_PRINTF("After doing pollCFG_PRT the 'ok' combined result is %d\n", ok);
    }

  // Configure NMEA messages based on detected module type
  if (detectedModule.type == GPS_MODULE_MTK_CLONE) 
  {
    BUFFER_LOG_PRINTF("%s MTK clone detected - skipping UBX configuration (NMEA works by default)\n", okAck);
  } 
  else 
  {
    // Allow NMEA output even when there is no fix (genuine u-blox modules)
    // Stop the suppression of No Fix GGA/RMC messages
    // Normally they are simply not sent, resulting in a gap in messages.    
    bool send_no_fix_result = sendUBX(0x06, 0x8A, UBX_CFG_NMEA_EMIT_NO_FIX, sizeof(UBX_CFG_NMEA_EMIT_NO_FIX),"ENABLE NO FIX MSGS");
    ok &= send_no_fix_result;
    BUFFER_LOG_PRINTF("%s Allow No Fix messages (GGA/RMC)\n", (send_no_fix_result ? okAck : badAck));

//    BUFFER_LOG_PRINTF("After setting no fix messages to send, the 'ok' combined result is %d\n", ok);

    // Configure NMEA message rates using module-specific format - 3 retries on failure
    ok &= sendCFG_MSG_enable(0xF0, 0x00,  "GGA",3);  // Enable GGA
    ok &= sendCFG_MSG_enable(0xF0, 0x04,  "RMC",3);  // Enable RMC
    ok &= sendCFG_MSG_disable(0xF0, 0x01, "GLL",3);  // Disable GLL
    ok &= sendCFG_MSG_disable(0xF0, 0x02, "GSA",3);  // Disable GSA
    ok &= sendCFG_MSG_disable(0xF0, 0x03, "GSV",3);  // Disable GSV
    ok &= sendCFG_MSG_disable(0xF0, 0x05, "VTG",3);  // Disable VTG
    ok &= sendCFG_MSG_disable(0xF0, 0x08, "ZDA",3);  // Disable ZDA    

    // Set navigation rate = 1 Hz - ZED-9FR and SAM-M10Q only
    bool rate_ok = sendUBX(0x06, 0x8A, UBX_CFG_RATE_VALSET, sizeof(UBX_CFG_RATE_VALSET),"SET TO 1HZ");
    BUFFER_LOG_PRINTF("%s Set nav Rate to 1 Hz\n", (rate_ok ? okAck : badAck));
    ok &= rate_ok;

    BUFFER_LOG_PRINTF("After setting nav rate the 'ok' combined result is %d\n", ok);

    // Set dynamic model to Sea - ZED-9FR and SAM-M10Q only
//    bool model_ok = sendUBX(0x06, 0x8A, UBX_CFG_SEA_MODEL_VALSET, sizeof(UBX_CFG_SEA_MODEL_VALSET),"DYNAMIC MODEL = SEA");
//    BUFFER_LOG_PRINTF("%s Set dynamic model to 'Sea'\n", (model_ok ? okAck : badAck));

    // Set dynamic model to Pedestrian  - ZED-9FR and SAM-M10Q only
    bool ped_ok = sendUBX(0x06, 0x8A, UBX_CFG_PEDESTRIAN_MODEL_VALSET, sizeof(UBX_CFG_PEDESTRIAN_MODEL_VALSET),"DYNAMIC MODEL = PEDESTRIAN");
    BUFFER_LOG_PRINTF("%s Set dynamic model to 'Pedestrian'\n", (ped_ok ? okAck : badAck));

//    BUFFER_LOG_PRINTF("After setting dynamic model the 'ped_ok' and 'ok' combined result are %d %d\n", ped_ok,ok);

    ok &= ped_ok;

    // get SBAS enabled status
    bool enabled = false;
    bool sbas_ok = pollCFG_SBAS(enabled);
    
    ok &= sbas_ok;

//    BUFFER_LOG_PRINTF("After getting SBAS the 'sbas_ok' and 'ok' combined result are %d %d\n", sbas_ok,ok);

    if (ok)
      BUFFER_LOG_PRINTF("%s Get SBAS enabled status: %s\n", okAck, (enabled ? "enabled" : "disabled"));
    else
      BUFFER_LOG_PRINTF("%s Get SBAS enabled status failed\n", badAck);

    bool responseOk = pollCFG_MinSatellites(minimumSatellitesForFix);

    if (responseOk)
      BUFFER_LOG_PRINTF("%s Get Min Visible Sat Count: %d\n", okAck, minimumSatellitesForFix);
    else
      BUFFER_LOG_PRINTF("%s Get Min Visible Sat Count failed\n", badAck);


    const bool flushBufferLog = false;
    bool setSatCountHigh = gpsTriggerNoFixBySatCountHighForFix(flushBufferLog);
    BUFFER_LOG_PRINTF("%s - Set sat count high (20)\n", setSatCountHigh ? okAck : badAck);

    responseOk = pollCFG_MinSatellites(minimumSatellitesForFix);

    if (responseOk)
      BUFFER_LOG_PRINTF("%s Get Min Visible Sat Count: %d\n", okAck, minimumSatellitesForFix);
    else
      BUFFER_LOG_PRINTF("%s Get Min Visible Sat Count failed\n", badAck);

    bool setSatCountNormal = gpsTriggerNormalSatCountForFix(flushBufferLog);
    BUFFER_LOG_PRINTF("%s - Set sat count normal (4)\n", setSatCountNormal ? okAck : badAck);

    responseOk = pollCFG_MinSatellites(minimumSatellitesForFix);

    if (responseOk)
      BUFFER_LOG_PRINTF("%s Get Min Visible Sat Count: %d\n", okAck, minimumSatellitesForFix);
    else
      BUFFER_LOG_PRINTF("%s Get Min Visible Sat Count failed\n", badAck);

  }

  serial_gps.flush();

  return ok;
}

bool gpsSendColdStartCommand(bool flushBufferLog)
{
  if (flushBufferLog)
    BUFFER_LOG_RESET();

  // due to non-blocking reads in the gpsRx task there is no need to wait for any read to complete after setting the halt flag to true
  haltGPSTaskWhilstUBXTransactionsOngoing = true;

  bool ok = sendUBXUnified(0x06, 0x04, UBX_CFG_RST_COLDSTART, sizeof(UBX_CFG_RST_COLDSTART),"Trigger Cold Restart",false);

  BUFFER_LOG_PRINTF("UBX_CFG_RST_COLDSTART: Sent Cold Restart Command\n");

  haltGPSTaskWhilstUBXTransactionsOngoing = false;

  if (flushBufferLog)
    BUFFER_LOG_FLUSH_TO_SERIAL();

  return true;
}

bool gpsSendWarmStartCommand(bool flushBufferLog)
{
  if (flushBufferLog)
    BUFFER_LOG_RESET();

  // due to non-blocking reads in the gpsRx task there is no need to wait for any read to complete after setting the halt flag to true
  haltGPSTaskWhilstUBXTransactionsOngoing = true;

  bool ok = sendUBXUnified(0x06, 0x04, UBX_CFG_RST_WARMSTART, sizeof(UBX_CFG_RST_WARMSTART),"Trigger Warm Restart",false);

  haltGPSTaskWhilstUBXTransactionsOngoing = false;

  BUFFER_LOG_PRINTF("UBX_CFG_RST_WARMSTART: Sent Warm Restart Command\n");

  if (flushBufferLog)
    BUFFER_LOG_FLUSH_TO_SERIAL();

  return true;
}

bool gpsSendHotStartCommand(bool flushBufferLog)
{
  if (flushBufferLog)
    BUFFER_LOG_RESET();

  // due to non-blocking reads in the gpsRx task there is no need to wait for any read to complete after setting the halt flag to true
  haltGPSTaskWhilstUBXTransactionsOngoing = true;

  bool ok = sendUBXUnified(0x06, 0x04, UBX_CFG_RST_HOTSTART, sizeof(UBX_CFG_RST_HOTSTART),"Trigger Hot Restart",false);

  haltGPSTaskWhilstUBXTransactionsOngoing = false;

  BUFFER_LOG_PRINTF("UBX_CFG_RST_HOTSTART: Sent Hot Restart Command\n");

  if (flushBufferLog)
    BUFFER_LOG_FLUSH_TO_SERIAL();

  return true;
}

bool gpsTriggerNoFixBySatCountHighForFix(bool flushBufferLog)
{
  if (flushBufferLog)
    BUFFER_LOG_RESET();

  // due to non-blocking reads in the gpsRx task there is no need to wait for any read to complete after setting the halt flag to true
  haltGPSTaskWhilstUBXTransactionsOngoing = true;

  bool ok = sendUBXUnified(0x06, 0x8A, UBX_CFG_VALSET_INFIL_MINSV_20, sizeof(UBX_CFG_VALSET_INFIL_MINSV_20),"Set Min Satellites 20 - force no fix",false);

  haltGPSTaskWhilstUBXTransactionsOngoing = false;

  BUFFER_LOG_PRINTF("%s Sent Min Satellites 20 for Fix Command\n", (ok ? okAck : badAck));
  
  if (flushBufferLog)
    BUFFER_LOG_FLUSH_TO_SERIAL();

  return ok;
}

bool gpsTriggerNormalSatCountForFix(bool flushBufferLog)
{
  if (flushBufferLog)
    BUFFER_LOG_RESET();

  // due to non-blocking reads in the gpsRx task there is no need to wait for any read to complete after setting the halt flag to true
  haltGPSTaskWhilstUBXTransactionsOngoing = true;

  bool ok = sendUBXUnified(0x06, 0x8A, UBX_CFG_VALSET_INFIL_MINSV_3, sizeof(UBX_CFG_VALSET_INFIL_MINSV_3),"Set Min Satellites 3",false);

  haltGPSTaskWhilstUBXTransactionsOngoing = false;

  BUFFER_LOG_PRINTF("%s Sent Min Satellites 3 for Fix Command\n", (ok ? okAck : badAck));

  if (flushBufferLog)
    BUFFER_LOG_FLUSH_TO_SERIAL();

  return ok;
}

bool gpsGetSatCountForFix(int& minSatCount, bool flushBufferLog)
{
  if (flushBufferLog)
    BUFFER_LOG_RESET();

  minSatCount = -1;  

  // due to non-blocking reads in the gpsRx task there is no need to wait for any read to complete after setting the halt flag to true
  haltGPSTaskWhilstUBXTransactionsOngoing = true;

  bool ok = pollCFG_MinSatellites(minSatCount);

  haltGPSTaskWhilstUBXTransactionsOngoing = false;

  BUFFER_LOG_PRINTF("%s Sent Get Minimum Satellites for Fix: minimum satCount = %d\n", (ok ? okAck : badAck), minSatCount);

  if (flushBufferLog)
    BUFFER_LOG_FLUSH_TO_SERIAL();

  return ok;
}

#endif