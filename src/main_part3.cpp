#ifdef BUILD_INCLUDE_MAIN_PART3

#include "MercatorMQTT.h"
#include "SerialConfig.h"


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
        if (writeTelemetryLogToSerial)
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
        if (writeTelemetryLogToSerial)
        {
          if (uplinkMessageLengthBad)
            USB_SERIAL_PRINTF("decodeUplink bad msg length %hu && checksum bad %hu  Rx Time: %lu\n", uplinkMessageLength, uplink_checksum, uplinkRxMicroSeconds);
          else if (uplinkMessageLengthBad)
            USB_SERIAL_PRINTF("decodeUplink bad msg length only %hu  Rx Time: %lu\n", uplinkMessageLength, uplinkRxMicroSeconds);
          else if (uplink_checksum_bad)
            USB_SERIAL_PRINTF("decodeUplink bad msg checksum only %hu  Rx Time: %lu\n", uplink_checksum, uplinkRxMicroSeconds);
        }

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
  
    if (writeTelemetryLogToSerial)
      USB_SERIAL_PRINTF("Commit head block: maxpipeblocklength=%hu longestpipe=%hu pipelineLength=%hu TH=%hu,%hu\n",telemetryPipeline.getMaximumPipelineLength(),telemetryPipeline.getMaximumDepth(),telemetryPipeline.getPipelineLength(),telemetryPipeline.getTailBlockIndex(),telemetryPipeline.getHeadBlockIndex());
  }
  else
  {
    // payload too large to fit into block
    if (writeTelemetryLogToSerial)
      USB_SERIAL_PRINTF("Combined Mako (%hu) and Lemon (%lu) payloads too large (%hu) to fit into telemetry block (%hu)\n",uplinkMessageLength,sizeof(LemonTelemetryForStorage),totalMakoAndLemonLength,blockMaxPayload);
  }
}

void getNextTelemetryMessagesUploadedToPrivateMQTT()
{
  extern MercatorMQTT privateMQTT;
  BlockHeader tailBlock;
  const uint8_t maxTailPullsPerCycle = 50;   // allow up to 50 messages per cycle (per second)
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
      
      if (writeTelemetryLogToSerial)
        USB_SERIAL_PRINTF("tail block committed:  pipelineLength=%hu TH=%hu,%hu\n",telemetryPipeline.getPipelineLength(),telemetryPipeline.getTailBlockIndex(),telemetryPipeline.getHeadBlockIndex());
    }
    else
    {
      if (writeTelemetryLogToSerial)
        USB_SERIAL_PRINTF("tail block NOT committed\n");

      break;    // do not attempt any more tail pulls this event cycle
    }
  }
}

// TinyGPSPlus must be non-const as act of getting lat and lng resets the updated flag
void populateCurrentLemonTelemetry(LemonTelemetryForJson& l, TinyGPSPlus& g)
{
  extern bool gpsFixStatusForTelemetry;
  l.gps_lat =  g.location.lat(); l.gps_lng = g.location.lng();
  l.gps_hdop = g.hdop.hdop();    l.gps_course_deg = g.course.deg(); l.gps_knots = g.speed.knots();
  l.gps_hour = g.time.hour();    l.gps_minute =  g.time.minute();   l.gps_second =  g.time.second();
  l.gps_day =  g.date.day();     l.gps_month =  g.date.month();
  l.gps_year = g.date.year();
  l.gps_satellites =             g.satellites.value();
  l.isFix = gpsFixStatusForTelemetry;

  if (writeTelemetryLogToSerial)
    USB_SERIAL_PRINTF("\nPOPULATE TELEMETRY: using gpsFixStatusForTelemetry=%d, set l.isFix=%d\n",
                    gpsFixStatusForTelemetry, l.isFix);

  populateLanternPowerStats(l);
}

void populateFinalLemonTelemetry(LemonTelemetryForJson& l)
{
  l.downlink_send_duration = downlinkSendMessageDurationMicroSeconds;
  l.uplink_preamble_latency = preambleReceivedAfterMicroSeconds;
  l.uplink_rx_latency = uplinkRxMicroSeconds;
}

void constructLemonTelemetryForStorage(struct LemonTelemetryForStorage& s, const LemonTelemetryForJson l, const uint16_t uplinkMessageLength)
{
  s.gps_lat = l.gps_lat;          // must be on 4 byte boundary   
  s.gps_lng = l.gps_lng;          // must be on 4 byte boundary 
  s.goodUplinkMessageCount = goodUplinkMessageCount;      // GLOBAL
  s.badUplinkMessageCount = badUplinkMessageCount;        // GLOBAL
  s.consoleDownlinkMsgCount = consoleDownlinkMsgCount;    // GLOBAL
  s.telemetry_timestamp = lastGoodUplinkMessage;          // GLOBAL
  s.fixCount = fixCount;                                  // GLOBAL

  s.powerbank_voltage = powerBankVolts * 1000.0;          // GLOBAL
  s.powerbank_current = powerBank_mA;                     // GLOBAL            
  s.powerbank_mAH = powerBank_mAH;                        // GLOBAL

  s.uplinkMessageMissingCount = (uint16_t)(uplinkMessageMissingCount);  
  s.uplinkMessageLength = uplinkMessageLength;            // GLOBAL
  s.gps_hdop = (uint16_t)(l.gps_hdop * 10.0);
  s.gps_course_deg = (uint16_t)(l.gps_course_deg * 10.0);
  s.gps_knots = (uint16_t)(l.gps_knots * 10.0);            
  
  s.downlink_send_duration = l.downlink_send_duration; 
  s.uplink_preamble_latency = l.uplink_preamble_latency; 
  s.uplink_rx_latency = l.uplink_rx_latency;                
  s.uplinkBadMessagePercentage = uplinkBadMessagePercentage;    

  s.KBFromMako = KBFromMako;                             // GLOBAL
  s.gps_hour = l.gps_hour; s.gps_minute = l.gps_minute;  s.gps_second = l.gps_second;  //
  s.gps_day = l.gps_day; s.gps_month = l.gps_month; s.gps_satellites = (uint8_t)l.gps_satellites;
  s.gps_year =  l.gps_year;         //

  s.is_fix = l.isFix;
  s.one_byte_zero_padding = 0;
  s.two_byte_zero_padding = 0;      //
  s.four_byte_zero_padding = 0;      //

  if (writeTelemetryLogToSerial)
    USB_SERIAL_PRINTF("\nSTORAGE: l.isFix=%d -> s.is_fix=%d\n", l.isFix, s.is_fix);
}

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
  l.gps_lat = decode_float(msg);
  l.gps_lng = decode_float(msg);         
  l.goodUplinkMessageCount = decode_uint32(msg);
  l.badUplinkMessageCount = decode_uint32(msg);
  l.consoleDownlinkMsgCount = decode_uint32(msg);
  l.telemetry_timestamp = decode_uint32(msg);
  l.fixCount = decode_uint32(msg);

  l.powerbank_voltage = ((float)decode_uint16(msg)) / 1000.0;
  l.powerbank_current = ((float)decode_uint16(msg));
  l.powerbank_mAH = ((float)decode_uint16(msg));

  l.uplinkMessageMissingCount = decode_uint16(msg);
  l.uplinkMessageLength = decode_uint16(msg);
  l.gps_hdop = ((float)decode_uint16(msg)) / 10.0;
  l.gps_course_deg = ((float)decode_uint16(msg)) / 10.0;
  l.gps_knots = ((float)decode_uint16(msg)) / 10.0;

  l.downlink_send_duration = decode_uint32(msg);
  l.uplink_preamble_latency = decode_uint32(msg);
  
  l.uplink_rx_latency = decode_uint32(msg);
  l.uplinkBadMessagePercentage = decode_float(msg);

  l.KBFromMako = decode_float(msg);
  l.gps_hour = decode_uint8(msg);
  l.gps_minute = decode_uint8(msg);
  l.gps_second = decode_uint8(msg);
  l.gps_day = decode_uint8(msg);
  l.gps_month = decode_uint8(msg);
  l.gps_satellites = decode_uint8(msg);
  l.gps_year = decode_uint16(msg);
  
  l.isFix = decode_uint8(msg);

  
  if (writeTelemetryLogToSerial)
    USB_SERIAL_PRINTF("\nDECODE: decoded is_fix=%d -> l.isFix=%d\n", (int)*(msg-1), l.isFix);
  return true;
}

void checkMakoJSONForAlarms(struct MakoUplinkTelemetryForJson& m)
{
  if (m.user_action & LEAK_DETECTED_USER_ACTION)
  {
      makoReportsLeak = true;
  }
}

// uplink msg from mako is 86 bytes
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
  m.mako_usb_voltage = ((float)decode_uint16(uplinkMsg)) / 1000.0;
  m.mako_usb_current = ((float)decode_uint16(uplinkMsg)) / 100.0;

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

  m.diver_roll_orientation = decode_float(uplinkMsg); m.diver_pitch_orientation = decode_float(uplinkMsg);

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

// u-Blox GPS does not send a 'no fix' GGA or RMC message - they are simply not sent.
// This is an issue because it means that there is no message to send the diver.
// Fake GPS functions removed - GPS module now sends real NO FIX messages

void sendCeaseFixMessagesNMEAMessage(bool cease, const char* context)
{
  // These are u-blox commands.
  // Ceasing FIX messages means to stop the output even if the GPS has a valid fix.
  // The u-blox default behaviour is to cease output of GGA/RMC messages when there is no fix,
  // rather than send GGA message with quality set to 0 and RMC message status set to V.
  // Sending the cease messages stops ALL sending of GGA/RMC.
  // Sending the resume messages allows GGA/RMC to be sent IF there is a FIX, as in the default case.

  const char* cease_fix_gga_messages =  "$PUBX,40,GGA,0,0,0,0,0,0*46\n";
  const char* cease_fix_rmc_messages =  "$PUBX,40,RMC,0,0,0,0,0,0*47\n";

  const char* resume_fix_gga_messages = "$PUBX,40,GGA,0,1,0,0,0,0*5B\n";
  const char* resume_fix_rmc_messages =  "$PUBX,40,RMC,0,1,0,0,0,0*46\n";

  USB_SERIAL_PRINTF("**** SEND TO MAKO **** sendCeaseFixMessagesNMEAMessage: %s  %s\n",(cease ? "True - Stop Sending FIX" : "False - Restart sending FIX"),context);

  if (cease)
  {
    serial_mako_gopro.write(cease_fix_gga_messages);
    serial_mako_gopro.write(cease_fix_rmc_messages);
  }
  else
  {
    serial_mako_gopro.write(resume_fix_gga_messages);
    serial_mako_gopro.write(resume_fix_rmc_messages);
  }
}

void buildUplinkTelemetryMessageV6a(char* payload, 
                                    uint16_t payload_size,
                                    const struct MakoUplinkTelemetryForJson& m, 
                                    const struct LemonTelemetryForJson& l)
{
  currentPrivateMQTTUploadAt = millis();
  
  // Analysis: send lat/long as doubles not needed as floats preserve 7 decimal places which is sub-metre precision.
  snprintf(payload, payload_size,
          "{\"UTC_time\":\"%02d:%02d:%02d\",\"UTC_date\":\"%02d:%02d:%02d\",\"lemon_on_mins\":%lu,\"coordinates\":[%f,%f],\"is_fix\":%i,\"depth\":%.1f,"
          "\"water_pressure\":%.1f,\"water_temperature\":%.1f,\"enclosure_temperature\":%.1f,\"enclosure_humidity\":%.0f,\"enclosure_air_pressure\":%.1f,"
          "\"magnetic_heading_compensated\":%.0f,\"heading_to_target\":%.0f,\"distance_to_target\":%.1f,\"journey_course\":%.1f,\"journey_distance\":%.1f,"
          "\"mako_screen_display\":\"%s\",\"mako_on_mins\":%lu,\"mako_user_action\":%d,\"mako_rx_bad_checksum_msgs\":%hu,"
          "\"mako_usb_voltage\":%.1f,\"mako_usb_current\":%.0f,\"mako_target_code\":\"%s\","
          "\"fix_count\":%lu,\"powerbank_voltage\":%.1f,\"powerbank_current\":%f,\"powerbank_mAh\":%f,\"uplink_missing_msgs_from_mako\":%hu,"
          "\"sats\":%lu,\"hdop\":%f,\"gps_course\":%f,\"gps_speed_knots\":%f,"
          "\"min_sens_read\":%hu,\"quiet_b4_uplink\":%hu,\"sens_read\":%hu,\"max_sens_read\":%hu,\"act_sens_read\":%hu,\"max_act_sens_read\":%hu,"
          "\"mako_roll\":%.1f,\"mako_pitch\":%.1f,"
          "\"mako_rx_good_checksum_msgs\":%hu,"
          "\"downlink_send_duration\":%lu,\"uplink_preamble_latency\":%lu,\"uplink_rx_latency\":%lu,"
          "\"uplink_bad_percentage\":%.1f,"
          "\"mako_waymarker_e\":%d,\"mako_waymarker_label\":\"%s\",\"mako_direction_metric\":\"%s\","
          "\"uplink_good_msgs_from_mako\":%lu,\"uplink_bad_msgs_from_mako\":%lu,\"uplink_msg_length\":%hu,"
          "\"msgs_to_mqtt\":%d,\"mqtt_msg_length\":%hu,\"KB_to_mqtt\":%.1f,\"KB_uplinked_from_mako\":%.1f,"
          "\"console_downlink_msg\":%lu,\"geo_location\":\"Gozo, Malta\""
          "}",

          l.gps_hour, l.gps_minute, l.gps_second,l.gps_day, l.gps_month, l.gps_year,
          currentPrivateMQTTUploadAt / 1000 / 60,   // lemon on minutes
          l.gps_lat, l.gps_lng, l.isFix,
          m.depth, m.water_pressure, m.water_temperature,
          m.enclosure_temperature, m.enclosure_humidity, m.enclosure_air_pressure,
          m.magnetic_heading_compensated, m.heading_to_target, m.distance_to_target,
          m.journey_course, m.journey_distance,m.screen_display, m.seconds_on, m.user_action,
          m.bad_checksum_msgs, m.mako_usb_voltage, m.mako_usb_current, m.target_code,l.fixCount,          
          l.powerbank_voltage, l.powerbank_current, l.powerbank_mAH, l.uplinkMessageMissingCount,
          l.gps_satellites, l.gps_hdop, l.gps_course_deg, l.gps_knots,
          m.minimum_sensor_read_time, m.quietTimeMsBeforeUplink, m.sensor_aquisition_time,  
          m.max_sensor_acquisition_time, m.actual_sensor_acquisition_time, m.max_actual_sensor_acquisition_time,
          m.diver_roll_orientation, m.diver_pitch_orientation,
          m.good_checksum_msgs,l.downlink_send_duration,l.uplink_preamble_latency,    
          l.uplink_rx_latency,l.uplinkBadMessagePercentage,
          m.way_marker_enum, m.way_marker_label, m.direction_metric,
          l.goodUplinkMessageCount,l.badUplinkMessageCount,l.uplinkMessageLength,
          privateMQTTUploadCount, privateMQTTMessageLength,
          KBToPrivateMQTT, l.KBFromMako,l.consoleDownlinkMsgCount
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
        buildUplinkTelemetryMessageV6a(privateMQTT.getPayloadBuffer(), 
                                       privateMQTT.getPayloadSize(),
                                      *makoTelemetry, 
                                      *lemonTelemetry);

        const int qos = 1;
        MQTTConnectionResult result = privateMQTT.publish("telemetry/uplink", privateMQTT.getPayloadBuffer(), qos);

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
        uploadStatus = Q_SUCCESS_NO_SEND;
        USB_SERIAL_PRINTF("Private MQTT - pending send window\n");
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

#define BUILD_INCLUDE_MAIN_GPS

#include "main_gps.cpp"

#endif