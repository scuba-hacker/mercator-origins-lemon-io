#ifdef BUILD_INCLUDE_MAIN_PART2

#include "MercatorMQTT.h"
#include "SerialConfig.h"

char* customiseNMEASentence(char* sentence, int showOnMapIndex)
{  
  const int minimumSentenceLength = 48;

  int startSentenceIndex = 0;
  char* startSentence = sentence;
  for (int i=0; i < minimumSentenceLength; i++)
  {
    if (sentence[i] == '$')
    {
      startSentence = sentence + i;
      startSentenceIndex = i;
      break;
    }
  }

  USB_SERIAL_PRINTF("0. startSentence index = %i\n",startSentenceIndex);

  const bool isGNGGA = ((strncmp(startSentence,"$GPGGA",6) == 0 ||
                         strncmp(startSentence,"$GNGGA",6) == 0));

  const bool isGNRMC = ((strncmp(startSentence,"$GPRMC",6) == 0 ||
                         strncmp(startSentence,"$GNRMC",6) == 0));

  if (overrideGPSToNoFixForTesting)
  {
    if (isGNGGA)
    {
      // $GPGGA,172814.0,3723.46587704,N,12202.26957864,W,2,6,1.2,18.893,M,-25.669,M,2.0 0031*4F
      // Field 6 is quality indicator (2 above) - 0 means NO FIX, 1 means GPS Fix, other vals out of scope
      // Find ',W,' or ',E,' and overwrite the following field
      char* fixFlagW = strstr(startSentence,",W,");
      char* fixFlagE = strstr(startSentence,",E,");

      if (fixFlagW)
        *(fixFlagW+3) = '0';   // override valid fix 1 with no fix 0
      else if (fixFlagE)
        *(fixFlagE+3) = '0';   // override valid fix 1 with no fix 0
    }
    else
    {
      // $GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A
      // Field 2 is A for Fix (active), V for no fix (void)
      char* fixFlag = strstr(startSentence,",A,");
      if (fixFlag)
        *(fixFlag+1) = 'V';   // override valid fix (A) with void fix (V) which is NO FIX
    }
  }

  bool overrideLocation = false;

  USB_SERIAL_PRINTLN("0. checking for override location");
  USB_SERIAL_PRINTF("0.0 showOnMapIndex=%i isGNGAA=%i isGNRMC=%i strlen(startSentence)=%zu\n",showOnMapIndex, (isGNGGA ? 1 : 0),(isGNRMC ? 1 : 0), strnlen(startSentence,minimumSentenceLength));
  USB_SERIAL_PRINTF("0.1 %s\n",startSentence);

  if (  showOnMapIndex >= 0 && 
        (isGNGGA || isGNRMC) && 
        strnlen(startSentence,minimumSentenceLength) >= minimumSentenceLength)
  {
    USB_SERIAL_PRINTLN("1. Entered override location");
    
    overrideLocation = true;
    // spoof GPS to be reporting lat/long at selected feature
    double longOverride = WraysburyWaypoints::waypoints[showOnMapIndex]._long;
    double latOverride = WraysburyWaypoints::waypoints[showOnMapIndex]._lat;

    char directionLong = 'E';
    char directionLat = 'N';
  
    if (longOverride < 0.0)
    {
      directionLong = 'W';
      longOverride = -longOverride;
    }

    if (latOverride < 0.0)
    {
      directionLat = 'S';
      latOverride = -latOverride;
    }

    int longDegrees = (int)longOverride;
    double longMinutesActual = (longOverride - (double)longDegrees) * 60.0;
    int longMinutes = longMinutesActual;
    int longMinuteFraction = (longMinutesActual - longMinutes) * 100000.0;

    int latDegrees = (int)latOverride;
    double latMinutesActual = (latOverride - (double)latDegrees) * 60.0;
    int latMinutes = latMinutesActual;
    int latMinuteFraction = (latMinutesActual - latMinutes) * 100000.0;

    const int lengthNMEALocation = 24;
    char newLocation[100];
  
    snprintf(newLocation, sizeof(newLocation), "%02d%02d.%05d,%c,%03d%02d.%05d,%c",
                          latDegrees,latMinutes,latMinuteFraction, directionLat, 
                          longDegrees,longMinutes, longMinuteFraction, directionLong);

    USB_SERIAL_PRINTF("1.1 %.60s    <-- new location\n", newLocation);

    int validDataRMCOffset = 16 + startSentenceIndex;
    char validDataGoodFixOverride = 'A';
    int copyOffset = -1;

    if (isGNGGA)
    {
      copyOffset = 17 + startSentenceIndex;
      validDataRMCOffset = -1;
    }
    else if (isGNRMC)
    {
      copyOffset = 19 + startSentenceIndex;
      // always assume valid fix for a location override
      startSentence[validDataRMCOffset] = validDataGoodFixOverride;
    }

    if (copyOffset >= 0)
    {
      USB_SERIAL_PRINTLN("2. Override Location");
      USB_SERIAL_PRINTF("3.0 %s    <-- Old Sentence\n", startSentence);

      memcpy(sentence+copyOffset,newLocation,lengthNMEALocation);
 
      USB_SERIAL_PRINTF("4.0 %s    <-- New Location Sentence\n", startSentence);
    }
    else
      USB_SERIAL_PRINTLN("2. Not Overriding Location");
  }
  
  char overrideForNoInternetConnection = '\0';

  if (telemetryPipeline.getPipelineLength() > 2)
  {
     overrideForNoInternetConnection = 'N';
  }

  if (overrideForNoInternetConnection && isGNGGA)
  {
    // Infiltrate internet upload status into
    // the byte that is normally fixed at M representing Metres units
    // for difference between sea level and geoid.

    const uint32_t padding = 8;
    char* next=sentence;
    char* end=sentence+strlen(sentence)-padding;

    while (*next++ != '$' && next < end);

    if (next == end)
      return sentence;
  
    next+=2;
  
    // sentences: GPGGA or GNGGA - search for 12th comma
    if (*next++ == 'G' && *next++ == 'G' && *next++ == 'A')
    {
      char priorVal = 0, newVal = 0;
      
      uint8_t commas=0;
      while (*next && next < end)
      {
        if (*next++ == ',')
        {
          commas++;
  
          if (commas == 12)
          {
            // next char change to be indicative of upload to internet status
            if (telemetryPipeline.getPipelineLength() > 2)
            {
              // overwrite the character in the sentence which is normally 'M' for Unit of altitude
              priorVal = *next;
              newVal = *next = overrideForNoInternetConnection;
            }
            else
            {
              break;
            }
          }
        }
      }
    }
  }

  bool overrideForTarget = false;

  if (networkManager.getSetTargetRequestIndex() >= 0 && isGNGGA)
  {
    overrideForTarget = true;
    // Infiltrate internet upload status into
    // the byte that is normally fixed at M representing Metres units
    // for difference between sea level and geoid.

    const uint32_t padding = 8;
    char* next=sentence;
    char* end=sentence+strlen(sentence)-padding;

    while (*next++ != '$' && next < end);

    if (next == end)
      return sentence;
  
    next+=2;
  
    // sentences: GPGGA or GNGGA - search for 14th comma
    if (*next++ == 'G' && *next++ == 'G' && *next++ == 'A')
    {
      uint8_t commas=0;
      while (*next && next < end)
      {
        if (*next++ == ',')
        {
          commas++;
  
          if (commas == 10)
          {
            // overwrite the character in the sentence which is normally 'M' for Unit of geoid separation
            *next = networkManager.getSetTargetRequestIndex()+33; // make sure visible char

            if (*next == 'M')     // exception for target mapping to M as M means no set target
              *next = -2;
          }
        }
      }
    }
  }

  // if change has been made, recalculate checksum and populate
  if (overrideForNoInternetConnection || overrideLocation || overrideForTarget)
  {
    unsigned char checksum = 0;
    // Start after the '$' character
    char *p = startSentence + 1;

    // XOR each character until '*' or end of string
    while (*p && *p != '*') {
        checksum ^= *p;
        p++;
    }

    // Find the position of the '*' character (p already points to it after the loop)
    if (*p == '*') {
        // Advance pointer by 1 to move past '*'
        p++;
        // Update checksum in hex
        *p++ = "0123456789ABCDEF"[checksum >> 4];  // High nibble
        *p++ = "0123456789ABCDEF"[checksum & 0x0F]; // Low nibble
    }

    USB_SERIAL_PRINTF("5.0 %s    <-- All updates new  Location Sentence\n", startSentence);
  }

  return sentence;
}

void incrementUplinkMessageMissedCount()
{
  if (accumulateMissedMessageCount)
  {
    uplinkMessageMissingCount++;
  }
  else
  {
    if (millis() > startAccumulatingMissedMessagesAt)
    {
      uplinkMessageMissingCount++;
      accumulateMissedMessageCount = true;
    }
  }
}

void initializeTempHumiditySensor()
{
  Wire.begin();

  //Configure HDC1080
  Wire.beginTransmission(0x40);
  Wire.write(0x02);
  Wire.write(0x90);
  Wire.write(0x00);
  Wire.endTransmission();

  delay(20);
}

// Have to call three times to get the first value to avoid synchronous waits
// This can go in a task later
bool readTempHumidityCJMCU_1080_Sensor(double* temperature, double* humidity)
{
  # define TEMP_TIME_FOR_CONVERSION 20
  # define TEMP_TIME_TO_GET_REQUEST 1
  # define MIN_TIME_BETWEEN_SAMPLES 1000

  bool newReadingsAvailable = false;

  static int state = 0;
  static uint32_t nextStateAt = 0;

  if (millis() > nextStateAt)
  {
    if (state == 0)
    {
      //holds 2 bytes of data from I2C Line
      uint8_t Byte[4];

      uint16_t temp;
      uint16_t humid;

      //Point to device 0x40 (Address for HDC1080)
      Wire.beginTransmission(0x40);

      //Point to register 0x00 (Temperature Register)
      Wire.write(0x00);

      //Relinquish master control of I2C line
      //Pointing to the temp register triggers a conversion
      Wire.endTransmission();

      nextStateAt = millis() + TEMP_TIME_FOR_CONVERSION;
      state++;
    }
    else if (state == 1)
    {
      Wire.requestFrom(0x40, 4);  // Request four bytes from registers  
      nextStateAt = millis() + TEMP_TIME_TO_GET_REQUEST;
      state++;
    }
    else if (state == 2)
    {
      //If the 4 bytes were returned sucessfully
      if (4 <= Wire.available())
      {
        uint8_t Byte[5];
        Byte[0] = Wire.read();    // upper byte of temp reading
        Byte[1] = Wire.read();    // lower byte of temp reading
        Byte[3] = Wire.read();    // upper byte of humidity reading
        Byte[4] = Wire.read();    // lower byte of humidity reading

        uint16_t temp = (((unsigned int)Byte[0] <<8 | Byte[1]));
        *temperature = (double)(temp)/(65536)*165-40;

        uint16_t humid = (((unsigned int)Byte[3] <<8 | Byte[4]));
        *humidity = (double)(humid)/(65536)*100;
        state = 0;
        newReadingsAvailable = true;
        nextStateAt = millis() + MIN_TIME_BETWEEN_SAMPLES;
      }
      else
      {
        nextStateAt = millis() + TEMP_TIME_TO_GET_REQUEST;
        // stay in state 2, try getting again in 1ms
      }
    }
  }
  return newReadingsAvailable;
}

// System configuration and preferences management
void loadTestingPreferences() {
   testingPrefs.begin("testing", false);
   wifiTestingBlocked = testingPrefs.getBool("wifi_blocked", false);
  
  USB_SERIAL_PRINTF("Testing preferences loaded: WiFi blocked=%s\n", 
            wifiTestingBlocked ? "YES" : "NO");
}

void saveTestingPreferences() {
  testingPrefs.putBool("wifi_blocked", wifiTestingBlocked);
  USB_SERIAL_PRINTF("Testing preferences saved: WiFi blocked=%s\n", 
            wifiTestingBlocked ? "YES" : "NO");
}

/**
 * @brief Initialize Marine Telemetry Storage System
 * 
 * Initializes the telemetry system based on compile-time configuration.
 * This function provides unified initialization for both storage modes:
 * 
 * Flash Mode (USE_FLASH_TELEMETRY defined):
 * - Initializes FlashTelemetryManager with flash persistence
 * - Runs power-on self-test with auto-repair capability  
 * - Falls back to PSRAM mode if flash system fails
 * - Enables 8+ hour marine logging without connectivity
 * 
 * PSRAM Mode (USE_FLASH_TELEMETRY not defined):
 * - Initializes traditional TelemetryPipeline  
 * - Battle-tested reliability for marine operations
 * - Limited to active power session duration
 * 
 * Marine Safety: Both modes use identical API ensuring seamless operation
 */
void initializeTelemetrySystem() {
  USB_SERIAL_PRINTLN(">>> Initializing telemetry system...");
  
  // Initialize the selected telemetry system (API is identical for both)
  telemetryPipeline.init(&millis, 2048);
  
#ifdef USE_FLASH_TELEMETRY
  USB_SERIAL_PRINTLN(">>> Telemetry System: Flash persistence ENABLED");
  USB_SERIAL_PRINTLN(">>> Marine Mode: Extended dive logging (8+ hours) with power-safe storage");
#else
  USB_SERIAL_PRINTLN(">>> Telemetry System: Using PSRAM (volatile) storage");
  USB_SERIAL_PRINTLN(">>> Marine Mode: Battle-tested reliability, active session only");
#endif
  
  // Configure NetworkManager to use the initialized telemetry system
  networkManager.setTelemetryPipeline(&telemetryPipeline);
}

const char* getCurrentPipelineType() {
#ifdef USE_FLASH_TELEMETRY
  return "FLASH (Persistent)";
#else
  return "PSRAM (Volatile)";
#endif
}

// Process individual serial command character
void processSerialCommand(char command) {
  
  switch (command) {
    case 'D':
    case 'd':
      // Disconnect WiFi for testing
      if (!wifiTestingBlocked) {
        wifiTestingBlocked = true;
        WiFi.disconnect(true);  // Disconnect and disable auto-reconnect
        saveTestingPreferences();
        USB_SERIAL_PRINTLN(">>> TESTING: WiFi DISCONNECTED - Flash buffer should activate");
        sendLemonStatus(LC_NO_WIFI);
      } else {
        USB_SERIAL_PRINTLN(">>> TESTING: WiFi already disconnected");
      }
      break;
      
    case 'C':
    case 'c':
      // Connect WiFi for testing  
      if (wifiTestingBlocked) {
        wifiTestingBlocked = false;
        saveTestingPreferences();
        // Trigger network manager to reconnect
        networkManager.setForceConnectivityCheckForDisplay(true);
        USB_SERIAL_PRINTLN(">>> TESTING: WiFi RECONNECT enabled - Should drain flash buffer to MQTT");
      } else {
        USB_SERIAL_PRINTLN(">>> TESTING: WiFi already connected");
      }
      break;
      
    case 'F':
    case 'f':
      // Show flash buffer compile-time setting
#ifdef USE_FLASH_TELEMETRY
      USB_SERIAL_PRINTLN(">>> Flash persistence is ENABLED (compile-time setting)");
      USB_SERIAL_PRINTLN(">>> To disable, comment out #define USE_FLASH_TELEMETRY in main.cpp and recompile");
#else
      USB_SERIAL_PRINTLN(">>> Flash persistence is DISABLED (compile-time setting)");
      USB_SERIAL_PRINTLN(">>> To enable, uncomment #define USE_FLASH_TELEMETRY in main.cpp and recompile");
#endif
      break;
      
    case 'R':
    case 'r':
      // Reset flash buffer (factory reset)
      USB_SERIAL_PRINTLN(">>> TESTING: Performing flash buffer factory reset...");
      // TODO: Call flash buffer factory reset when integrated
      break;
      
    case 'S':
    case 's':
      // Show status
      USB_SERIAL_PRINTLN("=== LEMON-IO SYSTEM STATUS ===");
      USB_SERIAL_PRINTF("Flash Persistence: %s\n", 
#ifdef USE_FLASH_TELEMETRY
                       "ENABLED");
#else
                       "DISABLED");
#endif
      USB_SERIAL_PRINTF("Active Pipeline: %s\n", getCurrentPipelineType());
      USB_SERIAL_PRINTF("WiFi Status: %s\n", WiFi.isConnected() ? "Connected" : "Disconnected");
      USB_SERIAL_PRINTF("MQTT Status: %s\n", privateMQTT.isConnected() ? "Connected" : "Disconnected");
      USB_SERIAL_PRINTF("Pipeline Length: %u records\n", telemetryPipeline.getPipelineLength());
      USB_SERIAL_PRINTF("Pipeline Draining: %s\n", telemetryPipeline.isPipelineDraining() ? "YES" : "NO");
      USB_SERIAL_PRINTLN("");
      USB_SERIAL_PRINTLN("Testing Simulation States:");
      USB_SERIAL_PRINTF("WiFi Testing Blocked: %s\n", wifiTestingBlocked ? "YES" : "NO");
      USB_SERIAL_PRINTLN("===============================");
      break;
      
    case 'H':
    case 'h':
    case '?':
      // Show help
      USB_SERIAL_PRINTLN("=== LEMON-IO COMMAND REFERENCE ===");
      USB_SERIAL_PRINTLN("Production Commands:");
      USB_SERIAL_PRINTLN("F/f - Toggle flash persistence on/off");
      USB_SERIAL_PRINTLN("S/s - Show system status");
      USB_SERIAL_PRINTLN("R/r - Factory reset flash storage");
      USB_SERIAL_PRINTLN("H/h/? - Show this help");
      USB_SERIAL_PRINTLN("");
      USB_SERIAL_PRINTLN("Testing/Simulation Commands:");
      USB_SERIAL_PRINTLN("D/d - Disconnect WiFi (simulate offline)");
      USB_SERIAL_PRINTLN("C/c - Connect WiFi (simulate online)");
      USB_SERIAL_PRINTLN("");
      USB_SERIAL_PRINTLN("Flash Diagnostic Commands (Web Interface):");
      USB_SERIAL_PRINTLN("POST - Power-On Self Test & Auto-Repair");
      USB_SERIAL_PRINTLN("DEEP - Deep Sector Validation");
      USB_SERIAL_PRINTLN("STRESS - High-Volume Stress Test");
      USB_SERIAL_PRINTLN("RECOVERY - Power-Loss Recovery Test");
      #ifdef TESTING_MODE
      USB_SERIAL_PRINTLN("");
      USB_SERIAL_PRINTLN("Failure Injection Commands (TESTING_MODE builds):");
      USB_SERIAL_PRINTLN("CORRUPT_SECTOR [num] - Corrupt sector magic number");
      USB_SERIAL_PRINTLN("CORRUPT_STATE - Corrupt EEPROM state (requires restart)");
      USB_SERIAL_PRINTLN("SIMULATE_POWER_LOSS - Simulate power-loss during write");
      USB_SERIAL_PRINTLN("CORRUPT_POINTERS - Corrupt ring buffer pointers");
      USB_SERIAL_PRINTLN("WEAR_TEST [cycles] - Accelerated wear testing");
      USB_SERIAL_PRINTLN("RANDOM_CORRUPT [num] - Random sector corruption");
      USB_SERIAL_PRINTLN("PARTITION_FAIL - Simulate partition failure");
      USB_SERIAL_PRINTLN("CORRUPT_CRC [num] - Corrupt sector CRC");
      USB_SERIAL_PRINTLN("ENABLE_FAIL_INJECT - Enable failure injection mode");
      #endif
      USB_SERIAL_PRINTLN("===================================");
      break;
      
    default:
      // Ignore other characters (including newlines, spaces, etc.)
      break;
  }
}

// These are commands to execute through USB serial to allow for testing of flash persistence/buffer
void processSerialCommands() {
  // Only process if serial data is available
  if (!Serial.available()) {
    return;
  }
  
  char command = Serial.read();
  processSerialCommand(command);
}

// Process extended WebSerial commands (flash diagnostics)
void processExtendedCommand(const String& command) {
  if (command == "POST") {
    USB_SERIAL_PRINTLN(">>> DIAGNOSTIC: Running Power-On Self Test...");
    bool result = telemetryPipeline.performPowerOnSelfTest(true);
    USB_SERIAL_PRINTF(">>> DIAGNOSTIC: Power-On Self Test %s\n", result ? "PASSED" : "FAILED");
  } else if (command == "DEEP") {
    USB_SERIAL_PRINTLN(">>> DIAGNOSTIC: Running Deep Sector Validation...");
    bool result = telemetryPipeline.performDeepSectorValidation();
    USB_SERIAL_PRINTF(">>> DIAGNOSTIC: Deep Sector Validation %s\n", result ? "PASSED" : "FAILED");
  } else if (command == "STRESS") {
    USB_SERIAL_PRINTLN(">>> DIAGNOSTIC: Running Stress Test (100 records)...");
    bool result = telemetryPipeline.performStressTest(100);
    USB_SERIAL_PRINTF(">>> DIAGNOSTIC: Stress Test %s\n", result ? "PASSED" : "FAILED");
  } else if (command == "RECOVERY") {
    USB_SERIAL_PRINTLN(">>> DIAGNOSTIC: Running Power-Loss Recovery Test...");
    bool result = telemetryPipeline.performPowerLossRecoveryTest();
    USB_SERIAL_PRINTF(">>> DIAGNOSTIC: Power-Loss Recovery Test %s\n", result ? "PASSED" : "FAILED");
  }
  
  // Failure injection commands (only available in TESTING_MODE builds)
  #ifdef TESTING_MODE
  else if (command.startsWith("CORRUPT_SECTOR")) {
    int sector_num = 5; // Default sector
    if (command.indexOf(' ') > 0) {
      sector_num = command.substring(command.indexOf(' ') + 1).toInt();
    }
    USB_SERIAL_PRINTF(">>> FAILURE INJECTION: Corrupting sector %d...\n", sector_num);
    bool result = telemetryPipeline.injectSectorCorruption(sector_num);
    USB_SERIAL_PRINTF(">>> FAILURE INJECTION: Sector corruption %s\n", result ? "INJECTED" : "FAILED (not in flash mode or not supported)");
  } else if (command == "CORRUPT_STATE") {
    USB_SERIAL_PRINTLN(">>> FAILURE INJECTION: Corrupting EEPROM state...");
    bool result = telemetryPipeline.corruptPersistedState();
    if (result) {
      USB_SERIAL_PRINTF(">>> FAILURE INJECTION: State corruption INJECTED\n");
      USB_SERIAL_PRINTLN(">>> FAILURE INJECTION: Restart system to test recovery");
    } else {
      USB_SERIAL_PRINTF(">>> FAILURE INJECTION: State corruption FAILED (not in flash mode or not supported)\n");
    }
  } else if (command == "SIMULATE_POWER_LOSS") {
    USB_SERIAL_PRINTLN(">>> FAILURE INJECTION: Simulating power-loss during write...");
    bool result = telemetryPipeline.simulateIncompleteWrite();
    USB_SERIAL_PRINTF(">>> FAILURE INJECTION: Power-loss simulation %s\n", result ? "INJECTED" : "FAILED (not in flash mode or not supported)");
  } else if (command == "CORRUPT_POINTERS") {
    USB_SERIAL_PRINTLN(">>> FAILURE INJECTION: Corrupting ring buffer pointers...");
    bool result = telemetryPipeline.corruptRingPointers();
    USB_SERIAL_PRINTF(">>> FAILURE INJECTION: Pointer corruption %s\n", result ? "INJECTED" : "FAILED (not in flash mode or not supported)");
  } else if (command.startsWith("WEAR_TEST")) {
    int cycles = 100; // Default cycles
    if (command.indexOf(' ') > 0) {
      cycles = command.substring(command.indexOf(' ') + 1).toInt();
    }
    USB_SERIAL_PRINTF(">>> FAILURE INJECTION: Running accelerated wear test (%d cycles)...\n", cycles);
    bool result = telemetryPipeline.acceleratedWearTest(cycles);
    USB_SERIAL_PRINTF(">>> FAILURE INJECTION: Wear test %s\n", result ? "COMPLETED" : "FAILED (not in flash mode or not supported)");
  } else if (command.startsWith("RANDOM_CORRUPT")) {
    int num_sectors = 3; // Default number of sectors
    if (command.indexOf(' ') > 0) {
      num_sectors = command.substring(command.indexOf(' ') + 1).toInt();
    }
    USB_SERIAL_PRINTF(">>> FAILURE INJECTION: Injecting random corruption (%d sectors)...\n", num_sectors);
    bool result = telemetryPipeline.injectRandomCorruption(num_sectors);
    USB_SERIAL_PRINTF(">>> FAILURE INJECTION: Random corruption %s\n", result ? "INJECTED" : "FAILED (not in flash mode or not supported)");
  } else if (command == "PARTITION_FAIL") {
    USB_SERIAL_PRINTLN(">>> FAILURE INJECTION: Simulating partition failure...");
    bool result = telemetryPipeline.simulatePartitionFailure();
    if (result) {
      USB_SERIAL_PRINTF(">>> FAILURE INJECTION: Partition failure SIMULATED\n");
      USB_SERIAL_PRINTLN(">>> FAILURE INJECTION: Restart system to restore partition access");
    } else {
      USB_SERIAL_PRINTF(">>> FAILURE INJECTION: Partition failure FAILED (not in flash mode or not supported)\n");
    }
  } else if (command.startsWith("CORRUPT_CRC")) {
    int sector_num = 7; // Default sector
    if (command.indexOf(' ') > 0) {
      sector_num = command.substring(command.indexOf(' ') + 1).toInt();
    }
    USB_SERIAL_PRINTF(">>> FAILURE INJECTION: Corrupting sector %d CRC...\n", sector_num);
    bool result = telemetryPipeline.injectCRCCorruption(sector_num);
    USB_SERIAL_PRINTF(">>> FAILURE INJECTION: CRC corruption %s\n", result ? "INJECTED" : "FAILED (not in flash mode or not supported)");
  } else if (command == "ENABLE_FAIL_INJECT") {
    USB_SERIAL_PRINTLN(">>> FAILURE INJECTION: Checking failure injection mode...");
    telemetryPipeline.enableFailureInjection();
    USB_SERIAL_PRINTLN(">>> FAILURE INJECTION: If using FlashTelemetryManager in FLASH_ONLY mode, failure injection is available");
    USB_SERIAL_PRINTLN(">>> FAILURE INJECTION: If using TelemetryPipeline (PSRAM mode), failure injection is not supported");
  }
  #endif
  
  else {
    USB_SERIAL_PRINTF(">>> DIAGNOSTIC: Unknown command: %s\n", command.c_str());
  }
}

// sizeof is 108 rounded to 112 without badLengthUplinkMsgCount and badChkSumUplinkMsgCount
// add these in and sizeof is 116 rounded to 120 to keep on 8 byte boundary
struct LemonTelemetryForStorage 
// 108 bytes defined, but sizeof is rounded to 112 to keep on 8 byte boundary as there is a double present
// The sizeof struct is rounded up to the largest sizeof primitive that is present.
{
  double    gps_lat;              // must be on 8 byte boundary
  double    gps_lng;              // 
  uint32_t  goodUplinkMessageCount;
  uint32_t  badUplinkMessageCount;
//  uint32_t  badLengthUplinkMsgCount;
//  uint32_t  badChkSumUplinkMsgCount;
  uint32_t  consoleDownlinkMsgCount;
  uint32_t  telemetry_timestamp;       
  uint32_t  fixCount;                   // 36
  uint16_t  vBusVoltage;
  uint16_t  vBusCurrent;
  uint16_t  vBatVoltage;
  uint16_t  uplinkMessageMissingCount;          // 44   
  uint16_t  uplinkMessageLength;
  uint16_t  gps_hdop;
  uint16_t  gps_course_deg;
  uint16_t  gps_knots;            // 52
  
  uint32_t  downlink_send_duration;   // must be on 4 byte boundary
  uint32_t  uplink_preamble_latency;
  uint32_t  uplink_rx_latency;
  float     imu_lin_acc_x;
  float     imu_lin_acc_y;
  float     imu_lin_acc_z;
  float     imu_rot_acc_x;
  float     imu_rot_acc_y;
  float     imu_rot_acc_z;
  float     uplinkBadMessagePercentage;      // 92

  float     KBFromMako;               
  uint8_t   gps_hour;
  uint8_t   gps_minute;
  uint8_t   gps_second;
  uint8_t   gps_day;            // 100

  uint8_t   gps_month;
  uint8_t   gps_satellites;
  uint16_t  gps_year;           // 104

  uint32_t  four_byte_zero_padding;     // 108
};


uint32_t getSizeOfLemonTelemetryForStorage()
{
  return sizeof(LemonTelemetryForStorage);
}

struct MakoUplinkTelemetryForJson
{
  float depth;
  float water_pressure;
  float water_temperature;
  float enclosure_temperature;
  float enclosure_humidity;
  float enclosure_air_pressure;
  float magnetic_heading_compensated;
  float heading_to_target;
  float distance_to_target;
  float journey_course;
  float journey_distance;
  char  screen_display[3];
  uint16_t seconds_on;
  uint16_t user_action;
  uint16_t bad_checksum_msgs;
  float usb_voltage;
  float usb_current;
  char target_code[5];
    
  uint16_t minimum_sensor_read_time;
  uint16_t quietTimeMsBeforeUplink;
  uint16_t sensor_aquisition_time;
  uint16_t max_sensor_acquisition_time;
  uint16_t actual_sensor_acquisition_time;
  uint16_t max_actual_sensor_acquisition_time;
  
  float lsm_acc_x;
  float lsm_acc_y;
  float lsm_acc_z;
  float imu_gyro_x;
  float imu_gyro_y;
  float imu_gyro_z;
  float imu_lin_acc_x;
  float imu_lin_acc_y;
  float imu_lin_acc_z;
  float imu_rot_acc_x;
  float imu_rot_acc_y;
  float imu_rot_acc_z;
  uint16_t good_checksum_msgs;
  uint16_t way_marker_enum;
  char way_marker_label[3];
  char direction_metric[3];  
  bool console_requests_send_tweet;
  bool console_requests_emergency_tweet;
  uint16_t console_flags;
  uint32_t goodUplinkMessageCount;
  uint32_t badUplinkMessageCount;
  uint32_t lastGoodUplinkMessage;
  float KBFromMako;
};


String getStats()
{
  readings["fixCount"] = fixCount;
  readings["noFixCount"] = noFixCount;
  readings["gpsMissingMsgsSimActive"] = (forceGPSMissingGGARMCForTesting ? "ACTIVE" : "INACTIVE");
  readings["gpsOverrideNoFixSimActive"] = (overrideGPSToNoFixForTesting ? "ACTIVE" : "INACTIVE");
  readings["goodUplinkMessageCount"] = goodUplinkMessageCount;
  readings["privateMQTTUploadCount"] = privateMQTTUploadCount;
  readings["uplinkBadMessagePercentage"] = (int)uplinkBadMessagePercentage;
  readings["badLengthUplinkMsgCount"] = badLengthUplinkMsgCount;
  readings["badUplinkMessageCount"] = badUplinkMessageCount;
  readings["badChkSumUplinkMsgCount"] = badChkSumUplinkMsgCount;
  readings["uplinkMessageMissingCount"] = uplinkMessageMissingCount;
  readings["lemonUptime"] = (int)(millis() / 1000);
  readings["pipelineDraining"] = (telemetryPipeline.isPipelineDraining() ? "Yes" : "No");
  readings["pipelineLength"] = telemetryPipeline.getPipelineLength();
  readings["offlineThrottleApplied"] = (g_offlineStorageThrottleApplied ? "Yes" : "No");  
  readings["last_private_mqtt_upload_at"] = (int)(privateMQTT.getLastUploadTime() / 1000);
  readings["last_head_committed_at"] = (int)(last_head_committed_at / 1000);
  readings["lastCheckForInternetConnectivityAt"] = (int)(lastCheckForInternetConnectivityAt / 1000);

  readings["min_sens_read"] = latestMakoStats.minimum_sensor_read_time;
  readings["sens_read"] = latestMakoStats.sensor_aquisition_time;
  readings["max_sens_read"] = latestMakoStats.max_sensor_acquisition_time;
  readings["act_sens_read"] = latestMakoStats.actual_sensor_acquisition_time;
  readings["max_act_sens_read"] = latestMakoStats.max_actual_sensor_acquisition_time;
  readings["quiet_b4_uplink"] = latestMakoStats.quietTimeMsBeforeUplink;

  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // internal RAM, memory capable to store data or to create new task

  readings["free_heap_bytes"] = info.total_free_bytes;
  readings["largest_free_block"] = info.largest_free_block;
  readings["minimum_free_ever"] = info.minimum_free_bytes;

  String jsonString;
  serializeJson(readings, jsonString);

  return jsonString;
}

// This is only a test function for the Arduino neopixel UART
// The current reed state is latched, it gives the last known state
uint8_t checkForLanternLatestReedEvent()
{
  static uint8_t currentReedState = 0;  // Maintain state between calls
  
  while (serial_lantern_neopixels.available())
  {
    neopixelSerialByteRead = serial_lantern_neopixels.read();
    USB_SERIAL_PRINTF("Reed raw byte: %d\n", neopixelSerialByteRead);
    // have an indication on the screen of a byte read and which byte
    // these map to the reed switches that are in the float box
    if (neopixelSerialByteRead == 100)
    {
      currentReedState = 100;
      USB_SERIAL_PRINTF("Reed state set to: %d\n", currentReedState);
    }
    else if (neopixelSerialByteRead == 200)
    {
      currentReedState = 200;
      USB_SERIAL_PRINTF("Reed state set to: %d\n", currentReedState);
    }
    else if (neopixelSerialByteRead == 10)
    {
      currentReedState = 0;
      USB_SERIAL_PRINTF("Reed state set to: %d\n", currentReedState);
    }
    else
    {
      USB_SERIAL_PRINTF("Reed state left as is - not recognised neopixel code: %d\n", neopixelSerialByteRead);
    }
  }
  return currentReedState;
}

  /*
  // WARNING DO NOT ENABLE THIS UNLESS LEAK DETECTOR ACTUALLY FITTED
const char* leakAlarmMsg = "    Float\n\n    Leak!";
void checkForLeak(const char* msg)
{
  bool leakStatus = false;

  leakStatus = !(digitalRead(LEAK_DETECTOR_GPIO));

  if (leakStatus)
  {
    // M5.Lcd.fillScreen(TFT_RED);
    // M5.Lcd.setTextSize(3);
    // M5.Lcd.setCursor(5, 10);
    // M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
    // M5.Lcd.print(msg);
    delay(100);
    updateButtonsAndBuzzer();

    // M5.Lcd.fillScreen(TFT_ORANGE);
    // M5.Lcd.setCursor(5, 10);
    // M5.Lcd.setTextColor(TFT_YELLOW, TFT_ORANGE);
    // M5.Lcd.print(msg);
    delay(100);

    updateButtonsAndBuzzer();
    // M5.Lcd.setTextSize(2);
    // M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    // M5.Lcd.fillScreen(TFT_BLACK);
  }
}
    */

bool doesHeadCommitRequireForce(BlockHeader& block)
{
  bool forceHeadCommit = false;

  uint16_t maxPayloadSize = 0;
  uint8_t* makoPayloadBuffer = block.getBuffer(maxPayloadSize);

  // 1. parse the mako payload into the mako json payload struct
  const bool preventGlobalUpdate = true; // refactoring needed to remove this
  MakoUplinkTelemetryForJson makoJSON;
  decodeMakoUplinkMessageV5a(makoPayloadBuffer, makoJSON, preventGlobalUpdate);

enum e_user_action{NO_USER_ACTION=0x0000, HIGHLIGHT_USER_ACTION=0x0001,RECORD_BREADCRUMB_TRAIL_USER_ACTION=0x0002,LEAK_DETECTED_USER_ACTION=0x0004};

  if (makoJSON.user_action & HIGHLIGHT_USER_ACTION ||                 // PIN Record Activated
      makoJSON.user_action & RECORD_BREADCRUMB_TRAIL_USER_ACTION ||   // Track Record Activated
      makoJSON.user_action & LEAK_DETECTED_USER_ACTION)               // Leak Detected in Mako
  {
    // highlight action - requires forced head commit to upload every message.
    forceHeadCommit = true;
  }
  
  diveInProgress = (makoJSON.depth > 0.5);

  return forceHeadCommit;
}

bool checkForValidPreambleInReceiveBuffer(MakoDataPacket& makoPacket, int& preambleStart)
{
  // Process the received data - look for preamble and valid message
  bool validPreambleFound = false;
  preambleStart = -1;

  // State machine preamble detection - full pattern is "MBJMBJAEJ"
  // Look for "MBJ" first, then "AEJ" (allowing MBJAEJ, JMBJAEJ, BJMBJAEJ, MBJMBJAEJ)
  char uplink_preamble_first_segment[] = "MBJ";
  char uplink_preamble_second_segment[] = "AEJ";
  
  const char* nextByteToFind = uplink_preamble_first_segment;
  const char* nextSecondSegmentByteToFind = uplink_preamble_second_segment;
  
  // First pass: look for MBJ pattern
  for (int i = 0; i < makoPacket.length && *nextByteToFind != 0; i++) 
  {
      char next = makoPacket.data[i];
      if (next == *nextByteToFind) {
          nextByteToFind++;
      } else {
          nextByteToFind = uplink_preamble_first_segment;  // Reset search for first char of preamble
          if (next == *nextByteToFind) {  // Check if current char starts new pattern
              nextByteToFind++;
          }
      }
  }
  
  // Second pass: if MBJ found, look for AEJ pattern in remaining data
  if (*nextByteToFind == 0) { // MBJ pattern found
      for (int i = 0; i < makoPacket.length && *nextSecondSegmentByteToFind != 0; i++) 
      {
          char next = makoPacket.data[i];
          if (next == *nextSecondSegmentByteToFind) {
              nextSecondSegmentByteToFind++;
          } else {
              nextSecondSegmentByteToFind = uplink_preamble_second_segment;  // Reset search
              if (next == *nextSecondSegmentByteToFind) {  // Check if current char starts new pattern
                  nextSecondSegmentByteToFind++;
              }
          }
      }
      
      if (*nextSecondSegmentByteToFind == 0) { // AEJ pattern also found
          validPreambleFound = true;
          // Find the end of AEJ pattern to determine preamble start
          int segmentLen = strlen(uplink_preamble_second_segment);
          for (int i = makoPacket.length - 1; i >= segmentLen - 1; i--) {
              bool patternMatch = true;
              for (int j = 0; j < segmentLen; j++) {
                  if (makoPacket.data[i - segmentLen + 1 + j] != uplink_preamble_second_segment[j]) {
                      patternMatch = false;
                      break;
                  }
              }
              if (patternMatch) {
                  preambleStart = i + 1;
                  break;
              }
          }
          USB_SERIAL_PRINTF("2.0 preamble: Found MBJ...%s pattern, data starts at position %d\n", uplink_preamble_second_segment, preambleStart);
      }
  }
  return validPreambleFound;
}

bool checkForValidPreambleOnUplink()
{
  bool validPreambleFound = false;

  // If uplink messages are to be decoded look for pre-amble sequence on Serial Rx
  if (enableReadUplinkComms)
  {
    uint32_t nowUS = micros();

    downlinkSendMessageDurationMicroSeconds = (nowUS >= downlinkSendMessageDurationMicroSeconds ? nowUS - downlinkSendMessageDurationMicroSeconds : 0xFFFFFFFF - downlinkSendMessageDurationMicroSeconds + nowUS);

    uplinkLingerTimeoutAt = millis()+uplinkMessageLingerPeriodMs;

    preambleReceivedAfterMicroSeconds = micros();

    // 1.1 Read received data searching for lead-in pattern from Tracker - MBJ\0AEJ\0
    // wait upto uplinkLingerTimeoutAt milliseconds to receive the pre-amble

    char uplink_preamble_first_segment[] = "MBJ";
    char uplink_preamble_second_segment[] = "AEJ";

    const char* nextByteToFind = uplink_preamble_first_segment;
    const char* nextSecondSegmentByteToFind = uplink_preamble_second_segment;

    const int preambleMBJSize = 256;
    char  preambleMBJ[preambleMBJSize] = "Preamble MBJ: ";
    char* nextCharIndexForSerialOutput = preambleMBJ + strlen(preambleMBJ);

    while ((serial_mako_gopro.available() || 
            !serial_mako_gopro.available() && millis() < uplinkLingerTimeoutAt) && 
            *nextByteToFind != 0)
    {
      // throw away trash bytes from half-duplex clash - always present
      char next = serial_mako_gopro.read();
      if (next == *nextByteToFind)
      {
        if (writeTelemetryLogToSerial)
          *nextCharIndexForSerialOutput++ = (isalnum(next) ? next : '?');

        nextByteToFind++;
      }
      else
      {
        if (writeTelemetryLogToSerial)
          *nextCharIndexForSerialOutput++ = (isalnum(next) ? next : (next == 0 ? '0' : '?'));

        nextByteToFind = uplink_preamble_first_segment;    // make sure contiguous preamble found, reset search for first char of preamble
      }

      if (writeTelemetryLogToSerial)
        if (nextCharIndexForSerialOutput == preambleMBJ + preambleMBJSize-10)
          nextCharIndexForSerialOutput = preambleMBJ;
    }

    if (writeTelemetryLogToSerial)
    {
      *nextCharIndexForSerialOutput++ = '\n';  *nextCharIndexForSerialOutput++ = '\0';

      USB_SERIAL_PRINTF("%s", preambleMBJ);

      if (*nextByteToFind != 0)
          USB_SERIAL_PRINTF("    MBJ Timeout\n");
    }

    int preambleAEJSize = 256;
    char  preambleAEJ[preambleAEJSize] = "Preamble AEJ: ";
    nextCharIndexForSerialOutput = preambleAEJ + strlen(preambleAEJ);

    if (*nextByteToFind == 0)
    {
      // found an MBJ now find an AEJ, ignoring any other MBJs
     while ((serial_mako_gopro.available() || 
            !serial_mako_gopro.available() && millis() < uplinkLingerTimeoutAt) && 
            *nextSecondSegmentByteToFind != 0)
     {
        char next = serial_mako_gopro.read();
        if (next == *nextSecondSegmentByteToFind)
        {
          if (writeTelemetryLogToSerial)
            *nextCharIndexForSerialOutput++ = (isalnum(next) ? next : '?');
          nextSecondSegmentByteToFind++;
        }
        else
        {
          if (writeTelemetryLogToSerial)
            *nextCharIndexForSerialOutput++ = (isalnum(next) ? next : (next == 0 ? '\0' : '?'));
          nextSecondSegmentByteToFind = uplink_preamble_second_segment;    // make sure contiguous preamble found, reset search for first char of preamble
        }

        if (writeTelemetryLogToSerial)
          if (nextCharIndexForSerialOutput == preambleAEJ + preambleAEJSize-10)
            nextCharIndexForSerialOutput = preambleAEJ;
      }
    }
    else
    {
      if (writeTelemetryLogToSerial)
        USB_SERIAL_PRINTF("\nTimeout: Not Found preamble null terminator for MBJ\n");
    }

    if (writeTelemetryLogToSerial)
    {
      *nextCharIndexForSerialOutput++ = '\n';  *nextCharIndexForSerialOutput++ = '\0';
      USB_SERIAL_PRINTF("%s", preambleAEJ);

      if (*nextSecondSegmentByteToFind != 0)
      {
        if (writeLogToSerial && writeTelemetryLogToSerial)
          USB_SERIAL_PRINTF("    AEJ Timeout\n");
      }
    }

    if (*nextSecondSegmentByteToFind == 0)   // last byte of pre-amble found before no more bytes available
    {
      validPreambleFound = true;
      
      // message pre-amble found - read the rest of the received message.
      if (writeTelemetryLogToSerial)
        USB_SERIAL_PRINT("\nPre-Amble Found\n");
    }
    else
    {
      if (accumulateMissedMessageCount)  // (must be at least 10 seconds since power on)
        uplinkMessageMissingCount++;
    }
  }
  else
  {
    // ignore any Serial Rx/Uplink bytes
  }

  uint32_t nowUS = micros();
  uplinkRxMicroSeconds = nowUS;

  if (validPreambleFound)
  {
    preambleReceivedAfterMicroSeconds = (nowUS >= preambleReceivedAfterMicroSeconds ? nowUS - preambleReceivedAfterMicroSeconds : 0xFFFFFFFF - preambleReceivedAfterMicroSeconds + nowUS);
  }
  else
  {
    preambleReceivedAfterMicroSeconds = 0; 
  }

  return validPreambleFound;
}

#define BUILD_INCLUDE_MAIN_PART3

#include "main_part3.cpp"

#endif