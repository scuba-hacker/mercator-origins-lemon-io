#ifdef BUILD_INCLUDE_MAIN_GPS

// u-blox SAM-M10Q Data Sheet


// u-blox NEO-6M Data Sheet
// https://content.u-blox.com/sites/default/files/products/documents/u-blox6_ReceiverDescrProtSpec_%28GPS.G6-SW-10018%29_Public.pdf

// u-blox ZED-F94-01B data sheet, integration manual and  data protocol
// https://content.u-blox.com/sites/default/files/documents/u-blox-F9-HPS-1.40_InterfaceDescription_UBXDOC-963802114-13138.pdf
//
// https://content.u-blox.com/sites/default/files/documents/ZED-F9R-04B_DataSheet_UBXDOC-963802114-12930.pdf
// https://content.u-blox.com/sites/default/files/ZED-F9R_Integrationmanual_UBX-20039643.pdf
// https://content.u-blox.com/sites/default/files/documents/ZED-F9R-GettingStarted_AppNote_UBX-22035176.pdf
// https://content.u-blox.com/sites/default/files/documents/NEO-D9S_ZED-F9_Config_SPARTN_AppNote_UBX-22008160.pdf


// u-blox NEO-6M "Mercator mode":
// - Dynamic model: Pedestrian
// - 1 Hz nav rate
// - NMEA allowed even with NO FIX (GGA: fix=0, RMC: V)
// - Only GGA + RMC enabled on UART1
//
// Mark: connect GPS TX->RX1, RX->TX1, GND, 3V3/5V as per your board.

/* This is Neo-6T, not a 6M

Poll MON-VER (firmware version)
UBX frame: cls=0x0A id=0x04 len=37 chk=OK (skipped 1 NMEA bytes)
MON-VER response: len=37 bytes
Raw payload: 54 33 2C 52 6F 6D 46 77 2C 31 2E 31 28 34 38 29 2C 41 75 67 20 31 32 20 32 30 31 36 20 31 36 3A 35 37 3A 33 35 
GPS Firmware: SW=[T3,RomFw,1.1(48),Aug 12 2016 1] (HW version unavailable - short response)

NEO-6M (Standard GNSS)
  - Multi-GNSS receiver (GPS + GLONASS)
  - General positioning applications
  - Consumer/hobbyist applications
  - Standard timing accuracy

  NEO-6T (Timing Receiver)
  - Specialized for precise timing applications
  - High-accuracy time synchronization (designed for cellular base stations, network
  infrastructure)
  - Better timing pulse outputs (more accurate 1PPS signals)
  - Enhanced timing holdover during GPS outages
  - Often used in telecommunications, industrial timing

  Why you might have a NEO-6T instead of NEO-6M:

  1. Supply chain substitution - Supplier sent NEO-6T when NEO-6M unavailable
  2. Module labeling - Sometimes NEO-6T modules are mislabeled or sold as "NEO-6M"
  3. Breakout board variation - Some manufacturers use NEO-6T on boards marketed as
  "NEO-6M"
  4. Cost/availability - NEO-6T might have been cheaper or more available when manufactured

  For your marine application:
  - ✅ Still works perfectly for position/navigation
  - ✅ Actually better for precise timing if you need it
  - ✅ Same NMEA output format
  - ✅ Same UBX protocol support

  The NEO-6T is essentially a "premium" version of the NEO-6M with enhanced timing
  capabilities, so you actually got an upgrade! The CFG-PRT parsing differences are just
  firmware quirks - your GPS works fine for marine positioning.

  Bottom line: You have better hardware than expected, and it works perfectly for your
  Mercator Origins system.

NEO-6T vs NEO-6M Product Data Sheet Differences:

  The datasheets are quite different in several key areas:

  Key Differences:
  1. Timing Specifications: NEO-6T has much tighter timing accuracy specs
    - 1PPS accuracy: NEO-6T: ±10ns RMS vs NEO-6M: ±30ns RMS
    - Timing pulse jitter: Much lower on NEO-6T
    - Holdover performance: Better on NEO-6T
  2. Power Consumption: NEO-6T typically higher (more timing circuitry)
  3. Interface Features:
    - NEO-6T has enhanced timing pulse outputs
    - Additional timing-specific configuration options
    - Same UART/I2C/SPI interfaces
  4. NMEA Output: Identical between both variants
  5. UBX Protocol: Same command set, but NEO-6T has additional timing-specific messages

  Firmware Updates:

  ⚠️ Important Considerations:

  1. ROM-based Firmware: Your GPS shows "RomFw" - this typically means firmware is NOT
  field-updateable
    - ROM firmware is burned into the chip during manufacturing
    - Cannot be updated by end users
    - Only factory-programmable variants allow updates
  2. Age Factor: Firmware from August 2016 is quite old (8+ years)
    - u-blox has released many updates since then
    - However, ROM chips cannot be updated
  3. Update Risks: Even if updateable
    - Could brick the device if update fails
    - May lose current working configuration
    - Marine environment - reliability is more important than latest features

  Recommendation for Marine Use:
  - Don't attempt firmware updates - your GPS works perfectly
  - ROM firmware is actually more reliable (can't be corrupted)
  - For marine safety applications, "if it works, don't fix it"
  - Focus on configuring it optimally rather than updating firmware

  Your NEO-6T with 2016 firmware is proven, stable, and working well for your marine
  positioning needs.

*/

#include "SerialConfig.h"
// --- GPS Module Type Detection ---


enum GpsModuleType {
  GPS_MODULE_UNKNOWN = 0,
  GPS_MODULE_SAM_M10Q,
  GPS_MODULE_NEO_6M,
  GPS_MODULE_NEO_6T,
  GPS_MODULE_ZED_F9R,
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
    UBLOX_ACK_TIMEOUT_MS = 50;
    BUFFER_LOG_PRINTF(" [CFG-VALSET detected, using 100ms timeout]");
  }

  // Expect: B5 62 05 01 02 00 <cls> <id> CK_A CK_B
  const uint32_t deadline = millis() + UBLOX_ACK_TIMEOUT_MS;
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

          if (isAck) {
            BUFFER_LOG_PRINTF(" ACK received %s (skipped %d NMEA bytes)\n", ubxCommand, bytesSkippedForAck);
            return true;
          } else if (isNack) {
            BUFFER_LOG_PRINTF(" NACK received %s - command rejected (skipped %d NMEA bytes)\n", ubxCommand, bytesSkippedForAck);
            BUFFER_LOG_PRINTF(" UBX frame rejected: ");
            for (int i = 0; i < 10; i++) {
              BUFFER_LOG_PRINTF("%02X ", buf[i]);
            }
            BUFFER_LOG_PRINTF("\n");
            return false;
          }

          // Log any other UBX responses we're seeing
          if (buf[0]==0xB5 && buf[1]==0x62) {
            BUFFER_LOG_PRINTF(" Other UBX response: %02X %02X (%s) len=%d\n", buf[2], buf[3], ubxCommand, buf[4] | (buf[5] << 8));
          }

          // slide window to resync
          memmove(buf, buf+1, --idx);
        }
      }
    }
  }
  BUFFER_LOG_PRINTF(" TIMEOUT (skipped %d NMEA bytes, no ACK received)\n", bytesSkippedForAck);
  return false; // timeout
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
      } else {
        state = 0;
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

  BUFFER_LOG_PRINTF("UBX frame: cls=0x%02X id=0x%02X len=%d chk=%s (skipped %d NMEA bytes)\n",
                    cls_, id_, len, checksumOk ? "OK" : "FAIL", bytesSkipped);

  return checksumOk;
}

// --- Poll MON-VER to get firmware version ---
bool pollMON_VER(uint32_t timeout_ms = 1000)
{
  // UBX-MON-VER poll message (no payload): B5 62 0A 04 00 00 0E 34
  const uint8_t poll[] = { 0xB5,0x62, 0x0A,0x04, 0x00,0x00, 0x0E,0x34 };
  while (serial_gps.available()) serial_gps.read();  // drain old bytes
  serial_gps.write(poll, sizeof(poll));
  serial_gps.flush();

  // Read frames until timeout; collect class=0x0A id=0x04
  uint8_t cls,id;
  uint8_t payload[400];  // MON-VER can be quite long
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
          detectedModule.protocolVersion = 21;
        }
        else if (strstr(swVer, "T3,RomFw")) {
          detectedModule.type = GPS_MODULE_MTK_CLONE;
          BUFFER_LOG_PRINTF("Module Type: MTK Clone (fake u-blox)\n");
          detectedModule.protocolVersion = 23;
        } 
        else if (strstr(swVer, "ROM CORE 7.03") || strstr(swVer, "ROM CORE 6.02")) 
        {
          if (strstr(swVer, "LEA-6T") || strstr(swVer, "NEO-6T")) {
            detectedModule.type = GPS_MODULE_NEO_6T;
            BUFFER_LOG_PRINTF("Module Type: NEO-6T (Timing)\n");
          } 
          else 
          {
            detectedModule.type = GPS_MODULE_NEO_6M;
            BUFFER_LOG_PRINTF("Module Type: NEO-6M (Standard)\n");
          }
          detectedModule.protocolVersion = 14;
        } 
        else if (strstr(swVer, "FWVER=HPG") || strstr(extensions.c_str(), "ZED-F9R")) 
        {
          detectedModule.type = GPS_MODULE_ZED_F9R;
          detectedModule.protocolVersion = 27;
          BUFFER_LOG_PRINTF("Module Type: ZED-F9R (High Precision)\n");
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

// --- Minimal struct for CFG-PRT decoded data (UART style) ---
struct UbxPortCfg {
  uint8_t  portID;        // 1 = UART1
  uint32_t baud;          // little-endian in payload
  uint16_t inProtoMask;   // bit0=UBX, bit1=NMEA
  uint16_t outProtoMask;  // bit0=UBX, bit1=NMEA
};

// --- Poll CFG-PRT and collect up to maxPorts port records. Returns true if any. ---
bool pollCFG_PRT(UbxPortCfg *ports, size_t maxPorts, size_t &count, uint32_t timeout_ms = 300)
{
  count = 0;

  // UBX-CFG-PRT poll message (no payload): B5 62 06 00 00 00 06 18
  const uint8_t poll[] = { 0xB5,0x62, 0x06,0x00, 0x00,0x00, 0x06,0x18 };
  while (serial_gps.available()) serial_gps.read();  // drain old bytes
  serial_gps.write(poll, sizeof(poll));
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

        if (detectedModule.type == GPS_MODULE_SAM_M10Q)
        {
          o.inProtoMask  = (uint16_t)payload[12] | ((uint16_t)payload[13]<<8);
          o.outProtoMask = (uint16_t)payload[14] | ((uint16_t)payload[15]<<8);
          BUFFER_LOG_PRINTF("SAM-M10Q parsing (standard offsets)\n");
        }
        else if (detectedModule.type == GPS_MODULE_ZED_F9R) 
        {
          // ZED-F9R may have extended CFG-PRT structure
          // For now, use standard parsing but could be different
          o.inProtoMask  = (uint16_t)payload[12] | ((uint16_t)payload[13]<<8);
          o.outProtoMask = (uint16_t)payload[14] | ((uint16_t)payload[15]<<8);
          BUFFER_LOG_PRINTF("ZED-F9R CFG-PRT parsing (standard offsets)\n");
        } 
        else if (detectedModule.type == GPS_MODULE_NEO_6M || detectedModule.type == GPS_MODULE_NEO_6T) 
        {
          // NEO-6 series standard structure
          o.inProtoMask  = (uint16_t)payload[12] | ((uint16_t)payload[13]<<8);
          o.outProtoMask = (uint16_t)payload[14] | ((uint16_t)payload[15]<<8);
          BUFFER_LOG_PRINTF("NEO-6 series CFG-PRT parsing\n");
        } 
        else if (detectedModule.type == GPS_MODULE_MTK_CLONE) 
        {
          // MTK clone - protocol masks may not be meaningful
          o.inProtoMask  = (uint16_t)payload[12] | ((uint16_t)payload[13]<<8);
          o.outProtoMask = (uint16_t)payload[14] | ((uint16_t)payload[15]<<8);
          BUFFER_LOG_PRINTF("MTK clone CFG-PRT parsing (may be unreliable)\n");
        } 
        else 
        {
          // Unknown module - use standard parsing
          o.inProtoMask  = (uint16_t)payload[12] | ((uint16_t)payload[13]<<8);
          o.outProtoMask = (uint16_t)payload[14] | ((uint16_t)payload[15]<<8);
          BUFFER_LOG_PRINTF("Unknown module CFG-PRT parsing (standard offsets)\n");
        }

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

constexpr uint32_t KEY_SBAS_ENA = 0x1036001D; // CFG-SIGNAL-SBAS_ENA

// Returns true if payload parsed. keyOut/valueOut are valid on success.
bool parseValgetPayload(const uint8_t* payload, size_t len,
                        uint32_t& keyOut, uint32_t& valueOut)
{
  /*
  UBX-CFG-VALGET response format for enable/disable response

  B5 62             ; sync chars
  06 8B             ; class=CFG (0x06), id=VALGET (0x8B)
  0C 00             ; length = 12 bytes (for one key)
  00 01 00 00       ; version=0, layers=RAM, reserved
  1D 00 36 10       ; keyID = 0x1036001D (LE)
  01 00 00 00       ; value = 0x01 (enabled)
  <CK_A> <CK_B>     ; Fletcher checksum
  */

  // Minimal VALGET payload for 1 key = 12 bytes:
  // [0]=version, [1]=layers, [2..3]=reserved,
  // [4..7]=KeyID (LE), [8..11]=Value (LE)
  if (!payload || len < 12) return false;
  if (payload[0] != 0x00)   return false; // version must be 0

  keyOut   = (uint32_t)payload[4] | ((uint32_t)payload[5] << 8) |
             ((uint32_t)payload[6] << 16) | ((uint32_t)payload[7] << 24);
  valueOut = (uint32_t)payload[8] | ((uint32_t)payload[9] << 8) |
             ((uint32_t)payload[10] << 16) | ((uint32_t)payload[11] << 24);
  return true;
}

// UBX-CFG-VALSET (0x06 0x8B)
// header: version=0x00, layers=0x01 (RAM), rsvd[2]=0
uint8_t UBX_CFG_SIGNAL_SBAS_ENA_VALSET[] = {
  0x00, 0x01, 0x00, 0x00,   // version 0, layers=0x01(RAM), 2 bytes reserved
  0x1D, 0x00, 0x36, 0x10    // key 0x10360001 (little-endian): CFG-SIGNAL-SBAS_ENA
};



// Example: check SBAS enable
bool isSbasEnabledFromValget(const uint8_t* payload, size_t len)
{
  uint32_t key, val;
  if (!parseValgetPayload(payload, len, key, val)) return false;
  if (key != KEY_SBAS_ENA) return false;   // wrong key in reply
  return (val & 0x01u) != 0;               // 1 = enabled, 0 = disabled
}

bool pollCFG_SBAS(bool& enabled, uint32_t timeout_ms = 300)
{
  bool sbasEnabled = false;
  
  while (serial_gps.available()) serial_gps.read();  // drain old bytes
  serial_gps.write(UBX_CFG_SIGNAL_SBAS_ENA_VALSET, sizeof(UBX_CFG_SIGNAL_SBAS_ENA_VALSET));
  serial_gps.flush();

  // Read frames until timeout gap; collect only class=0x06 id=0x00 with len=20
  uint8_t cls,id;
  uint8_t payload[32];
  uint16_t len;

  uint32_t endBy = millis() + timeout_ms;
  while (millis() < endBy) {
    if (!readUBXFrame(cls, id, payload, len, /*timeout per frame*/ 100)) 
      break;

    if (cls == 0x06 && id == 0x8B && len == 12) 
    {
      sbasEnabled = isSbasEnabledFromValget(payload,len);
      break;
    }
    // ignore unrelated frames (e.g., NMEA or other UBX) and keep reading until gap
  }

  return sbasEnabled;
}

// =================== Configuration payloads ===================

// Set to 115000 baud
// Build a UBX-CFG-VALSET that writes to RAM (+ optionally BBR/Flash)
// Header: version=0x00, layers=RAM(0x01) [or 0x07 for RAM|BBR|Flash], 2 reserved bytes
uint8_t UBX_CFG_BAUD_SET_VALSET[] = {
  0x00, 0x01, 0x00, 0x00,                             // version, layers, rsvd
  0x01,0x00,0x52,0x40,    // key 0x40520001
  0x00,0xC2,0x01,0x00     // value 115200
};

// 1) CFG-NAV5 (0x06 0x24) — set Dynamic Model = Pedestrian, Fix mode = Auto 2D/3D
// For u-blox 6, payload is 36 bytes.
// mask (LE) = 0x0005 (apply dynModel + fixMode). Other fields left 0 (ignored by mask).
const uint8_t CFG_NAV5_PEDESTRIAN_NEO_6M[36] = {
  0x05,0x00,       // mask: apply dynModel + fixMode
  0x03,            // dynModel: 3 = Pedestrian
  0x03,            // fixMode:  3 = Auto 2D/3D
  // fixedAlt (4), fixedAltVar (4)
  0x00,0x00,0x00,0x00,   0x00,0x00,0x00,0x00,
  // minElev, drLimit
  0x00,0x00,
  // pDop (2), tDop (2), pAcc (2), tAcc (2)
  0x00,0x00, 0x00,0x00,  0x00,0x00, 0x00,0x00,
  // staticHoldThresh, dgpsTimeOut, cnoThreshNumSVs, cnoThresh
  0x00,0x00,0x00,0x00,
  // reserved2 (2), staticHoldMaxDist (2), utcStandard (1)
  0x00,0x00, 0x00,0x00, 0x00,
  // reserved3 (pad to 36)
  0x00,0x00,0x00,0x00,0x00
};

// 2) CFG-NMEA (0x06 0x17) — u-blox 6: Alternative approach - try minimal 4-byte version
// Some NEO-6M modules may expect the shorter format. Filter bit0 (posFilt) = 0
const uint8_t CFG_NMEA_ALLOW_NOFIX[4] = {
  0x00, // filter: posFilt=0 (do not suppress invalid position)
  0x21, // NMEA version 2.1 (more compatible with NEO-6M) -- verified
  0x00, // numSV (unlimited satellite reporting) -- verified
  0x00  // flags (no special flags) -- verified
};

// 3) CFG-MSG payloads - Different formats for different modules

// NEO-6 series: 3-byte format [msgClass][msgID][rateThisPort]
const uint8_t CFG_MSG_NEO6_GGA_EN[3] = { 0xF0, 0x00, 0x01 };
const uint8_t CFG_MSG_NEO6_RMC_EN[3] = { 0xF0, 0x04, 0x01 };
const uint8_t CFG_MSG_NEO6_GLL_DIS[3]= { 0xF0, 0x01, 0x00 };
const uint8_t CFG_MSG_NEO6_GSA_DIS[3]= { 0xF0, 0x02, 0x00 };
const uint8_t CFG_MSG_NEO6_GSV_DIS[3]= { 0xF0, 0x03, 0x00 };
const uint8_t CFG_MSG_NEO6_VTG_DIS[3]= { 0xF0, 0x05, 0x00 };
const uint8_t CFG_MSG_NEO6_ZDA_DIS[3]= { 0xF0, 0x08, 0x00 };

// ZED-F9R series: 8-byte format [msgClass][msgID][rate0][rate1][rate2][rate3][rate4][rate5]
// rate0=I2C, rate1=UART1, rate2=UART2, rate3=USB, rate4=SPI, rate5=reserved
const uint8_t CFG_MSG_ZED_GGA_EN[8] = { 0xF0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };
const uint8_t CFG_MSG_ZED_RMC_EN[8] = { 0xF0, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };
const uint8_t CFG_MSG_ZED_GLL_DIS[8]= { 0xF0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const uint8_t CFG_MSG_ZED_GSA_DIS[8]= { 0xF0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const uint8_t CFG_MSG_ZED_GSV_DIS[8]= { 0xF0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const uint8_t CFG_MSG_ZED_VTG_DIS[8]= { 0xF0, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const uint8_t CFG_MSG_ZED_ZDA_DIS[8]= { 0xF0, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

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
uint8_t UBX_CFG_SEA_MODEL_VALSET[] = {
  0x00, 0x01, 0x00, 0x00,   // version 0, layers=0x01(RAM), 2 bytes reserved
  0x21, 0x00, 0x11, 0x20,   // key 0x20110021 is CFG-NAVSPG-DYNMODEL
  0x05                      // value 0x05 (Sea)
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

// 6) CFG-CFG (0x06 0x09) — Save to BBR/Flash (where present - not SAM-M10Q)
// len=12: [clearMaskLE][saveMaskLE][loadMaskLE]
// Use a broad but common mask to cover ports/messages/rates/NMEA.
const uint8_t CFG_CFG_SAVE[12] = {
  0x00,0x00,0x00,0x00,  // clearMask
  0xFF,0x00,0x00,0x00,  // saveMask (low byte set; adjust if you want a narrower scope)
  0xFF,0x00,0x00,0x00   // loadMask (not strictly needed for save)
};

// ------------------- Module-specific command helpers -------------------
bool sendCFG_MSG_enable(uint8_t msgClass, uint8_t msgId, const char* msgName, int retry=0) {
  int attempt=0;
  while (retry >= 0)
  {
    retry--;
    attempt++;
    if (detectedModule.type == GPS_MODULE_ZED_F9R) {
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
    if (detectedModule.type == GPS_MODULE_ZED_F9R) {
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
      bool baud_ok = false;
      if (!gpsAt115200)
      {
        baud_ok = sendUBX(0x06, 0x8A, UBX_CFG_BAUD_SET_VALSET, sizeof(UBX_CFG_BAUD_SET_VALSET),"SET BAUD TO 115200",false);
        delay(100);
        BUFFER_LOG_PRINTF("%s Set Baud Rate to 115200\n", (baud_ok ? okAck : badAck));
      }
      else
      {

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
    }

  // Configure NMEA messages based on detected module type
  if (detectedModule.type == GPS_MODULE_MTK_CLONE) 
  {
    BUFFER_LOG_PRINTF("%s MTK clone detected - skipping UBX configuration (NMEA works by default)\n", okAck);
  } 
  else 
  {
    // Allow NMEA output even when there is no fix (genuine u-blox modules)
    if (detectedModule.type != GPS_MODULE_ZED_F9R) {
      // Try CFG-NMEA for older modules (may not be supported)
      ok &= sendUBX(0x06, 0x17, CFG_NMEA_ALLOW_NOFIX, sizeof(CFG_NMEA_ALLOW_NOFIX),"ENABLE NO FIX MSGS");
      BUFFER_LOG_PRINTF("%s Allow No Fix messages (GGA/RMC) (CFG-NMEA): %s\n", (ok ? okAck : badAck), ok ? "OK" : "Not supported by this firmware");
    }
    else {
      // Stop the suppression of No Fix GGA/RMC messages
      // Normally they are simply not sent, resulting in a gap in messages.    
      bool send_no_fix_result = sendUBX(0x06, 0x8A, UBX_CFG_NMEA_EMIT_NO_FIX, sizeof(UBX_CFG_NMEA_EMIT_NO_FIX),"ENABLE NO FIX MSGS");
      ok &= send_no_fix_result;
      BUFFER_LOG_PRINTF("%s Allow No Fix messages (GGA/RMC)\n", (send_no_fix_result ? okAck : badAck));
    }

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

    // Set dynamic model to Sea - ZED-9FR and SAM-M10Q only
//    bool model_ok = sendUBX(0x06, 0x8A, UBX_CFG_SEA_MODEL_VALSET, sizeof(UBX_CFG_SEA_MODEL_VALSET),"DYNAMIC MODEL = SEA");
//    BUFFER_LOG_PRINTF("%s Set dynamic model to 'Sea'\n", (model_ok ? okAck : badAck));

    // Set dynamic model to Pedestrian  - ZED-9FR and SAM-M10Q only
    bool model_ok = sendUBX(0x06, 0x8A, UBX_CFG_PEDESTRIAN_MODEL_VALSET, sizeof(UBX_CFG_PEDESTRIAN_MODEL_VALSET),"DYNAMIC MODEL = PEDESTRIAN");
    BUFFER_LOG_PRINTF("%s Set dynamic model to 'Pedestrian'\n", (model_ok ? okAck : badAck));

    bool sbas_enabled = sendUBX(0x06, 0x8B, UBX_CFG_SIGNAL_SBAS_ENA_VALSET, sizeof(UBX_CFG_SIGNAL_SBAS_ENA_VALSET),"SBAS CORRECTION ENABLED?");
    BUFFER_LOG_PRINTF("%s Is SBAS Correction Enabled?\n", (model_ok ? okAck : badAck));

    


    ok &= model_ok;
  }

  serial_gps.flush();

  return ok;
}

#endif