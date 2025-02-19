#include <Arduino.h>

// rename the git file "mercator_secrets_template.c" to the filename below, filling in your wifi credentials etc.
#include "mercator_secrets.c"

#include <M5StickCPlus.h>

#include <WiFi.h>
#include <Update.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

#include <ArduinoJson.h>
#include <WebSerial.h>
#include "TinyGPSPlus.h"
#include <NavigationWaypoints.h>
#include <TelemetryPipeline.h>
#define PICOMQTT_MAX_MESSAGE_SIZE 4096
#include <PicoMQTT.h>
#include <WiFi.h>
#include <ESP32Ping.h>

#ifdef ENABLE_TELEGRAM_BOT_AT_COMPILE_TIME
  #include <WiFiClientSecure.h>
  #include <UniversalTelegramBot.h>
  WiFiClientSecure secured_client;
  UniversalTelegramBot telegramBot(TELEGRAM_BOT_TOKEN, secured_client);
#endif

// ****** Start Webserver ****** 
// Async Webserver and websocket for streaming statistics
AsyncElegantOtaClass AsyncElegantOTA;
AsyncWebSocket ws("/ws");

// Override ElegantOTA web page to have the Lemon banner graphic and Lemon-IO device label
#define MERCATOR_ELEGANTOTA_LEMON_BANNER
#define MERCATOR_OTA_DEVICE_LABEL "LEMON-IO"

// Duty cycle for sending statistics updates to stats web page
const int32_t timeBetweenSendingStatsUpdates = 990;
int32_t timeOfNextStatUpdate = 0;

// Store overriden lat/long and target (testing/diagnostics) set from lemon stats web page
String showOnMapRequest;
int showOnMapRequestIndex = -1;

String setTargetRequest;
int setTargetRequestIndex = -1;

// Json document for sending statistics to the lemon stats web page
JsonDocument readings;
// ****** End Webserver ****** 

// make sure this is disabled if writeLogToSerial is false
//#define USE_WEBSERIAL

#ifdef USE_WEBSERIAL
  #define USB_SERIAL WebSerial
#else
  #define USB_SERIAL Serial
#endif

// START FEATURE ENABLE FLAGS
bool writeLogToSerial = false;
bool writeTelemetryLogToSerial = false; // writeLogToSerial must also be true

bool enableReadUplinkComms = true;
bool enableGPSRead = true;
bool enableAllUplinkMessageIntegrityChecks = true;
bool enableConnectToPrivateMQTT = true;
bool enableUploadToPrivateMQTT = true;
const bool enableIMUSensor = false;
const bool enableOTAServer = true;          // over the air updates

//#define ENABLE_TELEGRAM_BOT_AT_COMPILE_TIME
#ifdef ENABLE_TELEGRAM_BOT_AT_COMPILE_TIME
  const bool enableTelegram = false;          // requires 35KB heap to send message (open/close secure connection)
#endif
// END FEATURE ENABLE FLAGS

// ################## START SERIAL/UART/GPIO CONFIGURATION
const int GPS_BAUD_RATE = 9600;

/*
// Max Baudrates for the IR LED (opto Schmitt Trigger)
// also works for both up and downlink: 57600, 71000
const int UPLINK_BAUD_RATE = 71000;  // 57600 115200 max baudrate for mako Tx due to mako phototransistor being 15 uS rise time, Lemon limited to 19200 as a result.
*/

// ******** Tx = GPIO2 Max Speed Tests ********
// GPIO2 Tx works for 57600, 71000, 91000, 576000
// at 1,700,000 getting about 10% bad msgs - 5% missing uplinks and 5% bad length uplinks (may help to have a small pause before sending response)
// at 2,100,000 getting about 14% bad msgs -  7% missing uplinks and 7% bad length uplinks
// ^^^^ add 3ms linger time before mako replying to lemon to get rid of all bad messages at 2,100,000
// ^^^^^ probably also works at 1,700,000
// Other rates to try which did work with tx only to Mako when not expecting a reply: 
//    922190, 1100000,1500000,1900000
// rates that did not work TO mako prior to changing reply to wired from IR LED:
//    921600, 1800000
const int UPLINK_BAUD_RATE = 57600;       // max working test so far: 2,100,000

const int NEOPIXELS_BAUD_RATE = 9600;

#define GOPRO_SERIAL Serial1

const uint8_t GROVE_GPS_RX_PIN = 33;   // only used for test without RS485 cable
const uint8_t GROVE_GPS_TX_PIN = 32;   // only used for test without RS485 cable

const uint8_t HAT_GPS_TX_PIN = 26;
const uint8_t HAT_GPS_RX_PIN = 36;

const uint8_t IR_LED_GPIO = 9;
const uint8_t TX_TO_NEOPIXELS_GPIO = IR_LED_GPIO;
const uint8_t RX_TO_NEOPIXELS_GPIO = 0;

const uint8_t RED_LED_GPIO = 10;
uint8_t redLEDStatus = HIGH;
// ################## END SERIAL/UART CONFIGURATION

// ################## START LANTERN NEO-PIXEL CONFIGURATION
enum e_display_brightness {OFF_DISPLAY = 0, DIM_DISPLAY = 25, HALF_BRIGHT_DISPLAY = 50, BRIGHTEST_DISPLAY = 100};
const e_display_brightness ScreenBrightness = BRIGHTEST_DISPLAY;

enum e_lemon_status{LC_NONE=0, LC_STARTUP=1, LC_SEARCH_WIFI=2, LC_FOUND_WIFI=3, LC_NO_WIFI=4, LC_NO_GPS=5, 
                    LC_NO_FIX=6, LC_GOOD_FIX=7, LC_ALL_OFF=8, LC_DIVE_IN_PROGRESS=64, LC_NO_STATUS_UPDATE=127, LC_NO_INTERNET=128};

e_lemon_status lemonStatus = LC_STARTUP;
// ################## END LANTERN NEO-PIXEL CONFIGURATION

// ################## START MQTT Configuration
PicoMQTT::Client localMQTT(       // credentials stored in mercator_secrets.c
    private_mqqt_local_host,      // broker address (or IP)
    private_mqqt_local_port,      // broker port (defaults to 1883)
    private_mqqt_client_id,       // Client ID
    private_mqqt_username,        // MQTT username
    private_mqqt_password         // MQTT password
);

PicoMQTT::Client remoteMQTT(      // credentials stored in mercator_secrets.c
    private_mqqt_remote_host,     // broker address (or IP)
    private_mqqt_remote_port,     // broker port (defaults to 1883)
    private_mqqt_client_id,       // Client ID
    private_mqqt_username,        // MQTT username
    private_mqqt_password         // MQTT password
);

uint32_t private_mqtt_upload_min_duty_ms = 50; // throttle upload to private mqtt ms
uint32_t last_private_mqtt_upload_at = 0;
const int16_t mqtt_payload_size = 2560;
char mqtt_payload[mqtt_payload_size];
uint32_t currentPrivateMQTTUploadAt = 0, lastPrivateMQTTUploadAt = 0;
uint32_t privateMQTTUploadDutyCycle = 0;
bool usingDevNetwork = false;
// ################## END MQTT Configuration


// #### START IN-MEMORY TELEMETRY-PIPELINE / MESSAGE BUFFER CONFIG
TelemetryPipeline telemetryPipeline;

const uint32_t telemetry_online_head_commit_duty_ms = 1900;
const uint32_t telemetry_offline_head_commit_duty_ms = 10000;
uint32_t last_head_committed_at = 0;
bool g_offlineStorageThrottleApplied = false;
// #### END IN-MEMORY TELEMETRY-PIPELINE / MESSAGE BUFFER CONFIG


// #### START WIFI CONFIG AND LABELS
WiFiClient wifiClient;
const String ssid_not_connected = "-";
String ssid_connected = ssid_not_connected;

char IPBuffer[16];
char IPLocalGateway[16];
char WiFiSSID[36];
const char* no_wifi_label="No WiFi";
const char* wait_ip_label="Wait IP";
const char* lost_ip_label="Lost IP";
// #### END WIFI CONFIG AND LABELS

const uint32_t maxTimeBeforeAlertNoFix = 3000;
const uint32_t maxTimeBeforeAlertNoGPSByte = 2000;
uint32_t timeNextGoodFixExpectedBy = 0;
uint32_t timeNextGPSByteExpectedBy = 0;

enum e_user_action{NO_USER_ACTION=0x0000, HIGHLIGHT_USER_ACTION=0x0001,RECORD_BREADCRUMB_TRAIL_USER_ACTION=0x0002,LEAK_DETECTED_USER_ACTION=0x0004};

// Mask with 0x01 to see if successful
enum e_q_upload_status {Q_SUCCESS=1, Q_SUCCESS_SEND=3, Q_SUCCESS_NO_SEND=5, Q_SUCCESS_NOT_ENABLED=7, 
                        Q_NO_WIFI_CONNECTION=8, Q_SERVER_CONNECT_ERROR=10,
                        Q_MQTT_CLIENT_CONNECT_ERROR=12, Q_MQTT_CLIENT_SEND_ERROR=14, 
                        Q_UNDEFINED_ERROR=254};

bool otaActive = false; // OTA updates toggle
AsyncWebServer asyncWebServer(80);

bool imuAvailable = true;

const char* leakAlarmMsg = "    Float\n\n    Leak!";

uint32_t fixCount = 0;
uint32_t passedChecksumCount = 0;
bool processUplinkMessage = true;

uint8_t journey_activity_count = 0;
const char* journey_activity_indicator = "\\|/-";

TinyGPSPlus gps;
int uart_number_gps = 2;
HardwareSerial gps_serial(uart_number_gps);

int uart_number_gopro = 1;
HardwareSerial ss_to_gopro(uart_number_gopro);

HardwareSerial& neopixels_serial = Serial;

bool diveInProgress = false;

void sendLemonStatus(const e_lemon_status status)
{
  if (!writeLogToSerial)
  {
    if (diveInProgress)
      neopixels_serial.write(status | LC_DIVE_IN_PROGRESS);
    else
      neopixels_serial.write(status);
  }
}

int nofix_byte_loop_count = 0;

template <typename T> struct vector
{
  T x, y, z;
};

uint16_t sideCount = 0, topCount = 0;
vector<float> magnetometer_vector, accelerometer_vector;

uint32_t consoleDownlinkMsgCount = 0;
uint32_t receivedUplinkMessageCount = 0;
uint32_t goodUplinkMessageCount = 0;
uint32_t badUplinkMessageCount = 0;
uint32_t badLengthUplinkMsgCount = 0;
uint32_t badChkSumUplinkMsgCount = 0;
uint16_t uplinkMessageMissingCount = 0;
float uplinkBadMessagePercentage = 0.0;

uint32_t lastGoodUplinkMessage = 0;
uint16_t uplinkMessageLength = 0;
uint32_t privateMQTTUploadCount = 0;
uint16_t privateMQTTMessageLength = 0;
float KBToPrivateMQTT = 0.0;
float KBFromMako = 0.0;

bool accumulateMissedMessageCount = false;    // start-up
const uint32_t delayBeforeCountingMissedMessages = 60000; // Allow 60 second start-up before counting lost/missed messages

const uint32_t uplinkMessageLingerPeriodMs = 30;   // max milliseconds to wait for Mako pre-amble to reply
uint32_t uplinkLingerTimeoutAt = 0;
uint32_t downlinkSendMessageDurationMicroSeconds = 0;   // Latency processing GPS message and downlink msg send to Mako complete.
uint32_t preambleReceivedAfterMicroSeconds = 0;         // Latency between start of preamble and end of preamble received from Mako.
uint32_t uplinkRxMicroSeconds = 0;                      // Latency between end of pre-amble received and good complete message received from Mako.
uint32_t uplinkMessageListenTimer = 0;                  // Latency processing GPS message, send to mako and valid msg received from Mako.

const int8_t maxPingAttempts = 1;
int32_t lastCheckForInternetConnectivityAt = 0;
int32_t checkInternetConnectivityDutyCycle = 10000; // 30 seconds between each check

const uint16_t pipelineBackedUpLength = 10;



const uint8_t M5_POWER_SWITCH_PIN = 255;

Button* p_primaryButton = nullptr;
Button* p_secondButton = nullptr;
void updateButtonsAndBuzzer();

const float minimumUSBVoltage = 4.0;
long USBVoltageDropTime = 0;
long milliSecondsToWaitForShutDown = 500;

extern const uint8_t STATS_HTML[];
extern const uint32_t STATS_HTML_SIZE;

extern const uint8_t MAP_HTML[];
extern const uint32_t MAP_HTML_SIZE;

void shutdownIfUSBPowerOff();
void toggleOTAActive();
void toggleWiFiActive();

void checkForLeak(const char* msg, const uint8_t pin);
void checkForReedSwitches();

bool setupOTAWebServer(const char* _ssid, const char* _password, const char* label, uint32_t timeout, bool wifiOnly);

void updateButtonsAndBuzzer()
{
  p_primaryButton->read();
  p_secondButton->read();
  M5.Beep.update();
}

struct MakoStats
{
  uint16_t minimum_sensor_read_time;
  uint16_t quietTimeMsBeforeUplink;
  uint16_t sensor_aquisition_time;
  uint16_t max_sensor_acquisition_time;
  uint16_t actual_sensor_acquisition_time;
  uint16_t max_actual_sensor_acquisition_time;

  MakoStats(uint16_t mi, uint16_t qu, uint16_t se,uint16_t max_s,uint16_t ac,uint16_t max_a) :
    minimum_sensor_read_time(mi),
    quietTimeMsBeforeUplink(qu),
    sensor_aquisition_time(se),
    max_sensor_acquisition_time(max_s),
    actual_sensor_acquisition_time(ac),
    max_actual_sensor_acquisition_time(max_a)
  {}

  MakoStats() :
    minimum_sensor_read_time(0),
    quietTimeMsBeforeUplink(0),
    sensor_aquisition_time(0),
    max_sensor_acquisition_time(0),
    actual_sensor_acquisition_time(0),
    max_actual_sensor_acquisition_time(0)
  {}
};

MakoStats latestMakoStats;

const uint16_t makoHardcodedUplinkMessageLength = 114;

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

struct LemonTelemetryForJson
{
  double    gps_lat;
  double    gps_lng;
  uint32_t  goodUplinkMessageCount;
  uint32_t  badUplinkMessageCount;
//  uint32_t  badLengthUplinkMsgCount;
//  uint32_t  badChkSumUplinkMsgCount;  
  uint32_t  consoleDownlinkMsgCount;
  uint32_t  telemetry_timestamp;
  uint32_t  fixCount;
  float     vBusVoltage;
  float     vBusCurrent;
  float     vBatVoltage;
  uint32_t  uplinkMessageMissingCount;
  uint16_t  uplinkMessageLength;
  float     uplinkBadMessagePercentage;

  double    gps_hdop;
  double    gps_course_deg;
  double    gps_knots;

  uint32_t  downlink_send_duration;
  uint32_t  uplink_preamble_latency;
  uint32_t  uplink_rx_latency;
  float     imu_lin_acc_x;
  float     imu_lin_acc_y;
  float     imu_lin_acc_z;
  float     imu_rot_acc_x;
  float     imu_rot_acc_y;
  float     imu_rot_acc_z;

  float     KBFromMako;
  uint8_t   gps_hour;
  uint8_t   gps_minute;
  uint8_t   gps_second;
  uint8_t   gps_day;
  uint8_t   gps_month;
  uint32_t  gps_satellites;
  uint16_t  gps_year;

  // not in LemonTelemetry message
  uint32_t  privateMQTTUploadCount;   // removed from LemonTelem message
  uint16_t  privateMQTTMessageLength;   // removed from LemonTelem message
  float     KBToPrivateMQTT;   // removed from LemonTelem message
  uint32_t  live_metrics_count;   // removed from LemonTelem message
  uint32_t  privateMQTTUploadDutyCycle;   // removed from LemonTelem message
};

struct LemonTelemetryForJson latestLemonTelemetry;

void getM5ImuSensorData(struct LemonTelemetryForJson& t);
void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info);
void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info);
void WiFiLostIP(WiFiEvent_t event, WiFiEventInfo_t info);
void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
void checkConnectivity();
bool isInternetAccessible();
bool isScubaMosquittoBrokerAvailable();
void dumpHeapUsage(const char* msg);
char* customiseSentence(char* sentence);
bool doesHeadCommitRequireForce(BlockHeader& block);
bool checkForValidPreambleOnUplink();
bool populateHeadWithMakoTelemetry(BlockHeader& headBlock, const bool validPreambleFound);
void populateHeadWithLemonTelemetryAndCommit(BlockHeader& headBlock);
void getNextTelemetryMessagesUploadedToPrivateMQTT();
void populateCurrentLemonTelemetry(LemonTelemetryForJson& l, TinyGPSPlus& g);
void populateFinalLemonTelemetry(LemonTelemetryForJson& l);
void constructLemonTelemetryForStorage(struct LemonTelemetryForStorage& s, const LemonTelemetryForJson l, const uint16_t uplinkMessageLength);
uint8_t decode_uint8(uint8_t*& msg) ;
uint16_t decode_uint16(uint8_t*& msg) ;
uint32_t decode_uint32(uint8_t*& msg) ;
float decode_float(uint8_t*& msg);
double decode_double(uint8_t*& msg) ;
void decode_uint16_into_3_char_array(uint8_t*& msg, char* target);
bool decodeIntoLemonTelemetryForUpload(uint8_t* msg, const uint16_t length, struct LemonTelemetryForJson& l);
bool decodeMakoUplinkMessageV5a(uint8_t* uplinkMsg, struct MakoUplinkTelemetryForJson& m, const bool preventGlobalUpdate);

bool makoReportsLeak = false;
void checkMakoJSONForAlarms(struct MakoUplinkTelemetryForJson& m);

uint16_t calcUplinkChecksum(char* buffer, uint16_t length);
void sendFakeGPSData_No_Fix();
void sendFakeGPSData_No_GPS();
void toggleOTAActive();
void toggleWiFiActive();
void shutdownIfUSBPowerOff();
void fadeToBlackAndShutdown();
void checkForLeak(const char* msg, const uint8_t pin);
const char* scanForKnownNetwork();
bool connectToWiFiAndInitOTA(const bool wifiOnly, int repeatScanAttempts);
bool setupOTAWebServer(const char* _ssid, const char* _password, const char* label, uint32_t timeout, bool wifiOnly);
void buildUplinkTelemetryMessageV6a(char* payload, const struct MakoUplinkTelemetryForJson& m, const struct LemonTelemetryForJson& l);
void buildBasicTelemetryMessage(char* payload);
enum e_q_upload_status uploadTelemetryToPrivateMQTT(MakoUplinkTelemetryForJson* makoTelemetry, struct LemonTelemetryForJson* lemonTelemetry);

void notifyWebSocketClients(String sensorReadings) {
  ws.textAll(sensorReadings);
}

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
  readings["last_private_mqtt_upload_at"] = (float)((int)((float)(last_private_mqtt_upload_at)/100.0))/10.0;
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

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) 
  {
    data[len] = 0;
//    if (strcmp((char*)data, "getReadings") == 0)
      notifyWebSocketClients(getStats());
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) 
  {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      notifyWebSocketClients(getStats()); // re-established
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  asyncWebServer.addHandler(&ws);
}

void getM5ImuSensorData(struct LemonTelemetryForJson& t)
{
  const float uninitialisedIMU = 0.0;
  
  // gyro x/y/z now not used
  if (enableIMUSensor)
  {
    M5.IMU.getAccelData(&t.imu_lin_acc_x, &t.imu_lin_acc_y, &t.imu_lin_acc_z);
    M5.IMU.getAhrsData(&t.imu_rot_acc_x, &t.imu_rot_acc_y, &t.imu_rot_acc_z);
  }
  else
  {
    t.imu_lin_acc_x = t.imu_lin_acc_y = t.imu_lin_acc_z = uninitialisedIMU;
    t.imu_rot_acc_x = t.imu_rot_acc_y = t.imu_rot_acc_z = uninitialisedIMU;
  }
}

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
  strcpy(IPBuffer,wait_ip_label);
  
  if (writeLogToSerial)
    USB_SERIAL.printf("***** Connected to %s successfully! *****\n",info.wifi_sta_connected.ssid);
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
  strcpy(IPBuffer,WiFi.localIP().toString().c_str());
  strcpy(IPLocalGateway, WiFi.gatewayIP().toString().c_str());
  strcpy(WiFiSSID, WiFi.SSID().c_str());

  usingDevNetwork = (!strcmp(IPLocalGateway,private_local_gateway) && !strcmp(WiFiSSID, private_dev_ssid));

  if (writeLogToSerial)
    USB_SERIAL.printf("***** WiFi CONNECTED IP: %s ******\n",IPBuffer);
}

bool devNetworkInUse()
{ return usingDevNetwork; }

void WiFiLostIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
  strcpy(IPBuffer,lost_ip_label);
  strcpy(IPBuffer,"");
  strcpy(IPLocalGateway, "");
  strcpy(WiFiSSID, WiFi.SSID().c_str());

  usingDevNetwork = false;

  if (writeLogToSerial)
    USB_SERIAL.printf("***** WiFi LOST IP ******\n");
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
  strcpy(IPBuffer,no_wifi_label);
  strcpy(IPLocalGateway, "");
  strcpy(WiFiSSID, "");

  usingDevNetwork = false;

  if (writeLogToSerial)
    USB_SERIAL.printf("***** WiFi DISCONNECTED: Reason: %d ******\n",info.wifi_sta_disconnected.reason);
  // Reason 2 
  // Reason 201
}

void checkConnectivity()
{
//  return;    // MBJ REFACTOR


  if (enableConnectToPrivateMQTT)
  {
    // Maximum of one connectivity check per duty cycle
    // If WiFi drops it will take two iterations to get back online, the first to reconnect to wifi
    // and the second to create a new Qubitro connection.
    if (millis() < lastCheckForInternetConnectivityAt + checkInternetConnectivityDutyCycle)
      return;

    // primary detection of no connectivity is messages backed up and not draining
    if (telemetryPipeline.getPipelineLength() > pipelineBackedUpLength && 
        telemetryPipeline.isPipelineDraining() == false)
    {
      // messages are backing up and not draining, either a WiFi or 4G or broker server connection issue
      lastCheckForInternetConnectivityAt = millis();

      if (writeLogToSerial)
        USB_SERIAL.println("0. checkConnectivity: Pipeline not draining");

      if (WiFi.status() == WL_CONNECTED)
      {
        if (writeLogToSerial)
    		{
          USB_SERIAL.println("1.1 checkConnectivity: WIFI is connected, ping 8.8.8.8");
    		}
        
        // either a 4G or broker server connection issue
        if (isInternetAccessible())   // ping google DNS
        {
          if (writeLogToSerial)
          {
            USB_SERIAL.println("1.2.1 checkConnectivity: WiFi ok, internet ping success");
          }
        }
        else
        {
          if (writeLogToSerial)
          {
            USB_SERIAL.println("1.2.2 checkConnectivity: WiFi ok, ping fail, out of coverage");
          }
          
          g_offlineStorageThrottleApplied = true;
        }

        if (isScubaMosquittoBrokerAvailable())
        {
          if (writeLogToSerial)
          {
            USB_SERIAL.println("1.2.3 checkConnectivity: Scuba MQTT Broker ping success");
          }
        }
        else
        {
          if (writeLogToSerial)
          {
            USB_SERIAL.println("1.2.4 checkConnectivity: WiFi ok, ping google ok, MQTT Broker fail");
          }
          
          g_offlineStorageThrottleApplied = true;
        }

      }
      else
      {
        g_offlineStorageThrottleApplied = true;
        
        if (writeLogToSerial)
          USB_SERIAL.println("checkConnectivity: WIFI not connected, attempt reconnect");

        // Do a manual wifi reconnect attempt - synchronous
        if (WiFi.reconnect())
        {
          if (writeLogToSerial)
            USB_SERIAL.println("checkConnectivity: WIFI reconnect success");          
        }
        else
        {
          if (writeLogToSerial)
            USB_SERIAL.println("checkConnectivity: WIFI reconnect fail");          
        }
      }
    }
  }
}

bool isInternetAccessible()
{
  lastCheckForInternetConnectivityAt = millis();
  return Ping.ping(ping_target,maxPingAttempts);
}

bool isScubaMosquittoBrokerAvailable()
{
    PicoMQTT::Client* MQTTClient = (usingDevNetwork ? &localMQTT : &remoteMQTT);

    return MQTTClient->connected();
}

void dumpHeapUsage(const char* msg)
{  
  if (writeLogToSerial)
  {
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // internal RAM, memory capable to store data or to create new task
    USB_SERIAL.printf("\n%s : free heap bytes: %i  largest free heap block: %i min free ever: %i\n",  msg, info.total_free_bytes, info.largest_free_block, info.minimum_free_bytes);
  }
}

void toggleRedLED()
{
  redLEDStatus = (redLEDStatus == HIGH ? LOW : HIGH );
  digitalWrite(RED_LED_GPIO, redLEDStatus);
}

bool haltAllProcessingDuringOTAUpload = false;

void disableFeaturesForOTA()
{
  enableConnectToPrivateMQTT = false;
  enableUploadToPrivateMQTT = false;
  enableReadUplinkComms = false;
  processUplinkMessage = false;
  enableAllUplinkMessageIntegrityChecks = false;
  enableGPSRead = false;
  writeLogToSerial = false;
  writeTelemetryLogToSerial = false;

  gps_serial.end();
  GOPRO_SERIAL.end();
  neopixels_serial.end();

  localMQTT.disconnect();
  remoteMQTT.disconnect();
  
  redLEDStatus = LOW;
  digitalWrite(RED_LED_GPIO, redLEDStatus);  // turn on red led

  haltAllProcessingDuringOTAUpload = true;

  if (writeLogToSerial)
  {
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // internal RAM, memory capable to store data or to create new task
    USB_SERIAL.printf("\n%s : free heap bytes: %i  largest free heap block: %i min free ever: %i\n",  "halted", info.total_free_bytes, info.largest_free_block, info.minimum_free_bytes);
  }

  telemetryPipeline.teardown();

      #ifdef USE_WEBSERIAL
        ws.closeAll();          // close all websocket connections for test page
        WebSerial.closeAll();   // close all websocket connetions for WebSerial
      #endif
}

TaskHandle_t mainTaskHandle = nullptr;
BaseType_t mainTaskCoreId = 0;

void uploadOTABeginCallback(AsyncElegantOtaClass* originator)
{
  disableFeaturesForOTA();
}

void setup()
{
    localMQTT.connected_callback = [] {
      if (writeLogToSerial)
        USB_SERIAL.println("Local MQTT connected");
    };

    localMQTT.disconnected_callback = [] {
      if (writeLogToSerial)
        USB_SERIAL.println("Local MQTT disconnected");
    };

    remoteMQTT.connected_callback = [] {
      if (writeLogToSerial)
        USB_SERIAL.println("Remote MQTT connected");
    };

    remoteMQTT.disconnected_callback = [] {
      if (writeLogToSerial)
        USB_SERIAL.println("Remote MQTT disconnected");
    };

  mainTaskCoreId = xPortGetCoreID();
  mainTaskHandle = xTaskGetCurrentTaskHandle();

  M5.begin();

  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiLostIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_LOST_IP);
  WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  
  strcpy(IPBuffer,no_wifi_label);

  ssid_connected = ssid_not_connected;

  if (writeLogToSerial)
    USB_SERIAL.printf("sizeof LemonTelemetry: %lu\n",sizeof(LemonTelemetryForStorage));

  const uint16_t maxPipelineBufferKB = 95;    // if telegram is enabled the pipeline needs to be 60KB or smaller. Without pipeline length can be 95KB.
  const uint16_t maxPipelineBlockPayloadSize = 256; // was 224 - Assuming 120 byte Mako Telemetry Msg and 104 byte Lemon Telemetry Msg
  BlockHeader::s_overrideMaxPayloadSize(maxPipelineBlockPayloadSize);  // 400 messages with 256 byte max payload. 
  telemetryPipeline.init(&millis,maxPipelineBufferKB);

  if (enableIMUSensor)
  {
    M5.Imu.Init();
  }
  else
  {
    if (writeLogToSerial)
    {
      USB_SERIAL.println("IMU Sensor Off");
    }
    M5.Lcd.println("IMU Sensor Off");
    imuAvailable = false;
  }

  pinMode(RED_LED_GPIO, OUTPUT); // Red LED - the interior LED to M5 Stick
  digitalWrite(RED_LED_GPIO, redLEDStatus); // switch off as redLEDStatus is HIGH

  pinMode(TX_TO_NEOPIXELS_GPIO, OUTPUT);
  digitalWrite(TX_TO_NEOPIXELS_GPIO, HIGH); // switch off

  pinMode(RX_TO_NEOPIXELS_GPIO, INPUT);

  if (!writeLogToSerial)
  {
    Serial.end();
    const bool invert = true; // Invertion of Tx needed due to using the IR Led as transmit
    neopixels_serial.begin(NEOPIXELS_BAUD_RATE, SERIAL_8N1, RX_TO_NEOPIXELS_GPIO, TX_TO_NEOPIXELS_GPIO, invert);
    neopixels_serial.setRxInvert(!invert);    // Need to not invert the Rx.
  }

  sendLemonStatus(LC_STARTUP);

  M5.Lcd.setRotation(0);
  M5.Lcd.setTextSize(2);
  M5.Axp.ScreenBreath(ScreenBrightness);

  p_primaryButton = &M5.BtnA;
  p_secondButton = &M5.BtnB;

  if (enableOTAServer)
  {
    sendLemonStatus(LC_SEARCH_WIFI);

    bool wifiOnly = false;
    int repeatScanAttempts = 4;
    bool connected = connectToWiFiAndInitOTA(wifiOnly, repeatScanAttempts);
    sendLemonStatus(connected ? LC_FOUND_WIFI : LC_NO_WIFI);

    if (!connected)
      delay(5000);    // wait 5 seconds before proceeding - lantern will show no wifi state for 5 seconds
    else
      delay(2000);    // show state for 2 seconds
  }

  // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/uart.html
  //  uart_set_mode(uart_number, UART_MODE_RS485_HALF_DUPLEX);

  M5.Lcd.setRotation(1);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setCursor(0, 0);

  gps_serial.begin(GPS_BAUD_RATE, SERIAL_8N1, GROVE_GPS_RX_PIN, GROVE_GPS_TX_PIN);   // pin 33=rx (white M5), pin 32=tx (yellow M5), specifies the grove SCL/SDA pins for Rx/Tx

  // setup second serial port for sending/receiving data to/from GoPro
  GOPRO_SERIAL.setRxBufferSize(1024); // was 256 - must set before begin
  GOPRO_SERIAL.begin(UPLINK_BAUD_RATE, SERIAL_8N2, HAT_GPS_RX_PIN, HAT_GPS_TX_PIN);

  // cannot use Pin 0 for receive of GPS (resets on startup), can use Pin 36, can use 26
  // cannot use Pin 0 for transmit of GPS (resets on startup), only Pin 26 can be used for transmit.

  if (enableConnectToPrivateMQTT && WiFi.status() == WL_CONNECTED)
  {
  // do any connect test to MQTT broker here
  }

  if (enableUploadToPrivateMQTT)
  {
    localMQTT.begin();
    remoteMQTT.begin();
  }
}

char* customiseSentence(char* sentence)
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

  if (writeLogToSerial)
  {
    USB_SERIAL.printf("0. startSentence index = %i\n",startSentenceIndex);
  }

  const bool isGNGGA = ((strncmp(startSentence,"$GPGGA",6) == 0 ||
                         strncmp(startSentence,"$GNGGA",6) == 0));

  const bool isGNRMC = ((strncmp(startSentence,"$GPRMC",6) == 0 ||
                         strncmp(startSentence,"$GNRMC",6) == 0));

  bool overrideLocation = false;

  if (writeLogToSerial)
  {
    USB_SERIAL.println("0. checking for override location");
    USB_SERIAL.printf("0.0 showOnMapRequestIndex=%i isGNGAA=%i isGNRMC=%i strlen(startSentence)=%zu\n",showOnMapRequestIndex, (isGNGGA ? 1 : 0),(isGNRMC ? 1 : 0), strnlen(startSentence,minimumSentenceLength));
    USB_SERIAL.printf("0.1 %s\n",startSentence);
  }

  if (  showOnMapRequestIndex >= 0 && 
        (isGNGGA || isGNRMC) && 
        strnlen(startSentence,minimumSentenceLength) >= minimumSentenceLength)
  {
    if (writeLogToSerial)
      USB_SERIAL.println("1. Entered override location");
    
    overrideLocation = true;
    // spoof GPS to be reporting lat/long at selected feature
    double longOverride = WraysburyWaypoints::waypoints[showOnMapRequestIndex]._long;
    double latOverride = WraysburyWaypoints::waypoints[showOnMapRequestIndex]._lat;

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

    if (writeLogToSerial)
    {
        USB_SERIAL.printf("1.1 %.60s    <-- new location\n", newLocation);
    }

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
      if (writeLogToSerial)
      {
        USB_SERIAL.println("2. Override Location");
        USB_SERIAL.printf("3.0 %s    <-- Old Sentence\n", startSentence);
      }

      memcpy(sentence+copyOffset,newLocation,lengthNMEALocation);
 
      if (writeLogToSerial)
        USB_SERIAL.printf("4.0 %s    <-- New Location Sentence\n", startSentence);
    }
    else if (writeLogToSerial)
      USB_SERIAL.println("2. Not Overriding Location");
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

  if (setTargetRequestIndex >= 0 && isGNGGA)
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
            *next = setTargetRequestIndex+33; // make sure visible char

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

    if (writeLogToSerial)
      USB_SERIAL.printf("5.0 %s    <-- All updates new  Location Sentence\n", startSentence);
  }

  return sentence;
}



uint32_t mainBackColour = TFT_BLACK;

uint32_t timeOfNextLemonStatus = 0;
const uint32_t lemonStatusDutyCycle = 1000;

uint32_t timeOfNextTelegramBotUpdateSendMsg = 0;
const uint32_t telegramBotDutyCycle = 10000;

const int initNeopixelSerialByteRead = -1;
int neopixelSerialByteRead = initNeopixelSerialByteRead;

void loop()
{
  shutdownIfUSBPowerOff();

  if (haltAllProcessingDuringOTAUpload)
  {  
    delay(100);
    toggleRedLED();
    return;
  }

  if (makoReportsLeak)
  {
    if (mainBackColour == TFT_BLACK)
    {
      mainBackColour = TFT_ORANGE;
      M5.Lcd.fillScreen(TFT_ORANGE);
    }
  }

  M5.Lcd.setTextColor(TFT_WHITE,mainBackColour);

  updateButtonsAndBuzzer();

  if (enableUploadToPrivateMQTT)
  {
      usingDevNetwork ? localMQTT.loop() : remoteMQTT.loop();
  }

  if (!accumulateMissedMessageCount && millis() > delayBeforeCountingMissedMessages)
    accumulateMissedMessageCount = true;

  checkConnectivity();
  
  if (p_primaryButton->wasReleasefor(100)) // disable message upload
  {
    updateButtonsAndBuzzer();

    disableFeaturesForOTA();
    return;
  }

  while (enableGPSRead && gps_serial.available() > 0)
  {
    checkForLeak(leakAlarmMsg, M5_POWER_SWITCH_PIN);

    if (ws.count() && millis() > timeOfNextStatUpdate)
    {
      notifyWebSocketClients(getStats());
      ws.cleanupClients();  // ensure no more than 8 connections

      timeOfNextStatUpdate = millis() + timeBetweenSendingStatsUpdates;

      dumpHeapUsage("Sent stats: ");
    }

    char nextByte = gps_serial.read();

    if (gps.encode(nextByte))
    {
      uint32_t now = millis();
      timeNextGPSByteExpectedBy = now + maxTimeBeforeAlertNoGPSByte;

      // Must extract longitude and latitude for the updated flag to be set on next location update.
      if (gps.location.isValid() && gps.location.isUpdated() && gps.isSentenceFix())
      {
        if (now > timeOfNextLemonStatus)
        {
          sendLemonStatus(LC_GOOD_FIX);
          timeOfNextLemonStatus = now + lemonStatusDutyCycle;
        }

        timeNextGoodFixExpectedBy = now + maxTimeBeforeAlertNoFix;

        // only enter here on GPRMC and GPGGA msgs with M5 GPS unit, 0.5 sec between each message.
        // GNRMC followed by GNGGA messages for NEO-6M, no perceptible gap between GNRMC and GNGGA.
        // 1 second between updates on the same message type for M5.
        // Only require uplink message for GGA.

        //////////////////////////////////////////////////////////
        // send message to outgoing serial connection to gopro
        GOPRO_SERIAL.write(customiseSentence(gps.getSentence()));
        consoleDownlinkMsgCount++;
// breaks good uplinks - keep commented out
//          int txDoneWaitMS = 100;
//         uart_wait_tx_done(uart_number_gopro,pdMS_TO_TICKS(txDoneWaitMS));

        if (gps.isSentenceGGA())
        {
          processUplinkMessage = true;  // triggers listen for uplink msg
          uplinkMessageListenTimer = millis();
          downlinkSendMessageDurationMicroSeconds = micros();
        }

        uint32_t newFixCount = gps.sentencesWithFix();
        uint32_t newPassedChecksum = gps.passedChecksum();
        if (newFixCount > fixCount)
        {
          fixCount = newFixCount;

          if (writeLogToSerial)
          {
            USB_SERIAL.printf("\nFix: %lu Good GPS Msg: %lu Bad GPS Msg: %lu\n", fixCount, newPassedChecksum, gps.failedChecksum());
          }
        }

        if (nofix_byte_loop_count > -1)
        {
          // clear the onscreen counter that increments whilst attempting to get first valid location
          nofix_byte_loop_count = -1;
          M5.Lcd.fillScreen(TFT_BLACK);
        }

        updateButtonsAndBuzzer();

        if (newPassedChecksum <= passedChecksumCount)
        {
          // incomplete message received, continue reading bytes, don't update display.
          return;
        }
        else
        {
          passedChecksumCount = newPassedChecksum;
        }

        populateCurrentLemonTelemetry(latestLemonTelemetry, gps);

      }
      else
      {
        // get location invalid if there is no new fix to read before 1 second is up.
        if (nofix_byte_loop_count > -1)
        {
          // Bytes are being received but no valid location fix has been seen since startup
          // Increment byte count shown until first fix received.
          M5.Lcd.setCursor(50, 100);
          M5.Lcd.printf("%d", nofix_byte_loop_count++);
        }
      }
    }
    else
    {
      // no byte received.
    }
  }

  if (nofix_byte_loop_count > 0)
  {
    // No fix only shown on first acquisition.
    M5.Lcd.setCursor(55, 5);
    M5.Lcd.setTextSize(4);
    M5.Lcd.print("No Fix\n\n   Lemon\n");
    M5.Lcd.setCursor(110, 45);
    M5.Lcd.printf("%c", journey_activity_indicator[(++journey_activity_count) % 4]);
    sendLemonStatus(LC_NO_FIX);

    // tells gopro M5 that gps is alive but no fix yet.
    // gopro M5 can choose to show this data for test purposes, otherwise in
    // swimming pool like new malden or putney there may be no gps signal so
    // won't be able to test the rest, eg compass, temperature, humidity, buttons, reed switches
    // note the leak sensor is active at all times in the gopro M5.
    sendFakeGPSData_No_Fix();

    delay(250); // no fix wait
  }
  else if (nofix_byte_loop_count != -1)
  {
    // No GPS is reported when no bytes have ever been received on the UART.
    // Once messages start being received, this is blocked as it is normal
    // to have gaps in the stream. There is no indication if GPS stream hangs
    // after first byte received, eg no bytes within 10 seconds.

    M5.Lcd.setCursor(55, 5);
    M5.Lcd.setTextSize(4);
    M5.Lcd.print("No GPS\n\n   Lemon\n");
    M5.Lcd.setCursor(110, 45);
    M5.Lcd.printf("%c", journey_activity_indicator[(++journey_activity_count) % 4]);
    sendLemonStatus(LC_NO_GPS);

    // tells gopro M5 that gps is alive but no fix yet.
    // gopro M5 can choose to show this data for test purposes, otherwise in
    // swimming pool like new malden or putney there may be no gps signal so
    // won't be able to test the rest, eg compass, temperature, humidity, buttons, reed switches
    // note the leak sensor is active at all times in the gopro M5.
    sendFakeGPSData_No_GPS();

    delay(250); // no fix wait
  }
  else
  {
    if (processUplinkMessage)
    {
      // 1. Skip past any trash characters due to half-duplex and read pre-amble
      // If uplink messages to be ignored this returns false, which will zero out the Mako telemetry in upload message.
      bool validPreambleFound = checkForValidPreambleOnUplink();
      if (validPreambleFound)      
        uplinkMessageListenTimer = millis() - uplinkMessageListenTimer;
      else
        uplinkMessageListenTimer = 0;

      // 2. Get the next free head block to populate in the telemetry pipeline
      uint16_t blockMaxPayload=0;
      BlockHeader headBlock = telemetryPipeline.getHeadBlockForPopulating();

      // 3. Populate the head block with the binary telemetry data received from Mako (or zero's if no data)
      bool messageValidatedOk = populateHeadWithMakoTelemetry(headBlock, validPreambleFound);

      float tempDenominator = float(goodUplinkMessageCount+badUplinkMessageCount+uplinkMessageMissingCount);

      if (tempDenominator > 0)
        uplinkBadMessagePercentage = 100.0*float(badUplinkMessageCount+uplinkMessageMissingCount)/tempDenominator;
      
      M5.Lcd.setCursor(5, 5);
      M5.Lcd.setTextColor(TFT_WHITE, mainBackColour);
          
      M5.Lcd.setTextSize(3);

      const bool showBadChecksumInsteadofAllBad=true;

      if (showBadChecksumInsteadofAllBad)
        M5.Lcd.printf("Fix %lu\nR^ %lu !%lu\n",fixCount, goodUplinkMessageCount, badChkSumUplinkMsgCount);
      else
        M5.Lcd.printf("Fix %lu\nR^ %lu !%lu\n",fixCount, goodUplinkMessageCount, badUplinkMessageCount);
 
      if (g_offlineStorageThrottleApplied && telemetryPipeline.isPipelineDraining() == false)
        M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
      else if (g_offlineStorageThrottleApplied && telemetryPipeline.isPipelineDraining())
        M5.Lcd.setTextColor(TFT_BLACK, TFT_ORANGE);
      else if (telemetryPipeline.getPipelineLength() > 4)
        M5.Lcd.setTextColor(TFT_BLACK, TFT_YELLOW);
      else
        M5.Lcd.setTextColor(TFT_WHITE, mainBackColour);

      const bool showPipeLength=false;

      if (showPipeLength)
        M5.Lcd.printf("P %-3hu Mis %hu\n",telemetryPipeline.getPipelineLength(),uplinkMessageMissingCount);
      else
        M5.Lcd.printf("L%-3hu Mis %hu\n",badLengthUplinkMsgCount,uplinkMessageMissingCount);

      if (WiFi.status() != WL_CONNECTED)
        M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
      else
        M5.Lcd.setTextColor(TFT_WHITE, mainBackColour);

      const bool showListenTimer = false;

      if (showListenTimer)       
        M5.Lcd.printf("Q %lu UT %lu  \n",privateMQTTUploadCount,uplinkMessageListenTimer);
      else
        M5.Lcd.printf("Q %lu !%.1f%%\n",privateMQTTUploadCount,uplinkBadMessagePercentage);
      
      M5.Lcd.setTextSize(2);

      if (WiFi.status() != WL_CONNECTED) 
        M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
      else
        M5.Lcd.setTextColor(TFT_WHITE, mainBackColour);

      int16_t xCurs = M5.Lcd.getCursorX();
      int16_t yCurs = M5.Lcd.getCursorY();

//      M5.Lcd.printf("     %-15s", IPBuffer); //(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "No WiFi         "));
//      M5.Lcd.printf("     %i %-15s", showOnMapRequestIndex, showOnMapRequest.c_str()); //(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "No WiFi         "));
      M5.Lcd.printf("     %i %-15s", setTargetRequestIndex, setTargetRequest.c_str()); //(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "No WiFi         "));

      M5.Lcd.setCursor(xCurs,yCurs);
      M5.Lcd.setTextColor(TFT_MAGENTA, TFT_BLACK);
      M5.Lcd.printf("%.1fC",M5.Axp.GetTempInAXP192());

      M5.Lcd.setTextColor(TFT_WHITE, mainBackColour);
      uplinkMessageListenTimer = 0;

      if (ws.count() && millis() > timeOfNextStatUpdate)
      {
        notifyWebSocketClients(getStats());
        ws.cleanupClients();  // ensure no more than 8 connections

        timeOfNextStatUpdate = millis() + timeBetweenSendingStatsUpdates;
      }
      
      if (!messageValidatedOk)    // validation fails if mako telemetry not invalid size
      {
        processUplinkMessage = false;
        return;
      }

      // 4.1 Throttle committing to head - check mako message to see if useraction != 0, otherwise only every 10 seconds
      bool forceHeadCommit = doesHeadCommitRequireForce(headBlock);

      uint32_t timeNow = millis();

      // Head will be committed if forced by result of user action, is more than 2 seconds passed when online, or more than 10 seconds passed when offline 
      if (forceHeadCommit || 
          (g_offlineStorageThrottleApplied == false && timeNow >= last_head_committed_at + telemetry_online_head_commit_duty_ms) ||
          (g_offlineStorageThrottleApplied == true && timeNow >= last_head_committed_at + telemetry_offline_head_commit_duty_ms))
      {
        last_head_committed_at = timeNow;
  
        populateFinalLemonTelemetry(latestLemonTelemetry);

        // 4.2 Populate the head block with the binary Lemon telemetry data and commit to the telemetry pipeline.
        populateHeadWithLemonTelemetryAndCommit(headBlock);
      }
      else
      {
        // do not commit the head block - throw away the entire message
      }

      // 5. Send the next message(s) from pipeline to private MQTT
      getNextTelemetryMessagesUploadedToPrivateMQTT();

      processUplinkMessage = false; // finished processing the uplink message  
    }
    else
    {
      uplinkMessageListenTimer = 0;
    }
  }

  uint32_t now = millis();

  if (now > timeOfNextLemonStatus)
  {
    if (now > timeNextGoodFixExpectedBy)
    {
      if (now > timeNextGPSByteExpectedBy)
        sendLemonStatus(LC_NO_GPS);
      else
        sendLemonStatus(LC_NO_FIX);
    }
    else
    {
      if (telemetryPipeline.isPipelineDraining())
        sendLemonStatus(LC_GOOD_FIX);
      else
        sendLemonStatus(LC_NO_INTERNET);
    }

    timeOfNextLemonStatus = millis() + lemonStatusDutyCycle;
  }

  checkForLeak(leakAlarmMsg, M5_POWER_SWITCH_PIN);

  checkForReedSwitches();

#ifdef ENABLE_TELEGRAM_BOT_AT_COMPILE_TIME
  if (enableTelegram && now > timeOfNextTelegramBotUpdateSendMsg)
  {
    bool result = telegramBot.sendSimpleMessage(TELEGRAM_USER_ID, "Heartbeat", "");
    if (writeLogToSerial)
      USB_SERIAL.printf("Result of Bot Send - loop(): %d\n",result);
    timeOfNextTelegramBotUpdateSendMsg = millis() + telegramBotDutyCycle;
    dumpHeapUsage("loop() - after send telegram");
  }
#endif
}

void checkForReedSwitches()
{
  while (!writeLogToSerial && neopixels_serial.available())
  {
    neopixelSerialByteRead = neopixels_serial.read();
    if (neopixelSerialByteRead == 100)
    {
      mainBackColour = TFT_BLUE;
      M5.Lcd.fillScreen(TFT_BLUE);
    }
    else if (neopixelSerialByteRead == 200)
    {
      mainBackColour = TFT_MAGENTA;
      M5.Lcd.fillScreen(TFT_MAGENTA);
    }
  }
}

void sendStatsWebSocketNotification()
{
    notifyWebSocketClients(getStats());
}

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


bool checkForValidPreambleOnUplink()
{
  bool validPreambleFound = false;

  // If uplink messages are to be decoded look for pre-amble sequence on Serial Rx
  if (enableReadUplinkComms)
  {
    // 1.1 wait until all transmitted data sent to Mako (synchronous)
//   int txDoneWaitMS = 100;
//   uart_wait_tx_done(uart_number_gopro,pdMS_TO_TICKS(txDoneWaitMS));

    uint32_t nowUS = micros();

    downlinkSendMessageDurationMicroSeconds = (nowUS >= downlinkSendMessageDurationMicroSeconds ? nowUS - downlinkSendMessageDurationMicroSeconds : 0xFFFFFFFF - downlinkSendMessageDurationMicroSeconds + nowUS);

    uplinkLingerTimeoutAt = millis()+uplinkMessageLingerPeriodMs;

    preambleReceivedAfterMicroSeconds = micros();

    // 1.2 Read received data searching for lead-in pattern from Tracker - MBJ\0AEJ\0
    // wait upto uplinkLingerTimeoutAt milliseconds to receive the pre-amble

    char uplink_preamble_first_segment[] = "MBJ";
    char uplink_preamble_second_segment[] = "AEJ";

    const char* nextByteToFind = uplink_preamble_first_segment;
    const char* nextSecondSegmentByteToFind = uplink_preamble_second_segment;

    const int preambleMBJSize = 256;
    char  preambleMBJ[preambleMBJSize] = "Preamble MBJ: ";
    char* nextIndex = preambleMBJ + strlen(preambleMBJ);

    while ((GOPRO_SERIAL.available() || 
            !GOPRO_SERIAL.available() && millis() < uplinkLingerTimeoutAt) && 
            *nextByteToFind != 0)
    {
      // throw away trash bytes from half-duplex clash - always present
      char next = GOPRO_SERIAL.read();
      if (next == *nextByteToFind)
      {
        if (writeLogToSerial && writeTelemetryLogToSerial)
          *nextIndex++ = (isalnum(next) ? next : '?');

        nextByteToFind++;
      }
      else
      {
        if (writeLogToSerial && writeTelemetryLogToSerial)
          *nextIndex++ = (isalnum(next) ? next : (next == 0 ? '0' : '?'));

        nextByteToFind = uplink_preamble_first_segment;    // make sure contiguous preamble found, reset search for first char of preamble
      }

      if (writeLogToSerial && writeTelemetryLogToSerial)
        if (nextIndex == preambleMBJ + preambleMBJSize-10)
          nextIndex = preambleMBJ;
    }

    if (writeLogToSerial && writeTelemetryLogToSerial)
    {
      *nextIndex++ = '\n';  *nextIndex++ = '\0';

      if (writeLogToSerial && writeTelemetryLogToSerial)
        USB_SERIAL.printf(preambleMBJ);

      if (*nextByteToFind != 0)
      {
        if (writeLogToSerial && writeTelemetryLogToSerial)
          USB_SERIAL.printf("    MBJ Timeout\n");
      }
    }

    int preambleAEJSize = 256;
    char  preambleAEJ[preambleAEJSize] = "Preamble AEJ: ";
    nextIndex = preambleAEJ + strlen(preambleAEJ);

    if (*nextByteToFind == 0)
    {
      // found an MBJ now find an AEJ, ignoring any other MBJs
     while ((GOPRO_SERIAL.available() || 
            !GOPRO_SERIAL.available() && millis() < uplinkLingerTimeoutAt) && 
            *nextSecondSegmentByteToFind != 0)
     {
        char next = GOPRO_SERIAL.read();
        if (next == *nextSecondSegmentByteToFind)
        {
          if (writeLogToSerial && writeTelemetryLogToSerial)
            *nextIndex++ = (isalnum(next) ? next : '?');
          nextSecondSegmentByteToFind++;
        }
        else
        {
          if (writeLogToSerial && writeTelemetryLogToSerial)
            *nextIndex++ = (isalnum(next) ? next : (next == 0 ? '\0' : '?'));
          nextSecondSegmentByteToFind = uplink_preamble_second_segment;    // make sure contiguous preamble found, reset search for first char of preamble
        }

        if (writeLogToSerial && writeTelemetryLogToSerial)
          if (nextIndex == preambleAEJ + preambleAEJSize-10)
            nextIndex = preambleAEJ;
      }
    }
    else
    {
      if (writeLogToSerial && writeTelemetryLogToSerial)
        USB_SERIAL.printf("\nTimeout: Not Found preamble null terminator for MBJ\n");
    }

    if (writeLogToSerial && writeTelemetryLogToSerial)
    {
      *nextIndex++ = '\n';  *nextIndex++ = '\0';
      USB_SERIAL.printf(preambleAEJ);

      if (*nextSecondSegmentByteToFind != 0)
      {
        if (writeLogToSerial && writeTelemetryLogToSerial)
          USB_SERIAL.printf("    AEJ Timeout\n");
      }
    }

    if (*nextSecondSegmentByteToFind == 0)   // last byte of pre-amble found before no more bytes available
    {
      validPreambleFound = true;
      
      // message pre-amble found - read the rest of the received message.
      if (writeLogToSerial && writeTelemetryLogToSerial)
        USB_SERIAL.print("\nPre-Amble Found\n");
    }
    else
    {
      if (accumulateMissedMessageCount && nofix_byte_loop_count == -1)  // (must be at least 10 seconds since power on and first fix received)
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

bool populateHeadWithMakoTelemetry(BlockHeader& headBlock, const bool validPreambleFound)
{
  bool messageValidatedOk = false;

  uint16_t blockMaxPayload = 0;
  uint8_t* blockBuffer = headBlock.getBuffer(blockMaxPayload);
  uint8_t* nextBlockByte = blockBuffer;
  uint16_t headMaxPayloadSize = headBlock.getMaxPayloadSize();

  // 3. Populate the head block buffer with mako telemetry (or nullptr if no pre-amble found)
  if (validPreambleFound)
  {
    uplinkRxMicroSeconds = micros();

    // Allow max of 2ms to get a byte
    const uint32_t maxWaitOneByteMS = 2;
    uint32_t endWait = millis() + maxWaitOneByteMS;

    // 3.1a Read the uplink message from Serial into the blockBuffer
    while ((nextBlockByte-blockBuffer) < headMaxPayloadSize && (GOPRO_SERIAL.available() || millis() < endWait))
    {
      // must only listen for data when not sending gps data.
      // after send of gps must flush rx buffer
      if (GOPRO_SERIAL.available())
      {
        *(nextBlockByte++) = GOPRO_SERIAL.read();
        endWait = millis() + maxWaitOneByteMS;
      }
    }

    uint32_t nowUS = micros();
    uplinkRxMicroSeconds = (nowUS >= uplinkRxMicroSeconds ? nowUS - uplinkRxMicroSeconds : 0xFFFFFFFF - uplinkRxMicroSeconds + nowUS);

    if (writeLogToSerial && writeTelemetryLogToSerial)
      USB_SERIAL.printf("Rx Time: %lu\n",uplinkRxMicroSeconds);
 
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

  if (writeLogToSerial && writeTelemetryLogToSerial)
  {
    USB_SERIAL.printf("Mako uplinkMessageLength == %hu    ",uplinkMessageLength);

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

    USB_SERIAL.printf(firstLastBytes);
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
        if (writeLogToSerial)
          USB_SERIAL.printf("decodeUplink bad msg length %%2!=0 %hu  Rx Time: %lu\n", uplinkMessageLength, uplinkRxMicroSeconds);

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
        if (writeLogToSerial)
        {
          if (uplinkMessageLengthBad)
            USB_SERIAL.printf("decodeUplink bad msg length %hu && checksum bad %hu  Rx Time: %lu\n", uplinkMessageLength, uplink_checksum, uplinkRxMicroSeconds);
          else if (uplinkMessageLengthBad)
            USB_SERIAL.printf("decodeUplink bad msg length only %hu  Rx Time: %lu\n", uplinkMessageLength, uplinkRxMicroSeconds);
          else if (uplink_checksum_bad)
            USB_SERIAL.printf("decodeUplink bad msg checksum only %hu  Rx Time: %lu\n", uplink_checksum, uplinkRxMicroSeconds);
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

  if (writeLogToSerial && writeTelemetryLogToSerial)
    USB_SERIAL.printf("Mako roundedUpLength == %hu\n",roundedUpLength);

  uint16_t totalMakoAndLemonLength = roundedUpLength + sizeof(LemonTelemetryForStorage);

  // populate basictelemetry

  // construct lemon telemetry, append to the padded mako telemetry message and commit to the telemetry pipeline
  if (totalMakoAndLemonLength <= blockMaxPayload)
  {
    LemonTelemetryForStorage lemon_telemetry_for_storage;
    constructLemonTelemetryForStorage(lemon_telemetry_for_storage, latestLemonTelemetry, uplinkMessageLength);
    
    memcpy(nextBlockByte, (uint8_t*)&lemon_telemetry_for_storage,sizeof(LemonTelemetryForStorage));
    
    if (writeLogToSerial && writeTelemetryLogToSerial)
      USB_SERIAL.printf("memcpy done LemonTelemetryForStorage == sizeof %i\n",sizeof(LemonTelemetryForStorage));

    nextBlockByte+=sizeof(LemonTelemetryForStorage);

    if (writeLogToSerial && writeTelemetryLogToSerial)
      USB_SERIAL.printf("totalMakoAndLemonLength %hu\n",totalMakoAndLemonLength);

    headBlock.setPayloadSize(totalMakoAndLemonLength);

    bool isPipelineFull=false;
    telemetryPipeline.commitPopulatedHeadBlock(headBlock, isPipelineFull);
  
    if (writeLogToSerial)
      USB_SERIAL.printf("Commit head block: maxpipeblocklength=%hu longestpipe=%hu pipelineLength=%hu TH=%hu,%hu\n",telemetryPipeline.getMaximumPipelineLength(),telemetryPipeline.getMaximumDepth(),telemetryPipeline.getPipelineLength(),telemetryPipeline.getTailBlockIndex(),telemetryPipeline.getHeadBlockIndex());
  }
  else
  {
    // payload too large to fit into block
    if (writeLogToSerial)
      USB_SERIAL.printf("Combined Mako (%hu) and Lemon (%lu) payloads too large (%hu) to fit into telemetry block (%hu)\n",uplinkMessageLength,sizeof(LemonTelemetryForStorage),totalMakoAndLemonLength,blockMaxPayload);
  }
}

void getNextTelemetryMessagesUploadedToPrivateMQTT()
{
  BlockHeader tailBlock;
  const uint8_t maxTailPullsPerCycle = 10;   // allow up to 10 messages per cycle
  uint8_t tailPulls = maxTailPullsPerCycle;

  if (millis() < last_private_mqtt_upload_at + private_mqtt_upload_min_duty_ms) // upload throttle.
    return;

  while (telemetryPipeline.pullTailBlock(tailBlock) && tailPulls)
  {
    tailPulls--;
    
    if (writeLogToSerial && writeTelemetryLogToSerial)
      USB_SERIAL.printf("tail block pulled\n");

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
      
      if (writeLogToSerial)
      {
        USB_SERIAL.printf("tail block committed:  pipelineLength=%hu TH=%hu,%hu\n",telemetryPipeline.getPipelineLength(),telemetryPipeline.getTailBlockIndex(),telemetryPipeline.getHeadBlockIndex());
      }
    }
    else
    {
      if (writeLogToSerial)
        USB_SERIAL.printf("tail block NOT committed\n");

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
  s.vBusVoltage = (uint16_t)(M5.Axp.GetVBusVoltage() * 1000.0);
  s.vBusCurrent = (uint16_t)(M5.Axp.GetVBusCurrent() * 100.0);
  s.vBatVoltage = (uint16_t)(M5.Axp.GetBatVoltage() * 1000.0);
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
  GOPRO_SERIAL.write(fake_no_fix);
  delay(100);
}

const char* fake_no_gps = "$GPRMC,092204.999,A,4250.5589,S,14718.5084,E,0.00,89.68,211200,,*25\n";

void sendFakeGPSData_No_GPS()
{
  GOPRO_SERIAL.write(fake_no_gps);
  delay(100);
}


void toggleOTAActive()
{
  M5.Lcd.fillScreen(TFT_ORANGE);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLUE);
  M5.Lcd.setRotation(1);

  if (otaActive)
  {
    asyncWebServer.end();
    M5.Lcd.println("OTA Disabled");
    otaActive = false;
    delay (2000);
  }
  else
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      asyncWebServer.begin();
      M5.Lcd.printf("OTA Enabled");
      otaActive = true;
    }
    else
    {
      M5.Lcd.println("Error: Enable Wifi First");
    }
    delay (200);
  }

  M5.Lcd.fillScreen(TFT_BLACK);
}

void toggleWiFiActive()
{
  M5.Lcd.fillScreen(TFT_ORANGE);
  M5.Lcd.setCursor(0, 0);

  if (WiFi.status() == WL_CONNECTED)
  {
    if (otaActive)
    {
      asyncWebServer.end();
      M5.Lcd.println("OTA Disabled");
      otaActive = false;
    }

    WiFi.disconnect();
    ssid_connected = ssid_not_connected;
    M5.Lcd.printf("Wifi Disabled");
    delay (2000);
  }
  else
  {
    M5.Lcd.printf("Wifi Connecting");

    const bool wifiOnly = true;
    const int scanAttempts = 3;
    connectToWiFiAndInitOTA(wifiOnly,scanAttempts);
 
    M5.Lcd.fillScreen(TFT_ORANGE);
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setRotation(1);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLUE);

    M5.Lcd.printf(WiFi.status() == WL_CONNECTED ? "Wifi Enabled" : "No Connect");
    
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    delay(2000);
  }
  
  M5.Lcd.fillScreen(TFT_BLACK);
}

void shutdownIfUSBPowerOff()
{
  if (M5.Axp.GetVBusVoltage() < minimumUSBVoltage)
  {
    if (USBVoltageDropTime == 0)
      USBVoltageDropTime = millis();
    else
    {
      if (millis() > USBVoltageDropTime + milliSecondsToWaitForShutDown)
      {
        M5.Axp.PowerOff();
      }
    }
  }
  else
  {
    if (USBVoltageDropTime != 0)
      USBVoltageDropTime = 0;
  }
}

void fadeToBlackAndShutdown()
{
  for (int i = 90; i > 0; i=i-15)
  {
    M5.Axp.ScreenBreath(i);             // fade to black
    delay(100);
  }

  M5.Axp.PowerOff();
}

void checkForLeak(const char* msg, const uint8_t pin)
{
  bool leakStatus = false;

  if (pin == M5_POWER_SWITCH_PIN)
  {
    leakStatus = (M5.Axp.GetBtnPress());
  }
  else
  {
    leakStatus = !(digitalRead(pin));
  }

  if (leakStatus)
  {
    M5.Lcd.fillScreen(TFT_RED);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(5, 10);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
    M5.Lcd.print(msg);
    M5.Beep.setBeep(1200, 100);
    M5.Beep.beep();
    delay(100);
    updateButtonsAndBuzzer();

    M5.Lcd.fillScreen(TFT_ORANGE);
    M5.Lcd.setCursor(5, 10);
    M5.Lcd.setTextColor(TFT_YELLOW, TFT_ORANGE);
    M5.Lcd.print(msg);
    M5.Beep.setBeep(1500, 100);
    M5.Beep.beep();
    delay(100);

    updateButtonsAndBuzzer();
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Beep.mute();
  }
}

const char* scanForKnownNetwork() // return first known network found
{
  const char* network = nullptr;

  M5.Lcd.println("Scan WiFi\nSSIDs...");
  int8_t scanResults = WiFi.scanNetworks();

  if (scanResults != 0)
  {
    for (int i = 0; i < scanResults; ++i) 
    {
      // Print SSID and RSSI for each device found
      String SSID = WiFi.SSID(i);

//      delay(10);
      
      // Check if the current device starts with the peerSSIDPrefix
      if (strcmp(SSID.c_str(), ssid_1) == 0)
        network=ssid_1;
      else if (strcmp(SSID.c_str(), ssid_2) == 0)
        network=ssid_2;
      else if (strcmp(SSID.c_str(), ssid_3) == 0)
        network=ssid_3;

      if (network)
        break;
    }    
  }

  if (network)
  {
      M5.Lcd.printf("Found:\n%s",network);

    if (writeLogToSerial)
      USB_SERIAL.printf("Found:\n%s\n",network);
  }
  else
  {
    M5.Lcd.println("None\nFound");
    if (writeLogToSerial)
      USB_SERIAL.println("No networks Found\n");
  }

  // clean up ram
  WiFi.scanDelete();

  return network;
}

bool connectToWiFiAndInitOTA(const bool wifiOnly, int repeatScanAttempts)
{
  if (wifiOnly && WiFi.status() == WL_CONNECTED)
    return true;

  M5.Lcd.setCursor(0, 0);
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(2);

  while (repeatScanAttempts-- &&
         (WiFi.status() != WL_CONNECTED ||
          WiFi.status() == WL_CONNECTED && wifiOnly == false && otaActive == false ) )
  {
    const char* network = scanForKnownNetwork();
  
    if (!network)
    {
      delay(1000);
      continue;
    }
    
    int connectToFoundNetworkAttempts = 3;
    const int repeatDelay = 1000;
  
    if (strcmp(network,ssid_1) == 0)
    {
      while (connectToFoundNetworkAttempts-- && !setupOTAWebServer(ssid_1, password_1, label_1, timeout_1, wifiOnly))
        delay(repeatDelay);
    }
    else if (strcmp(network,ssid_2) == 0)
    {
      while (connectToFoundNetworkAttempts-- && !setupOTAWebServer(ssid_2, password_2, label_2, timeout_2, wifiOnly))
        delay(repeatDelay);
    }
    else if (strcmp(network,ssid_3) == 0)
    {
      while (connectToFoundNetworkAttempts-- && !setupOTAWebServer(ssid_3, password_3, label_3, timeout_3, wifiOnly))
        delay(repeatDelay);
    }
    
    delay(1000);
  }

  bool connected=WiFi.status() == WL_CONNECTED;
  
  if (connected)
  {
    ssid_connected = WiFi.SSID();
  }
  else
  {
    ssid_connected = ssid_not_connected;
  }
  
  return connected;
}

void webSerialReceiveMessage(uint8_t *data, size_t len){
  WebSerial.println("Received Data...");
  String d = "";
  for(int i=0; i < len; i++){
    d += char(data[i]);
  }

  WebSerial.println(d);

  if (d == "ON"){
    digitalWrite(RED_LED_GPIO, LOW);
  }
  else if (d=="OFF"){
    digitalWrite(RED_LED_GPIO, HIGH);
  }
  else if (d=="serial-off")
  {
    writeLogToSerial = false;
    WebSerial.closeAll();
  }
}

bool setupOTAWebServer(const char* _ssid, const char* _password, const char* label, uint32_t timeout, bool wifiOnly)
{
  if (wifiOnly && WiFi.status() == WL_CONNECTED)
  {
    if (writeLogToSerial)
      USB_SERIAL.printf("setupOTAWebServer: attempt to connect wifiOnly, already connected - otaActive=%i\n",otaActive);

    return true;
  }

  if (writeLogToSerial)
    USB_SERIAL.printf("setupOTAWebServer: attempt to connect %s wifiOnly=%i when otaActive=%i\n",_ssid, wifiOnly,otaActive);

  bool forcedCancellation = false;

  M5.Lcd.setCursor(0, 0);
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(2);
  bool connected = false;
  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname("lemon"); //define hostname

  WiFi.begin(_ssid, _password);
#ifdef ENABLE_TELEGRAM_BOT_AT_COMPILE_TIME
  if (enableTelegram)
    secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org
#endif

  // Wait for connection for max of timeout/1000 seconds
  M5.Lcd.printf("%s Wifi", label);
  int count = timeout / 500;
  while (WiFi.status() != WL_CONNECTED && --count > 0)
  {
    // check for cancellation button - top button.
    updateButtonsAndBuzzer();

    if (p_primaryButton->isPressed()) // cancel connection attempts
    {
      forcedCancellation = true;
      break;
    }

    M5.Lcd.print(".");
    delay(500);
  }
  M5.Lcd.print("\n\n");

  if (WiFi.status() == WL_CONNECTED )
  {
    if (wifiOnly == false && !otaActive)
    {
      if (writeLogToSerial)
        USB_SERIAL.println("setupOTAWebServer: WiFi connected ok, starting up OTA");

      if (writeLogToSerial)
        USB_SERIAL.println("setupOTAWebServer: calling asyncWebServer.on");

      asyncWebServer.on("/", HTTP_GET, [](AsyncWebServerRequest * request) 
      {
        request->send(200, "text/plain", "To upload firmware use /update");
      });

      asyncWebServer.on("/reboot", HTTP_GET, [](AsyncWebServerRequest * request) 
      {
        request->send(200, "text/plain", "Rebooting");
        delay(500);
        esp_restart();
      });

      asyncWebServer.on("/stats", HTTP_GET, [](AsyncWebServerRequest * request) 
      {
          AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", STATS_HTML, STATS_HTML_SIZE); 
          request->send(response);
      });

      asyncWebServer.on("/map", HTTP_GET, [](AsyncWebServerRequest * request)
      {
          AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", MAP_HTML, MAP_HTML_SIZE); 
          request->send(response);
      });
          
      asyncWebServer.on("/stats", HTTP_POST, [&](AsyncWebServerRequest *request)
      {
              const AsyncWebParameter* pButton = request->getParam("button",true,false);

              if (pButton)
              {
                  request->send(200, "text/html", "ok");
                  if (pButton->value() == String("rebootButton"))
                  {
                    esp_restart();
                  }
                  else if (pButton->value() == String("clearCountersButton"))
                  {
                    uplinkMessageMissingCount = consoleDownlinkMsgCount = privateMQTTUploadCount = 0;
                    badUplinkMessageCount = badLengthUplinkMsgCount = badChkSumUplinkMsgCount = goodUplinkMessageCount = 0;
                  }
                  else if (pButton->value() == String("showOnMapButton"))
                  {
                    const AsyncWebParameter* pChoice = request->getParam("choice",true,false);
                    if (pChoice)
                    {
                      showOnMapRequest=pChoice->value();

                      showOnMapRequestIndex = -1;

                      String searchWaypoint;
                      for (int i=0; i<WraysburyWaypoints::getWaypointsCount(); i++)
                      {
                        searchWaypoint=WraysburyWaypoints::waypoints[i]._label;
                        if (searchWaypoint.indexOf(showOnMapRequest) != -1)
                        {
                          showOnMapRequestIndex = i;
                          break;
                        }
                      }

                      if (showOnMapRequestIndex == -1)
                        showOnMapRequest = "";
                    }
                  }
                  else if (pButton->value() == String("setTargetButton"))
                  {
                    const AsyncWebParameter* pTarget = request->getParam("target",true,false);
                    if (pTarget)
                    {
                      setTargetRequest=pTarget->value();

                      setTargetRequestIndex = -1;

                      String searchWaypoint;
                      for (int i=0; i<WraysburyWaypoints::getWaypointsCount(); i++)
                      {
                        searchWaypoint=WraysburyWaypoints::waypoints[i]._label;
                        if (searchWaypoint.indexOf(setTargetRequest) != -1)
                        {
                          setTargetRequestIndex = i;
                          break;
                        }
                      }

                      if (setTargetRequestIndex == -1)
                        setTargetRequest = "";
                    }
                  }             }
              else
              {
                request->send(200, "text/plain", "invalid");
              }
      });

      initWebSocket();

      if (writeLogToSerial)
        USB_SERIAL.println("setupOTAWebServer: calling AsyncElegantOTA.begin");

      AsyncElegantOTA.setID(MERCATOR_OTA_DEVICE_LABEL);
      AsyncElegantOTA.setUploadBeginCallback(uploadOTABeginCallback);
      AsyncElegantOTA.begin(&asyncWebServer);    // Start AsyncElegantOTA


      #ifdef USE_WEBSERIAL
        static bool webSerialInitialised = false;
        if (!webSerialInitialised)
        {
          WebSerial.begin(&asyncWebServer);
          WebSerial.msgCallback(webSerialReceiveMessage);
          webSerialInitialised = true;
        }
      #endif

      if (writeLogToSerial)
        USB_SERIAL.println("setupOTAWebServer: calling asyncWebServer.begin");

      asyncWebServer.begin();

      dumpHeapUsage("setupOTAWebServer(): after asyncWebServer.begin");

      if (writeLogToSerial)
        USB_SERIAL.println("setupOTAWebServer: OTA setup complete");

      M5.Lcd.setRotation(0);
      
      M5.Lcd.fillScreen(TFT_BLACK);
      M5.Lcd.setCursor(0,155);
      M5.Lcd.setTextSize(2);
      M5.Lcd.printf("%s\n\n",WiFi.localIP().toString());
      M5.Lcd.println(WiFi.macAddress());
      connected = true;
      otaActive = true;
  
      M5.Lcd.qrcode("http://"+WiFi.localIP().toString()+"/update",0,0,135);
  
      delay(2000);

      connected = true;
  
      updateButtonsAndBuzzer();
  
      if (p_secondButton->isPressed())
      {
        M5.Lcd.print("\n\n20\nsecond pause");
        delay(20000);
      }
    }
  }
  else
  {
    if (forcedCancellation)
      M5.Lcd.print("\nCancelled\nConnect\nAttempts");
    else
    {
      if (writeLogToSerial)
        USB_SERIAL.printf("setupOTAWebServer: WiFi failed to connect %s\n",_ssid);

      M5.Lcd.print("No Connect");
    }
  }

  M5.Lcd.fillScreen(TFT_BLACK);

  return connected;
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
  enum e_q_upload_status uploadStatus = Q_UNDEFINED_ERROR;

  if(enableUploadToPrivateMQTT)
  {
    // This needs to be either localMQTT or remoteMQTT
    PicoMQTT::Client* MQTTClient = (usingDevNetwork ? &localMQTT : &remoteMQTT);

    if (WiFi.status() == WL_CONNECTED)
    {
      if (MQTTClient->connected())
      {
        buildUplinkTelemetryMessageV6a(mqtt_payload, *makoTelemetry, *lemonTelemetry);

        const int qos = 1;
        last_private_mqtt_upload_at = millis();
        bool result = MQTTClient->publish("test", mqtt_payload,qos);

        if (result)
        {
          toggleRedLED();
          uploadStatus = Q_SUCCESS_SEND;
//          if (writeLogToSerial)
  //          USB_SERIAL.printf("PrivateMQTT Client sent message %s\n", mqtt_payload);
          privateMQTTUploadCount++;
        }
        else
        {
          if (writeLogToSerial)
            USB_SERIAL.printf("Private MQTT Client failed to send message. Publish returned false.\n");
          uploadStatus = Q_MQTT_CLIENT_SEND_ERROR;
        }
      }
      else
      {
        uploadStatus = Q_MQTT_CLIENT_CONNECT_ERROR;
        if (writeLogToSerial)
          USB_SERIAL.printf("Private MQTT Client error - not connected\n");
      }
    }
    else
    {
      uploadStatus = Q_NO_WIFI_CONNECTION;

      if (writeLogToSerial)
        USB_SERIAL.println("Private MQTT No Wifi\n");
    }
  }
  else
  {
    uploadStatus = Q_SUCCESS_NOT_ENABLED;

    if (writeLogToSerial)
      USB_SERIAL.println("Private MQTT Not Enabled\n");
  }

  return uploadStatus;
}



