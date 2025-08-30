#ifdef BUILD_INCLUDE_MAIN_PART2

#include "MercatorMQTT.h"
#include "SerialConfig.h"

// NOTE: SerialConfig.h now provides all serial macros and extern declarations


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
  readings["last_private_mqtt_upload_at"] = (float)((int)((float)(privateMQTT.getLastUploadTime())/100.0))/10.0;
  readings["last_head_committed_at"] = (float)((int)((float)(last_head_committed_at)/100.0))/10.0;
  readings["lastCheckForInternetConnectivityAt"] = (float)((int)((float)(lastCheckForInternetConnectivityAt)/100.0))/10.0;

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
      if (accumulateMissedMessageCount && nofix_msg_loop_count == -1)  // (must be at least 10 seconds since power on and first fix received)
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

bool populateHeadWithMakoTelemetry(BlockHeader& headBlock, const bool validPreambleFound, const uint8_t* packetData, int dataLength)
{
  bool messageValidatedOk = false;

  uint16_t blockMaxPayload = 0;
  uint8_t* blockBuffer = headBlock.getBuffer(blockMaxPayload);
  uint8_t* nextBlockByte = blockBuffer;
  uint16_t headMaxPayloadSize = headBlock.getMaxPayloadSize();

  // 3. Populate the head block buffer with mako telemetry (or nullptr if no pre-amble found)
  if (validPreambleFound && packetData != nullptr && dataLength > 0)
  {
    uplinkRxMicroSeconds = micros();

    // 3.1a Copy packet data into blockBuffer
    int bytesToCopy = (dataLength < headMaxPayloadSize) ? dataLength : headMaxPayloadSize;
    memcpy(nextBlockByte, packetData, bytesToCopy);
    nextBlockByte += bytesToCopy;

    uint32_t nowUS = micros();
    uplinkRxMicroSeconds = (nowUS >= uplinkRxMicroSeconds ? nowUS - uplinkRxMicroSeconds : 0xFFFFFFFF - uplinkRxMicroSeconds + nowUS);

    if (writeTelemetryLogToSerial)
      USB_SERIAL_PRINTF("Rx Time: %lu\n",uplinkRxMicroSeconds);
 
    receivedUplinkMessageCount++;
  }
  else
  {
    // 3.1b uplink mako struct to Quibitro will be zero fields
    memset(nextBlockByte,0,makoHardcodedUplinkMessageLength);
    nextBlockByte+=makoHardcodedUplinkMessageLength;
  }
      
  // entire message received and stored into blockBuffer (makoHardcodedUplinkMessageLength)
  uint16_t uplinkMessageLength = nextBlockByte-blockBuffer;

  if (writeTelemetryLogToSerial)
  {
    USB_SERIAL_PRINTF("Mako uplinkMessageLength == %hu    ",uplinkMessageLength);

    char  firstLastBytes[512]="";
    
    char* nextIndex = firstLastBytes;

    if (uplinkMessageLength > 10)
    {
      int bytesToDisplay=5;

      for (int i=0; i<bytesToDisplay; i++)
        *nextIndex++ = (isalnum(*(blockBuffer+i)) ? *(blockBuffer+i) : '?');

      *nextIndex++=' ';
      *nextIndex++=' ';
      *nextIndex++=' ';

      for (int i=uplinkMessageLength-bytesToDisplay; i<uplinkMessageLength; i++)
        *nextIndex++ = (isalnum(*(blockBuffer+i)) ? *(blockBuffer+i) : '?');
    }
    *nextIndex++='\n';
    *nextIndex++='\0';

    USB_SERIAL_PRINTF("%s", firstLastBytes);
  }

  // check integrity of Mako message here - increment good count or bad count
  if (validPreambleFound)
  {
    if (enableAllUplinkMessageIntegrityChecks)
    {
      uint16_t uplink_checksum = 0;
      
      if (uplinkMessageLength > 2 && (uplinkMessageLength % 2) == 0)
        uplink_checksum = *((uint16_t*)(blockBuffer + uplinkMessageLength - 2));
      else
      {
        USB_SERIAL_PRINTF("decodeUplink bad msg length %%2!=0 %hu  Rx Time: %lu\n", uplinkMessageLength, uplinkRxMicroSeconds);

        headBlock.resetPayload();

        badUplinkMessageCount++;

        badLengthUplinkMsgCount++;
        
        messageValidatedOk = false;              
        return messageValidatedOk;
      }

      bool uplink_checksum_bad = (uplink_checksum != calcUplinkChecksum((char*)blockBuffer,uplinkMessageLength-2));
      bool uplinkMessageLengthBad = (uplinkMessageLength != makoHardcodedUplinkMessageLength);

      // hardcoding needs to be removed and replaced with length check according to msgtype
      if (uplinkMessageLengthBad || uplink_checksum_bad)
      {
        if (uplinkMessageLengthBad)
          USB_SERIAL_PRINTF("decodeUplink bad msg length %hu && checksum bad %hu  Rx Time: %lu\n", uplinkMessageLength, uplink_checksum, uplinkRxMicroSeconds);
        else if (uplinkMessageLengthBad)
          USB_SERIAL_PRINTF("decodeUplink bad msg length only %hu  Rx Time: %lu\n", uplinkMessageLength, uplinkRxMicroSeconds);
        else if (uplink_checksum_bad)
          USB_SERIAL_PRINTF("decodeUplink bad msg checksum only %hu  Rx Time: %lu\n", uplink_checksum, uplinkRxMicroSeconds);
        
        // clear blockBuffer
        headBlock.resetPayload();

        badUplinkMessageCount++;

        if (uplinkMessageLengthBad)
          badLengthUplinkMsgCount++;
        else if (uplink_checksum_bad)
          badChkSumUplinkMsgCount++;

        messageValidatedOk = false;              
        return messageValidatedOk;  // this is going to stop any further messages to be uploaded if there are repeated checksum failures.
        // for now live with this.
      }
      else
      {
        goodUplinkMessageCount++;
      }
    }
    else
    {
      // no checksum validation, assume good uplink message
      goodUplinkMessageCount++;
    }
  }
  else
  {
    // No valid preamble found (or readuplinkcomms disabled)
    // do not increment checksum counts good/bad.
  }
  
  messageValidatedOk = true;

  // finished processing the uplink Message

  // round up nextBlockByte to 8 byte boundary if needed (120)
  while ((nextBlockByte-blockBuffer) < blockMaxPayload && (nextBlockByte-blockBuffer)%8 != 0)
    *(nextBlockByte++)=0;

  headBlock.setRoundedUpPayloadSize(nextBlockByte-blockBuffer);

  return messageValidatedOk;
}

void populateHeadWithLemonTelemetryAndCommit(BlockHeader& headBlock)
{
  uint16_t roundedUpLength = headBlock.getRoundedUpPayloadSize();
  
  uint16_t blockMaxPayload = 0;
  uint8_t* blockBuffer = headBlock.getBuffer(blockMaxPayload);
  uint8_t* nextBlockByte = blockBuffer+roundedUpLength;

  if (writeTelemetryLogToSerial)
    USB_SERIAL_PRINTF("Mako roundedUpLength == %hu\n",roundedUpLength);

  uint16_t totalMakoAndLemonLength = roundedUpLength + sizeof(LemonTelemetryForStorage);

  // populate basictelemetry

  // construct lemon telemetry, append to the padded mako telemetry message and commit to the telemetry pipeline
  if (totalMakoAndLemonLength <= blockMaxPayload)
  {
    LemonTelemetryForStorage lemon_telemetry_for_storage;
    constructLemonTelemetryForStorage(lemon_telemetry_for_storage, latestLemonTelemetry, uplinkMessageLength);
    
    memcpy(nextBlockByte, (uint8_t*)&lemon_telemetry_for_storage,sizeof(LemonTelemetryForStorage));
    
    if (writeTelemetryLogToSerial)
      USB_SERIAL_PRINTF("memcpy done LemonTelemetryForStorage == sizeof %i\n",sizeof(LemonTelemetryForStorage));

    nextBlockByte+=sizeof(LemonTelemetryForStorage);

    if (writeTelemetryLogToSerial)
      USB_SERIAL_PRINTF("totalMakoAndLemonLength %hu\n",totalMakoAndLemonLength);

    headBlock.setPayloadSize(totalMakoAndLemonLength);

    bool isPipelineFull=false;
    telemetryPipeline.commitPopulatedHeadBlock(headBlock, isPipelineFull);
  
    USB_SERIAL_PRINTF("Commit head block: maxpipeblocklength=%hu longestpipe=%hu pipelineLength=%hu TH=%hu,%hu\n",telemetryPipeline.getMaximumPipelineLength(),telemetryPipeline.getMaximumDepth(),telemetryPipeline.getPipelineLength(),telemetryPipeline.getTailBlockIndex(),telemetryPipeline.getHeadBlockIndex());
  }
  else
  {
    // payload too large to fit into block
    USB_SERIAL_PRINTF("Combined Mako (%hu) and Lemon (%lu) payloads too large (%hu) to fit into telemetry block (%hu)\n",uplinkMessageLength,sizeof(LemonTelemetryForStorage),totalMakoAndLemonLength,blockMaxPayload);
  }
}

void getNextTelemetryMessagesUploadedToPrivateMQTT()
{
  extern MercatorMQTT privateMQTT;
  BlockHeader tailBlock;
  const uint8_t maxTailPullsPerCycle = 10;   // allow up to 10 messages per cycle
  uint8_t tailPulls = maxTailPullsPerCycle;

  if (!privateMQTT.canUpload()) // upload throttle and connectivity check.
    return;

  while (telemetryPipeline.pullTailBlock(tailBlock) && tailPulls)
  {
    tailPulls--;
    
    if (writeTelemetryLogToSerial)
      USB_SERIAL_PRINTF("tail block pulled\n");

    uint16_t maxPayloadSize = 0;
    uint8_t* makoPayloadBuffer = tailBlock.getBuffer(maxPayloadSize);
    uint16_t combinedBufferSize = tailBlock.getPayloadSize();
    const uint16_t roundedUpLength = tailBlock.getRoundedUpPayloadSize();

    // 1. parse the mako payload into the mako json payload struct
    MakoUplinkTelemetryForJson makoJSON;
    const bool preventGlobalUpdate = false; // refactoring needed to remove this
    decodeMakoUplinkMessageV5a(makoPayloadBuffer, makoJSON, preventGlobalUpdate);

    checkMakoJSONForAlarms(makoJSON);

    // 2. parse the lemon payload into the lemon json payload struct
    LemonTelemetryForJson lemonForUpload;
    decodeIntoLemonTelemetryForUpload(makoPayloadBuffer+roundedUpLength, combinedBufferSize - roundedUpLength, lemonForUpload);

    // 3. construct the JSON message from the two structs and send MQTT to Private MQTT
    e_q_upload_status uploadStatus=Q_SUCCESS;
    e_q_upload_status uploadStatusPrivateMQTT=Q_SUCCESS;

    if (enableConnectToPrivateMQTT && enableUploadToPrivateMQTT)
        uploadStatus = uploadStatusPrivateMQTT = uploadTelemetryToPrivateMQTT(&makoJSON, &lemonForUpload);


    // 5. If sent ok then commit (or no send to Qubitro required), otherwise do nothing
    if (uploadStatus & 0x01 == Q_SUCCESS)
    {
      telemetryPipeline.tailBlockCommitted();
      
      g_offlineStorageThrottleApplied = false;
      
      USB_SERIAL_PRINTF("tail block committed:  pipelineLength=%hu TH=%hu,%hu\n",telemetryPipeline.getPipelineLength(),telemetryPipeline.getTailBlockIndex(),telemetryPipeline.getHeadBlockIndex());
    }
    else
    {
      USB_SERIAL_PRINTF("tail block NOT committed\n");

      break;    // do not attempt any more tail pulls this event cycle
    }
  }
}

// TinyGPSPlus must be non-const as act of getting lat and lng resets the updated flag
void populateCurrentLemonTelemetry(LemonTelemetryForJson& l, TinyGPSPlus& g)
{
  l.gps_lat =  g.location.lat(); l.gps_lng = g.location.lng();
  l.gps_hdop = g.hdop.hdop();    l.gps_course_deg = g.course.deg(); l.gps_knots = g.speed.knots();
  l.gps_hour = g.time.hour();    l.gps_minute =  g.time.minute();   l.gps_second =  g.time.second();
  l.gps_day =  g.date.day();     l.gps_month =  g.date.month();
  l.gps_year = g.date.year();
  l.gps_satellites =             g.satellites.value();

  getM5ImuSensorData(l);
}

void populateFinalLemonTelemetry(LemonTelemetryForJson& l)
{
  l.downlink_send_duration = downlinkSendMessageDurationMicroSeconds;
  l.uplink_preamble_latency = preambleReceivedAfterMicroSeconds;
  l.uplink_rx_latency = uplinkRxMicroSeconds;
}

void constructLemonTelemetryForStorage(struct LemonTelemetryForStorage& s, const LemonTelemetryForJson l, const uint16_t uplinkMessageLength)
{
  s.gps_lat = l.gps_lat;  s.gps_lng = l.gps_lng;          // must be on 8 byte boundary 
  s.goodUplinkMessageCount = goodUplinkMessageCount;      // GLOBAL
  s.badUplinkMessageCount = badUplinkMessageCount;      // GLOBAL
//  s.badLengthUplinkMsgCount = badLengthUplinkMsgCount;      // GLOBAL
//  s.badChkSumUplinkMsgCount = badChkSumUplinkMsgCount;      // GLOBAL  
  s.consoleDownlinkMsgCount = consoleDownlinkMsgCount;    // GLOBAL
  s.telemetry_timestamp = lastGoodUplinkMessage;          // GLOBAL
  s.fixCount = fixCount;                                  // GLOBAL
  s.vBusVoltage = (uint16_t)(0.11);
  s.vBusCurrent = (uint16_t)(0.11);
  s.vBatVoltage = (uint16_t)(0.11);
  s.uplinkMessageMissingCount = (uint16_t)(uplinkMessageMissingCount);          // 40
  s.uplinkMessageLength = uplinkMessageLength;            // GLOBAL
  s.gps_hdop = (uint16_t)(l.gps_hdop * 10.0);
  s.gps_course_deg = (uint16_t)(l.gps_course_deg * 10.0);
  s.gps_knots = (uint16_t)(l.gps_knots * 10.0);            // 48
  
  s.downlink_send_duration = l.downlink_send_duration; 
  s.uplink_preamble_latency = l.uplink_preamble_latency; 
  s.uplink_rx_latency = l.uplink_rx_latency;
  s.imu_lin_acc_x = l.imu_lin_acc_x; s.imu_lin_acc_y = l.imu_lin_acc_y; s.imu_lin_acc_z = l.imu_lin_acc_z;
  s.imu_rot_acc_x = l.imu_rot_acc_x; s.imu_rot_acc_y = l.imu_rot_acc_y; s.imu_rot_acc_z = l.imu_rot_acc_z;
  s.uplinkBadMessagePercentage = uplinkBadMessagePercentage;      // 88

  s.KBFromMako = KBFromMako;                             // GLOBAL
  s.gps_hour = l.gps_hour; s.gps_minute = l.gps_minute;  s.gps_second = l.gps_second;
  s.gps_day = l.gps_day; s.gps_month = l.gps_month; s.gps_satellites = (uint8_t)l.gps_satellites;
  s.gps_year =  l.gps_year;         // 100     

  s.four_byte_zero_padding = 0;     // 104
}

//  uint32_t  l.privateMQTTUploadCount;
//  float     l.KBToPrivateMQTT;
//  uint32_t  l.live_metrics_count;
//  uint32_t  l.privateMQTTUploadDutyCycle;
//  uint16_t  l.privateMQTTMessageLength = privateMQTTMessageLength;

uint8_t decode_uint8(uint8_t*& msg) 
{ 
  return  *(msg++);
}

uint16_t decode_uint16(uint8_t*& msg) 
{ 
  // copy 2 bytes out of msg
  uint16_t r = *(msg++) + ((*(msg++)) << 8);
  return r;
}

uint32_t decode_uint32(uint8_t*& msg) 
{
  // copy 4 bytes out of msg
  uint32_t r = *(msg++) + ((*(msg++)) << 8) + ((*(msg++)) << 16) + ((*(msg++)) << 24);
  return r;
}

float decode_float(uint8_t*& msg) 
{ 
  char* p = nullptr;
  float f = 0.0; 
  
  // copy 4 bytes out of msg
  p = (char*)&f; *(p++) = *(msg++); *(p++) = *(msg++); *(p++) = *(msg++); *(p++) = *(msg++); 

  return  f;
}

double decode_double(uint8_t*& msg) 
{ 
  char* p = nullptr;
  double d = 0.0; 

  // copy 8 bytes out of msg
  p = (char*)&d; *(p++) = *(msg++); *(p++) = *(msg++); *(p++) = *(msg++); *(p++) = *(msg++); *(p++) = *(msg++); *(p++) = *(msg++); *(p++) = *(msg++); *(p++) = *(msg++);
  return  d;
}

void decode_uint16_into_3_char_array(uint8_t*& msg, char* target)
{
  uint16_t twoBytes = decode_uint16(msg);

  target[0] = (twoBytes & 0x00FF);
  target[1] = ((twoBytes & 0xFF00) >> 8);
  target[2] = '\0';
}

void decode_uint32_into_5_char_array(uint8_t*& msg, char* target)
{
  uint16_t twoBytes = decode_uint16(msg);

  target[0] = (twoBytes & 0x00FF);
  target[1] = ((twoBytes & 0xFF00) >> 8);

  twoBytes = decode_uint16(msg);
  target[2] = (twoBytes & 0x00FF);
  target[3] = ((twoBytes & 0xFF00) >> 8);

  target[4] = '\0';
}

bool decodeIntoLemonTelemetryForUpload(uint8_t* msg, const uint16_t length, struct LemonTelemetryForJson& l)
{
  l.gps_lat = decode_double(msg);
  l.gps_lng = decode_double(msg);         
  l.goodUplinkMessageCount = decode_uint32(msg);
  l.badUplinkMessageCount = decode_uint32(msg);
//  l.badLengthUplinkMsgCount = decode_uint32(msg);
//  l.badChkSumUplinkMsgCount = decode_uint32(msg); 
  l.consoleDownlinkMsgCount = decode_uint32(msg);
  l.telemetry_timestamp = decode_uint32(msg);
  l.fixCount = decode_uint32(msg);
  l.vBusVoltage = ((float)decode_uint16(msg)) / 1000.0;
  l.vBusCurrent = ((float)decode_uint16(msg)) / 100.0;
  l.vBatVoltage = ((float)decode_uint16(msg)) / 1000.0;
  l.uplinkMessageMissingCount = decode_uint16(msg);
  l.uplinkMessageLength = decode_uint16(msg);
  l.gps_hdop = ((float)decode_uint16(msg)) / 10.0;
  l.gps_course_deg = ((float)decode_uint16(msg)) / 10.0;
  l.gps_knots = ((float)decode_uint16(msg)) / 10.0;        // 44

  l.downlink_send_duration = decode_uint32(msg);
  l.uplink_preamble_latency = decode_uint32(msg);
  
  l.uplink_rx_latency = decode_uint32(msg);
  l.imu_lin_acc_x = decode_float(msg);
  l.imu_lin_acc_y = decode_float(msg);
  l.imu_lin_acc_z = decode_float(msg);
  l.imu_rot_acc_x = decode_float(msg);
  l.imu_rot_acc_y = decode_float(msg);
  l.imu_rot_acc_z = decode_float(msg);
  l.uplinkBadMessagePercentage = decode_float(msg);   // 88

  l.KBFromMako = decode_float(msg);
  l.gps_hour = decode_uint8(msg);
  l.gps_minute = decode_uint8(msg);
  l.gps_second = decode_uint8(msg);
  l.gps_day = decode_uint8(msg);
  l.gps_month = decode_uint8(msg);
  l.gps_satellites = decode_uint8(msg);
  l.gps_year = decode_uint16(msg);    // 100
  
  return true;
}

void checkMakoJSONForAlarms(struct MakoUplinkTelemetryForJson& m)
{
  if (m.user_action & LEAK_DETECTED_USER_ACTION)
  {
      makoReportsLeak = true;
  }
}

// uplink msg from mako is 114 bytes
bool decodeMakoUplinkMessageV5a(uint8_t* uplinkMsg, struct MakoUplinkTelemetryForJson& m, const bool preventGlobalUpdate)
{
  bool result = false;

  uint16_t uplink_length = decode_uint16(uplinkMsg);
  uint16_t uplink_msgtype = decode_uint16(uplinkMsg);

  m.depth = ((float)decode_uint16(uplinkMsg)) / 10.0;
  m.water_pressure = ((float)decode_uint16(uplinkMsg)) / 100.0;
  m.water_temperature = ((float)decode_uint16(uplinkMsg)) / 10.0;
  
  m.enclosure_temperature = ((float)decode_uint16(uplinkMsg)) / 10.0;
  m.enclosure_humidity = ((float)decode_uint16(uplinkMsg)) / 10.0;
  m.enclosure_air_pressure = ((float)decode_uint16(uplinkMsg)) / 10.0;
  m.magnetic_heading_compensated = ((float)decode_uint16(uplinkMsg)) / 10.0;

  m.heading_to_target = ((float)decode_uint16(uplinkMsg)) / 10.0;
  m.distance_to_target = ((float)decode_uint16(uplinkMsg)) / 10.0;
  m.journey_course = ((float)decode_uint16(uplinkMsg)) / 10.0;
  m.journey_distance = ((float)decode_uint16(uplinkMsg)) / 100.0;

  decode_uint16_into_3_char_array(uplinkMsg, m.screen_display);

  m.seconds_on = decode_uint16(uplinkMsg);
  m.user_action = decode_uint16(uplinkMsg);

  m.bad_checksum_msgs = decode_uint16(uplinkMsg);
  m.usb_voltage = ((float)decode_uint16(uplinkMsg)) / 1000.0;
  m.usb_current = ((float)decode_uint16(uplinkMsg)) / 100.0;

  decode_uint32_into_5_char_array(uplinkMsg,m.target_code);

  char* stripChar = strchr(m.target_code,' ');
  if (stripChar)
    *stripChar = '\0';                // strip any trailing space

  stripChar = strchr(m.target_code,'\n');
  if (stripChar)
    *stripChar = '\0';      // strip any trailing newline

  m.minimum_sensor_read_time = decode_uint16(uplinkMsg);
  m.quietTimeMsBeforeUplink = decode_uint16(uplinkMsg);
  m.sensor_aquisition_time = decode_uint16(uplinkMsg);
  m.max_sensor_acquisition_time = decode_uint16(uplinkMsg);
  m.actual_sensor_acquisition_time = decode_uint16(uplinkMsg);
  m.max_actual_sensor_acquisition_time = decode_uint16(uplinkMsg);

  m.lsm_acc_x = decode_float(uplinkMsg); m.lsm_acc_y = decode_float(uplinkMsg);  m.lsm_acc_z = decode_float(uplinkMsg);

  m.imu_gyro_x = decode_float(uplinkMsg); m.imu_gyro_y = decode_float(uplinkMsg); m.imu_gyro_z = decode_float(uplinkMsg);
  m.imu_lin_acc_x = decode_float(uplinkMsg); m.imu_lin_acc_y = decode_float(uplinkMsg); m.imu_lin_acc_z = decode_float(uplinkMsg);
  m.imu_rot_acc_x = decode_float(uplinkMsg); m.imu_rot_acc_y = decode_float(uplinkMsg); m.imu_rot_acc_z = decode_float(uplinkMsg);

  m.good_checksum_msgs = decode_uint16(uplinkMsg);

  m.way_marker_enum = decode_uint16(uplinkMsg);
  
  decode_uint16_into_3_char_array(uplinkMsg, m.way_marker_label);
  decode_uint16_into_3_char_array(uplinkMsg, m.direction_metric);
 
  m.console_flags = decode_uint16(uplinkMsg);

  // must include this otherwise will not decode rest of message correctly
  uint16_t uplink_checksum = decode_uint16(uplinkMsg);    // not including in MakoUplinkTelemetryForJson struct
  
  m.console_requests_send_tweet = (m.console_flags & 0x01);
  m.console_requests_emergency_tweet = (m.console_flags & 0x02);

  //  USB_SERIAL.printf("decodeUplink good msg: %d msg type\n",uplink_msgtype);

  m.goodUplinkMessageCount = goodUplinkMessageCount;
  m.lastGoodUplinkMessage = lastGoodUplinkMessage;
  m.KBFromMako = KBFromMako;

/* GLOBALS - need to remove/refactor*/
  if (!preventGlobalUpdate)
  {
    lastGoodUplinkMessage = millis();
    KBFromMako = KBFromMako + (((float)uplink_length) / 1024.0);
  
    uplinkMessageLength = uplink_length;
  }
  
  result = true;

  return result;
}

uint16_t calcUplinkChecksum(char* buffer, uint16_t length)
{
  uint16_t* word_buffer = (uint16_t*)buffer;
  uint16_t word_length = length / 2;

  uint16_t checksum = 0;

  while (word_length--)
    checksum = checksum ^ *(word_buffer++);

  return checksum;
}

const char* fake_no_fix = "$GPRMC,235316.000,A,4003.9040,N,10512.5792,W,0.09,144.75,141112,,*19\n";

void sendFakeGPSData_No_Fix()
{
  serial_mako_gopro.write(fake_no_fix);
  delay(100);
}

const char* fake_no_gps = "$GPRMC,092204.999,A,4250.5589,S,14718.5084,E,0.00,89.68,211200,,*25\n";

void sendFakeGPSData_No_GPS()
{
  serial_mako_gopro.write(fake_no_gps);
  delay(100);
}

void buildUplinkTelemetryMessageV6a(char* payload, const struct MakoUplinkTelemetryForJson& m, const struct LemonTelemetryForJson& l)
{
  currentPrivateMQTTUploadAt = millis();
  privateMQTTUploadDutyCycle = currentPrivateMQTTUploadAt - lastPrivateMQTTUploadAt;

  uint32_t live_metrics_count = 75; // as of 9 May 2023
  
  sprintf(payload,
          "{\"UTC_time\":\"%02d:%02d:%02d\",\"UTC_date\":\"%02d:%02d:%02d\",\"lemon_on_mins\":%lu,\"coordinates\":[%f,%f],\"depth\":%f,"
          "\"water_pressure\":%f,\"water_temperature\":%f,\"enclosure_temperature\":%f,\"enclosure_humidity\":%f,\"enclosure_air_pressure\":%f,"
          "\"magnetic_heading_compensated\":%f,\"heading_to_target\":%f,\"distance_to_target\":%f,\"journey_course\":%f,\"journey_distance\":%f,"
          "\"mako_screen_display\":\"%s\",\"mako_on_mins\":%lu,\"mako_user_action\":%d,\"mako_rx_bad_checksum_msgs\":%hu,"
          "\"mako_usb_voltage\":%f,\"mako_usb_current\":%f,\"mako_target_code\":\"%s\","
          "\"fix_count\":%lu,\"lemon_usb_voltage\":%f,\"lemon_usb_current\":%f,\"lemon_bat_voltage\":%f,\"uplink_missing_msgs_from_mako\":%hu,"
          "\"sats\":%lu,\"hdop\":%f,\"gps_course\":%f,\"gps_speed_knots\":%f,"

          "\"min_sens_read\":%hu,\"quiet_b4_uplink\":%hu,\"sens_read\":%hu,\"max_sens_read\":%hu,\"act_sens_read\":%hu,\"max_act_sens_read\":%hu,"

          "\"mako_lsm_acc_x\":%f,\"mako_lsm_acc_y\":%f,\"mako_lsm_acc_z\":%f,"

          "\"mako_imu_gyro_x\":%f,\"mako_imu_gyro_y\":%f,\"mako_imu_gyro_z\":%f,"
          "\"mako_imu_lin_acc_x\":%f,\"mako_imu_lin_acc_y\":%f,\"mako_imu_lin_acc_z\":%f,"
          "\"mako_imu_rot_acc_x\":%f,\"mako_imu_rot_acc_y\":%f,\"mako_imu_rot_acc_z\":%f,"
          "\"mako_rx_good_checksum_msgs\":%hu,"

          "\"downlink_send_duration\":%lu,\"uplink_preamble_latency\":%lu,\"uplink_rx_latency\":%lu,"
          "\"lemon_imu_lin_acc_x\":%f,\"lemon_imu_lin_acc_y\":%f,\"lemon_imu_lin_acc_z\":%f,"
          "\"lemon_imu_rot_acc_x\":%f,\"lemon_imu_rot_acc_y\":%f,\"lemon_imu_rot_acc_z\":%f,"
          "\"uplink_bad_percentage\":%.1f,"

          "\"mako_waymarker_e\":%d,\"mako_waymarker_label\":\"%s\",\"mako_direction_metric\":\"%s\","

          "\"uplink_good_msgs_from_mako\":%lu,\"uplink_bad_msgs_from_mako\":%lu,\"uplink_msg_length\":%hu,\"msgs_to_qubitro\":%d,\"qubitro_msg_length\":%hu,\"KB_to_qubitro\":%.1f,\"KB_uplinked_from_mako\":%.1f,"
          "\"live_metrics\":%lu,\"qubitro_upload_duty_cycle\":%lu,\"console_downlink_msg\":%lu,\"geo_location\":\"Gozo, Malta\""
          "}",

          // with bad length and bad checksum stats
          //           "\"uplink_good_msgs_from_mako\":%lu,\"uplink_bad_msgs_from_mako\":%lu,\"uplink_bad_len_msgs_from_mako\":%lu,\"uplink_bad_chk_msgs_from_mako\":%lu,\"uplink_msg_length\":%hu,\"msgs_to_qubitro\":%d,\"qubitro_msg_length\":%hu,\"KB_to_qubitro\":%.1f,\"KB_uplinked_from_mako\":%.1f,"

          l.gps_hour, l.gps_minute, l.gps_second,
          l.gps_day, l.gps_month, l.gps_year,
          currentPrivateMQTTUploadAt / 1000 / 60,   // lemon on minutes
          l.gps_lat, l.gps_lng,
          m.depth, m.water_pressure, m.water_temperature,
          m.enclosure_temperature, m.enclosure_humidity, m.enclosure_air_pressure,
          m.magnetic_heading_compensated, m.heading_to_target, m.distance_to_target,
          m.journey_course, m.journey_distance,
          m.screen_display,
          m.seconds_on,
          m.user_action,
          m.bad_checksum_msgs, m.usb_voltage, m.usb_current, 
          
          m.target_code,

          l.fixCount,
          
          l.vBusVoltage, l.vBusCurrent, l.vBatVoltage, l.uplinkMessageMissingCount,

          l.gps_satellites, l.gps_hdop, l.gps_course_deg, l.gps_knots,
          
          m.minimum_sensor_read_time, m.quietTimeMsBeforeUplink, m.sensor_aquisition_time,  
          m.max_sensor_acquisition_time, m.actual_sensor_acquisition_time, m.max_actual_sensor_acquisition_time,

          m.lsm_acc_x, m.lsm_acc_y, m.lsm_acc_z,

          m.imu_gyro_x,    m.imu_gyro_y,    m.imu_gyro_z,
          m.imu_lin_acc_x, m.imu_lin_acc_y, m.imu_lin_acc_z,
          m.imu_rot_acc_x, m.imu_rot_acc_y, m.imu_rot_acc_z,
          m.good_checksum_msgs,
          l.downlink_send_duration,
          l.uplink_preamble_latency,    
          l.uplink_rx_latency,
          l.imu_lin_acc_x, l.imu_lin_acc_y, l.imu_lin_acc_z,
          l.imu_rot_acc_x, l.imu_rot_acc_y, l.imu_rot_acc_z,
          l.uplinkBadMessagePercentage,

          m.way_marker_enum, m.way_marker_label, m.direction_metric,
          
          l.goodUplinkMessageCount,
          l.badUplinkMessageCount,
//          l.badLengthUplinkMsgCount,
//          l.badChkSumUplinkMsgCount,
          l.uplinkMessageLength,
          privateMQTTUploadCount,
          privateMQTTMessageLength,             ///  ????
          KBToPrivateMQTT,                      ///  ????
          l.KBFromMako,
          live_metrics_count,
          privateMQTTUploadDutyCycle,           ///  ????
          l.consoleDownlinkMsgCount
          
          // DO NOT POPULATE (HARDCODED IN SPRINTF STRING) geo_location
         );

  extern uint16_t privateMQTTMessageLength;
  extern float KBToPrivateMQTT;
  
  privateMQTTMessageLength = strlen(payload);
  KBToPrivateMQTT += (((float)(privateMQTTMessageLength)) / 1024.0);

  lastPrivateMQTTUploadAt = millis();

  // update last uploaded mako stats
  latestMakoStats=MakoStats(m.minimum_sensor_read_time, m.quietTimeMsBeforeUplink,m.sensor_aquisition_time, 
                            m.max_sensor_acquisition_time, m.actual_sensor_acquisition_time, m.max_actual_sensor_acquisition_time);
}

void buildBasicTelemetryMessage(char* payload)
{
  sprintf(payload, "{\"lat\":%f,\"lng\":%f}",  gps.location.lat(), gps.location.lng());
}

enum e_q_upload_status uploadTelemetryToPrivateMQTT(MakoUplinkTelemetryForJson* makoTelemetry, struct LemonTelemetryForJson* lemonTelemetry)
{
  extern MercatorMQTT privateMQTT;
  enum e_q_upload_status uploadStatus = Q_UNDEFINED_ERROR;

  if(enableUploadToPrivateMQTT)
  {
    if (privateMQTT.canUpload())
    {
        char* mqtt_payload = privateMQTT.getPayloadBuffer();
        buildUplinkTelemetryMessageV6a(mqtt_payload, *makoTelemetry, *lemonTelemetry);

        const int qos = 1;
        MQTTConnectionResult result = privateMQTT.publish("telemetry/uplink", mqtt_payload, qos);

        switch(result) {
          case MQTTConnectionResult::SUCCESS:
            toggleStatusLED();
            uploadStatus = Q_SUCCESS_SEND;
            privateMQTTUploadCount++;
            USB_SERIAL_PRINTF("Private MQTT Client SEND MESSAGE SUCCESS.\n");
            break;
          case MQTTConnectionResult::SEND_ERROR:
            uploadStatus = Q_MQTT_CLIENT_SEND_ERROR;
            USB_SERIAL_PRINTF("Private MQTT Client failed to send message.\n");
            break;
          default:
            USB_SERIAL_PRINTF("Private MQTT Client failed - error unknown.\n");
            uploadStatus = Q_MQTT_CLIENT_SEND_ERROR;
            break;
        }
    }
    else
    {
      if (WiFi.status() != WL_CONNECTED) {
        uploadStatus = Q_NO_WIFI_CONNECTION;
        USB_SERIAL_PRINTLN("Private MQTT No Wifi\n");
      } else {
        uploadStatus = Q_MQTT_CLIENT_CONNECT_ERROR;
        USB_SERIAL_PRINTF("Private MQTT Client error - not connected\n");
      }
    }
  }
  else
  {
    uploadStatus = Q_SUCCESS_NOT_ENABLED;

    USB_SERIAL_PRINTLN("Private MQTT Not Enabled\n");
  }

  return uploadStatus;
}



#endif