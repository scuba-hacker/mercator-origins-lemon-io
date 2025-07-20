#include <Arduino.h>

// rename the git file "mercator_secrets_template.c" to the filename below, filling in your wifi credentials etc.
#include "mercator_secrets.c"

#include <UMS3.h>
UMS3 ProS3;

#include <U8g2lib.h>
#define OLED_RST_BROWN     12
#define OLED_DC_PURPLE     13
#define OLED_CS_ORANGE     14
#define OLED_CLK_YELLOW    15
#define OLED_DIN_MOSI_BLUE 16
U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI u8g2(U8G2_R0, OLED_CS_ORANGE, OLED_DC_PURPLE, OLED_RST_BROWN);

#include <SPI.h>

#include "FS.h"
#include "SPIFFS.h"

#include <Button.h>
#include <colours.h>

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
#include "MercatorMQTT.h"

#include "root_ca.h"
extern const char* isrg_root_ca;

#include <WiFi.h>
#include <ESP32Ping.h>

#define DEBOUNCE_MS 10
#define RED_BUTTON_GPIO 42
Button redButton = Button(RED_BUTTON_GPIO, true, DEBOUNCE_MS);

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
  #define USB_SERIAL_BASE WebSerial
#else
  #define USB_SERIAL_BASE Serial
#endif

#define USB_SERIAL_PRINTF(...) do { if (writeLogToSerial) USB_SERIAL_BASE.printf(__VA_ARGS__); } while(0)
#define USB_SERIAL_PRINTLN(...) do { if (writeLogToSerial) USB_SERIAL_BASE.println(__VA_ARGS__); } while(0)
#define USB_SERIAL_PRINT(...) do { if (writeLogToSerial) USB_SERIAL_BASE.print(__VA_ARGS__); } while(0)

// Keep USB_SERIAL for non-conditional usage (like WebSerial setup)
#define USB_SERIAL USB_SERIAL_BASE

// START FEATURE ENABLE FLAGS
bool writeLogToSerial = true;
bool writeTelemetryLogToSerial = true; // writeLogToSerial must also be true

bool enableMQTTEncryption = true; // Set to true to use encrypted MQTT connections (port 8883, otherwise port 8887)

bool enableReadUplinkComms = true;
bool enableGPSRead = true;
bool enableAllUplinkMessageIntegrityChecks = true;
bool enableConnectToPrivateMQTT = true;
bool enableUploadToPrivateMQTT = true;
const bool enableOTAServer = true;          // over the air updates

const bool publishMQTTTestMessages = true;

//#define ENABLE_TELEGRAM_BOT_AT_COMPILE_TIME
#ifdef ENABLE_TELEGRAM_BOT_AT_COMPILE_TIME
  const bool enableTelegram = false;          // requires 35KB heap to send message (open/close secure connection)
#endif
// END FEATURE ENABLE FLAGS

// ################## START SERIAL/UART/GPIO CONFIGURATION
const int GPS_BAUD_RATE = 9600;

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

const int NEOPIXELS_ARDUINO_BAUD_RATE = 9600;

#define MAKO_GOPRO_SERIAL Serial1

const uint8_t GPS_TX_GPIO = 38;
const uint8_t GPS_RX_GPIO = 39;

const uint8_t MAKO_GOPRO_TX_GPIO = 38;    // should be called mako gopro GPIO
const uint8_t MAKO_GOPRO_RX_GPIO = 39;    // should be called mako gopro GPIO

const uint8_t TX_TO_NEOPIXELS_GPIO = 40;
const uint8_t RX_TO_NEOPIXELS_GPIO = 41;

const uint8_t STATUS_LED_GPIO = 42;

#define STATUS_LED_ON HIGH
#define STATUS_LED_OFF LOW
// ################## END SERIAL/UART CONFIGURATION

uint8_t statusLED = STATUS_LED_OFF;

// ################## START LANTERN NEO-PIXEL CONFIGURATION
enum e_display_brightness {OFF_DISPLAY = 0, DIM_DISPLAY = 25, HALF_BRIGHT_DISPLAY = 50, BRIGHTEST_DISPLAY = 100};
const e_display_brightness ScreenBrightness = BRIGHTEST_DISPLAY;

enum e_lemon_status{LC_NONE=0, LC_STARTUP=1, LC_SEARCH_WIFI=2, LC_FOUND_WIFI=3, LC_NO_WIFI=4, LC_NO_GPS=5, 
                    LC_NO_FIX=6, LC_GOOD_FIX=7, LC_ALL_OFF=8, LC_DIVE_IN_PROGRESS=64, LC_NO_STATUS_UPDATE=127, LC_NO_INTERNET=128};

e_lemon_status lemonStatus = LC_STARTUP;
// ################## END LANTERN NEO-PIXEL CONFIGURATION

// ################## START MQTT Configuration
MQTTConfig mqttConfig = {
    private_mqqt_local_host,
    enableMQTTEncryption ? private_mqqt_local_port_tls : private_mqqt_local_port,   // Use port 8883 for TLS, otherwise use configured port
    private_mqqt_remote_host,
    enableMQTTEncryption ? private_mqqt_remote_port_tls : private_mqqt_remote_port,  // Use port 8883 for TLS, otherwise use configured port
    private_mqqt_client_id,
    private_mqqt_username,
    private_mqqt_password,
    private_local_gateway,
    private_dev_ssid,
    enableMQTTEncryption
};

MercatorMQTT privateMQTT(mqttConfig);

uint32_t currentPrivateMQTTUploadAt = 0, lastPrivateMQTTUploadAt = 0;
uint32_t privateMQTTUploadDutyCycle = 0;
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

bool restartForGoodOTAScheduled = false;
uint32_t restartAfterGoodOTAUpdateAt = 0;


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

const char* leakAlarmMsg = "    Float\n\n    Leak!";

uint32_t fixCount = 0;
uint32_t passedChecksumCount = 0;
bool processUplinkMessage = true;

uint8_t journey_activity_count = 0;
const char* journey_activity_indicator = "\\|/-";

TinyGPSPlus gps;
int uart_number_gps = 2;
HardwareSerial gps_serial(uart_number_gps);

int uart_number_mako_gopro = 1;
HardwareSerial ss_to_mako_gopro(uart_number_mako_gopro);

HardwareSerial& neopixels_serial = Serial0;

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

const uint8_t LEAK_DETECTOR_GPIO = 42;

Button* p_primaryButton = nullptr;
void updateButtonsAndBuzzer();

extern const uint8_t STATS_HTML[];
extern const uint32_t STATS_HTML_SIZE;

extern const uint8_t MAP_HTML[];
extern const uint32_t MAP_HTML_SIZE;

void toggleOTAActive();
void toggleWiFiActive();

void checkForLeak(const char* msg);

void checkForFloatBoxReedSwitches();

bool setupOTAWebServer(const char* _ssid, const char* _password, const char* label, uint32_t timeout, bool wifiOnly);

void updateButtonsAndBuzzer()
{
  p_primaryButton->read();
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
char* customiseNMEASentence(char* sentence, int showOnMapIndex);
char* getMQTTPayloadBuffer();
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
  t.imu_lin_acc_x = t.imu_lin_acc_y = t.imu_lin_acc_z = uninitialisedIMU;
  t.imu_rot_acc_x = t.imu_rot_acc_y = t.imu_rot_acc_z = uninitialisedIMU;
}

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
  strcpy(IPBuffer,wait_ip_label);
  
  USB_SERIAL_PRINTF("***** Connected to %s successfully! *****\n",info.wifi_sta_connected.ssid);
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
  strcpy(IPBuffer,WiFi.localIP().toString().c_str());
  strcpy(IPLocalGateway, WiFi.gatewayIP().toString().c_str());
  strcpy(WiFiSSID, WiFi.SSID().c_str());

  bool isDevNet = (!strcmp(IPLocalGateway,private_local_gateway) && !strcmp(WiFiSSID, private_dev_ssid));
  privateMQTT.setUsingDevNetwork(isDevNet);

  USB_SERIAL_PRINTF("***** WiFi CONNECTED IP: %s ******\n",IPBuffer);
}

bool devNetworkInUse()
{ 
  extern const char* private_local_gateway;
  extern const char* private_dev_ssid;
  return (!strcmp(IPLocalGateway,private_local_gateway) && !strcmp(WiFiSSID, private_dev_ssid));
}

void WiFiLostIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
  strcpy(IPBuffer,lost_ip_label);
  strcpy(IPBuffer,"");
  strcpy(IPLocalGateway, "");
  strcpy(WiFiSSID, WiFi.SSID().c_str());

  privateMQTT.setUsingDevNetwork(false);

  USB_SERIAL_PRINTF("***** WiFi LOST IP ******\n");
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
  strcpy(IPBuffer,no_wifi_label);
  strcpy(IPLocalGateway, "");
  strcpy(WiFiSSID, "");

  privateMQTT.setUsingDevNetwork(false);

  USB_SERIAL_PRINTF("***** WiFi DISCONNECTED: Reason: %d ******\n",info.wifi_sta_disconnected.reason);
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

      USB_SERIAL_PRINTLN("0. checkConnectivity: Pipeline not draining");

      if (WiFi.status() == WL_CONNECTED)
      {
        USB_SERIAL_PRINTLN("1.1 checkConnectivity: WIFI is connected, ping 8.8.8.8");
        
        // either a 4G or broker server connection issue
        if (isInternetAccessible())   // ping google DNS
        {
          USB_SERIAL_PRINTLN("1.2.1 checkConnectivity: WiFi ok, internet ping success");
        }
        else
        {
          USB_SERIAL_PRINTLN("1.2.2 checkConnectivity: WiFi ok, ping fail, out of coverage");
          
          g_offlineStorageThrottleApplied = true;
        }

        if (isScubaMosquittoBrokerAvailable())
        {
          USB_SERIAL_PRINTLN("1.2.3 checkConnectivity: Scuba MQTT Broker ping success");
        }
        else
        {
          USB_SERIAL_PRINTLN("1.2.4 checkConnectivity: WiFi ok, ping google ok, MQTT Broker fail");
          
          g_offlineStorageThrottleApplied = true;
        }

      }
      else
      {
        g_offlineStorageThrottleApplied = true;
        
        USB_SERIAL_PRINTLN("checkConnectivity: WIFI not connected, attempt reconnect");

        // Do a manual wifi reconnect attempt - synchronous
        if (WiFi.reconnect())
        {
          USB_SERIAL_PRINTLN("checkConnectivity: WIFI reconnect success");          
        }
        else
        {
          USB_SERIAL_PRINTLN("checkConnectivity: WIFI reconnect fail");          
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
    return privateMQTT.isConnected();
}

void dumpHeapUsage(const char* msg)
{  
  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // internal RAM, memory capable to store data or to create new task

  USB_SERIAL_PRINTF("\n%s : free heap bytes: %i  largest free heap block: %i min free ever: %i\n",  msg, info.total_free_bytes, info.largest_free_block, info.minimum_free_bytes);
  USB_SERIAL_PRINTF("Internal heap: %u bytes %u KB free\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
  USB_SERIAL_PRINTF("SPIRAM heap  : %u bytes %u KB free\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
}

void toggleStatusLED() { statusLED = !statusLED; ProS3.setPixelPower(statusLED); ProS3.writePixel(); }
void statusLEDOn()     { statusLED = true;       ProS3.setPixelPower(statusLED); ProS3.writePixel(); }
void statusLEDOff()    { statusLED = false;      ProS3.setPixelPower(statusLED); ProS3.writePixel(); }
void statusLEDColour() { ProS3.setPixelColor(128,128,0); }

bool haltAllProcessingDuringOTAUpload = false;

void disableFeaturesForOTA()
{
  enableConnectToPrivateMQTT = false;
  enableUploadToPrivateMQTT = false;
  privateMQTT.setEnabled(false, false);
  enableReadUplinkComms = false;
  processUplinkMessage = false;
  enableAllUplinkMessageIntegrityChecks = false;
  enableGPSRead = false;
  writeLogToSerial = false;
  writeTelemetryLogToSerial = false;

  gps_serial.end();
  MAKO_GOPRO_SERIAL.end();
  neopixels_serial.end();

  privateMQTT.disconnect();
  
  statusLEDOn();

  haltAllProcessingDuringOTAUpload = true;

  dumpHeapUsage("Disabled OTA stats: ");

  telemetryPipeline.teardown();

  dumpHeapUsage("Torn Down Telemetry Pipeline: ");

  #ifdef USE_WEBSERIAL
    ws.closeAll();          // close all websocket connections for test page
    WebSerial.closeAll();   // close all websocket connetions for WebSerial

    dumpHeapUsage("Closed Web Sockets and Web Serial : ");
  #endif
}

TaskHandle_t mainTaskHandle = nullptr;
BaseType_t mainTaskCoreId = 0;

void uploadOTABeginCallback(AsyncElegantOtaClass* originator)
{
  disableFeaturesForOTA();
}

void uploadOTASucceededCallback(AsyncElegantOtaClass* originator)
{
  restartAfterGoodOTAUpdateAt = millis() + 3000;
  restartForGoodOTAScheduled = true;
}

void setup()
{
  ProS3.begin();
  statusLEDColour();
  statusLEDOn();

  SPI.begin(OLED_CLK_YELLOW, /*MISO=*/-1, OLED_DIN_MOSI_BLUE, OLED_CS_ORANGE);
  u8g2.begin();

  privateMQTT.setConnectionCallbacks(
    [&] { USB_SERIAL_PRINTF("Local MQTT connected (%s)\n", privateMQTT.getEncryptionStatus()); },
    [&] { USB_SERIAL_PRINTF("Local MQTT disconnected (%s)\n", privateMQTT.getEncryptionStatus()); },
    [&] { USB_SERIAL_PRINTF("Remote MQTT connected (%s)\n", privateMQTT.getEncryptionStatus()); },
    [&] { USB_SERIAL_PRINTF("Remote MQTT disconnected (%s)\n", privateMQTT.getEncryptionStatus()); }
  );

  mainTaskCoreId = xPortGetCoreID();
  mainTaskHandle = xTaskGetCurrentTaskHandle();

  Serial.begin(115200);
  Serial.flush();
  delay(1000);
  USB_SERIAL_PRINTLN("Unexpected Maker Pro S3 Initialised...");

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
  } else {
    Serial.println("SPIFFS mounted OK");
  }

  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiLostIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_LOST_IP);
  WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  
  strcpy(IPBuffer,no_wifi_label);

  ssid_connected = ssid_not_connected;

  USB_SERIAL_PRINTF("sizeof LemonTelemetry: %lu\n",sizeof(LemonTelemetryForStorage));

  dumpHeapUsage("main: prior to Telemetry Pipeline creation  ");

  // On M5 Stick C Plus - if telegram is enabled the pipeline needs to be 60KB or smaller. Without pipeline length can be 95KB.
  // On ProS3 make it massive, eg 4MB! On ProS3 even a 60KB pipeline gets allocated to the PSRAM automatically.
  // 4 MB of buffer equates to 2048 * 4 = 8192 messages.
  // at one message every 2 seconds this is 4.5 hours of collection before running out of space!
  // This means that if out of internet coverage then it will buffer for this long before getting back into coverage.
  const uint16_t maxPipelineBufferKB = 2048;
  const uint16_t maxPipelineBlockPayloadSize = 256; // was 224 - Assuming 120 byte Mako Telemetry Msg and 104 byte Lemon Telemetry Msg
  BlockHeader::s_overrideMaxPayloadSize(maxPipelineBlockPayloadSize);  // 400 messages with 256 byte max payload. 
  telemetryPipeline.init(&millis,maxPipelineBufferKB);

  dumpHeapUsage("main: after Telemetry Pipeline creation  ");

  statusLEDOff();

  pinMode(TX_TO_NEOPIXELS_GPIO, OUTPUT);
  digitalWrite(TX_TO_NEOPIXELS_GPIO, HIGH); // switch off
  pinMode(RX_TO_NEOPIXELS_GPIO, INPUT);

  const bool invert = false;
  neopixels_serial.begin(NEOPIXELS_ARDUINO_BAUD_RATE, SERIAL_8N1, RX_TO_NEOPIXELS_GPIO, TX_TO_NEOPIXELS_GPIO, invert);

  sendLemonStatus(LC_STARTUP);

  p_primaryButton = &redButton;

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

  gps_serial.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_GPIO, GPS_TX_GPIO);   // pin 33=rx (white M5), pin 32=tx (yellow M5), specifies the grove SCL/SDA pins for Rx/Tx

  // setup second serial port for sending/receiving data to/from GoPro
  MAKO_GOPRO_SERIAL.setRxBufferSize(1024); // was 256 - must set before begin
  MAKO_GOPRO_SERIAL.begin(UPLINK_BAUD_RATE, SERIAL_8N2, MAKO_GOPRO_RX_GPIO, MAKO_GOPRO_TX_GPIO);

  // cannot use Pin 0 for receive of GPS (resets on startup), can use Pin 36, can use 26
  // cannot use Pin 0 for transmit of GPS (resets on startup), only Pin 26 can be used for transmit.

  if (enableUploadToPrivateMQTT)
  {
    privateMQTT.begin();
  }
}

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

  bool overrideLocation = false;

  USB_SERIAL_PRINTLN("0. checking for override location");
  USB_SERIAL_PRINTF("0.0 showOnMapIndex=%i isGNGAA=%i isGNRMC=%i strlen(startSentence)=%zu\n",showOnMapIndex, (isGNGGA ? 1 : 0),(isGNRMC ? 1 : 0), strnlen(startSentence,minimumSentenceLength));
  USB_SERIAL_PRINTF("0.1 %s\n",startSentence);

  // 
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

    USB_SERIAL_PRINTF("5.0 %s    <-- All updates new  Location Sentence\n", startSentence);
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

MQTTConnectionResult publishMQTTTestMessageOnDutyCycle(const char* topic="test_mqtt", uint32_t testPublishDutyCycle=1000)
{
    MQTTConnectionResult result = MQTTConnectionResult::UNDEFINED_ERROR;
    static uint32_t lastTestMessagePublishedAt = millis();
    if (millis() - lastTestMessagePublishedAt > testPublishDutyCycle)
    {
      char message[128];
      snprintf(message,sizeof(message),"[%lu] This is a test message from Lemon_V2 (%s)", millis(), privateMQTT.getEncryptionStatus());
      result = privateMQTT.publish(topic, message);
      USB_SERIAL_PRINTF("[%lu] Publish MQTT Test message on topic %s (%s)  Result = %s\n", millis(), topic, privateMQTT.getEncryptionStatus(), MercatorMQTT::resultToText(result));
      lastTestMessagePublishedAt = millis();
    }
    return result;
}

void loop()
{
  if (restartForGoodOTAScheduled && millis() >= restartAfterGoodOTAUpdateAt) 
  {
        USB_SERIAL_PRINTLN("Restarting now...");
        ESP.restart();
  }

  if (haltAllProcessingDuringOTAUpload)
  {  
    delay(100);
    toggleStatusLED();
    return;
  }

  if (makoReportsLeak)
  {
    if (mainBackColour == TFT_BLACK)
    {
      mainBackColour = TFT_ORANGE;
      // Do what for e-paper leak?
//      M5.Lcd.fillScreen(TFT_ORANGE);
    }
  }

//  M5.Lcd.setTextColor(TFT_WHITE,mainBackColour);

  updateButtonsAndBuzzer();

  if (enableUploadToPrivateMQTT)
  {
      privateMQTT.loop();
  }

  if (publishMQTTTestMessages)
    publishMQTTTestMessageOnDutyCycle();

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
    checkForLeak(leakAlarmMsg);

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
        // send message to outgoing serial connection to mako gopro
        MAKO_GOPRO_SERIAL.write(customiseNMEASentence(gps.getSentence(), showOnMapRequestIndex));
        consoleDownlinkMsgCount++;

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

          USB_SERIAL_PRINTF("\nFix: %lu Good GPS Msg: %lu Bad GPS Msg: %lu\n", fixCount, newPassedChecksum, gps.failedChecksum());
        }

        if (nofix_byte_loop_count > -1)
        {
          // clear the onscreen counter that increments whilst attempting to get first valid location
          nofix_byte_loop_count = -1;
//          M5.Lcd.fillScreen(TFT_BLACK);
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

//          M5.Lcd.setCursor(50, 100);
//          M5.Lcd.printf("%d", nofix_byte_loop_count++);
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
//    M5.Lcd.setCursor(55, 5);
//    M5.Lcd.setTextSize(4);
//    M5.Lcd.print("No Fix\n\n   Lemon\n");
//    M5.Lcd.setCursor(110, 45);
//    M5.Lcd.printf("%c", journey_activity_indicator[(++journey_activity_count) % 4]);
    sendLemonStatus(LC_NO_FIX);

    // tells mako gopro M5 that gps is alive but no fix yet.
    // mako gopro M5 can choose to show this data for test purposes, otherwise in
    // swimming pool like new malden or putney there may be no gps signal so
    // won't be able to test the rest, eg compass, temperature, humidity, buttons, reed switches
    // note the leak sensor is active at all times in the mako gopro M5.
    sendFakeGPSData_No_Fix();

    delay(250); // no fix wait
  }
  else if (nofix_byte_loop_count != -1)
  {
    // No GPS is reported when no bytes have ever been received on the UART.
    // Once messages start being received, this is blocked as it is normal
    // to have gaps in the stream. There is no indication if GPS stream hangs
    // after first byte received, eg no bytes within 10 seconds.

 //   M5.Lcd.setCursor(55, 5);
 //   M5.Lcd.setTextSize(4);
 //   M5.Lcd.print("No GPS\n\n   Lemon\n");
 //   M5.Lcd.setCursor(110, 45);
 //   M5.Lcd.printf("%c", journey_activity_indicator[(++journey_activity_count) % 4]);
    sendLemonStatus(LC_NO_GPS);

    // tells mako gopro M5 that gps is alive but no fix yet.
    // mako gopro M5 can choose to show this data for test purposes, otherwise in
    // swimming pool like new malden or putney there may be no gps signal so
    // won't be able to test the rest, eg compass, temperature, humidity, buttons, reed switches
    // note the leak sensor is active at all times in the mako gopro M5.
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
      
      // OLED-UPDATE-HERE
      // M5.Lcd.setCursor(5, 5);
      // M5.Lcd.setTextColor(TFT_WHITE, mainBackColour);          
      // M5.Lcd.setTextSize(3);

      const bool showBadChecksumInsteadofAllBad=true;

      // if (showBadChecksumInsteadofAllBad)
      //   M5.Lcd.printf("Fix %lu\nR^ %lu !%lu\n",fixCount, goodUplinkMessageCount, badChkSumUplinkMsgCount);
      // else
      //   M5.Lcd.printf("Fix %lu\nR^ %lu !%lu\n",fixCount, goodUplinkMessageCount, badUplinkMessageCount);
 
      // if (g_offlineStorageThrottleApplied && telemetryPipeline.isPipelineDraining() == false)
      //   M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
      // else if (g_offlineStorageThrottleApplied && telemetryPipeline.isPipelineDraining())
      //   M5.Lcd.setTextColor(TFT_BLACK, TFT_ORANGE);
      // else if (telemetryPipeline.getPipelineLength() > 4)
      //   M5.Lcd.setTextColor(TFT_BLACK, TFT_YELLOW);
      // else
      //   M5.Lcd.setTextColor(TFT_WHITE, mainBackColour);

      // this is a feature flag for testing
      // const bool showPipeLength=false;
      // if (showPipeLength)
      //   M5.Lcd.printf("P %-3hu Mis %hu\n",telemetryPipeline.getPipelineLength(),uplinkMessageMissingCount);
      // else
      //   M5.Lcd.printf("L%-3hu Mis %hu\n",badLengthUplinkMsgCount,uplinkMessageMissingCount);

      // if (WiFi.status() != WL_CONNECTED)
      //   M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
      // else
      //   M5.Lcd.setTextColor(TFT_WHITE, mainBackColour);

      // this is a feature flag for testing
      // const bool showListenTimer = false;
      // if (showListenTimer)       
      //   M5.Lcd.printf("Q %lu UT %lu  \n",privateMQTTUploadCount,uplinkMessageListenTimer);
      // else
      //   M5.Lcd.printf("Q %lu !%.1f%%\n",privateMQTTUploadCount,uplinkBadMessagePercentage);
      
      // M5.Lcd.setTextSize(2);

      // if (WiFi.status() != WL_CONNECTED) 
      //   M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
      // else
      //   M5.Lcd.setTextColor(TFT_WHITE, mainBackColour);

      // int16_t xCurs = M5.Lcd.getCursorX();
      // int16_t yCurs = M5.Lcd.getCursorY();

      // Option 1 for this line output
//      // M5.Lcd.printf("     %-15s", IPBuffer); //(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "No WiFi         "));
      // Option 2 for this line output
//      // M5.Lcd.printf("     %i %-15s", showOnMapRequestIndex, showOnMapRequest.c_str()); //(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "No WiFi         "));
      // Option 3 for this line output - this is the one I have been using most recently
//      M5.Lcd.printf("     %i %-15s", setTargetRequestIndex, setTargetRequest.c_str()); //(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "No WiFi         "));

      // M5.Lcd.setCursor(xCurs,yCurs);
      // M5.Lcd.setTextColor(TFT_MAGENTA, TFT_BLACK);
      // M5.Lcd.printf("%.1fC",M5.Axp.GetTempInAXP192());

      // M5.Lcd.setTextColor(TFT_WHITE, mainBackColour);
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

  checkForLeak(leakAlarmMsg);

  checkForFloatBoxReedSwitches();

#ifdef ENABLE_TELEGRAM_BOT_AT_COMPILE_TIME
  if (enableTelegram && now > timeOfNextTelegramBotUpdateSendMsg)
  {
    bool result = telegramBot.sendSimpleMessage(TELEGRAM_USER_ID, "Heartbeat", "");
    USB_SERIAL_PRINTF("Result of Bot Send - loop(): %d\n",result);
    timeOfNextTelegramBotUpdateSendMsg = millis() + telegramBotDutyCycle;
    dumpHeapUsage("loop() - after send telegram");
  }
#endif
}

// This is only a test function for the Arduino neopixel UART
void checkForFloatBoxReedSwitches()
{
  while (!writeLogToSerial && neopixels_serial.available())
  {
    neopixelSerialByteRead = neopixels_serial.read();
    // have an indication on the screen of a byte read and which byte
    // these map to the reed switches that are in the float box
    if (neopixelSerialByteRead == 100)
    {
      mainBackColour = TFT_BLUE;
//      M5.Lcd.fillScreen(TFT_BLUE);
    }
    else if (neopixelSerialByteRead == 200)
    {
      mainBackColour = TFT_MAGENTA;
//      M5.Lcd.fillScreen(TFT_MAGENTA);
    }
  }
}

void sendStatsWebSocketNotification()
{
    notifyWebSocketClients(getStats());
}


void toggleOTAActive()
{
  // M5.Lcd.fillScreen(TFT_ORANGE);
  // M5.Lcd.setCursor(10, 10);
  // M5.Lcd.setTextSize(3);
  // M5.Lcd.setTextColor(TFT_WHITE, TFT_BLUE);
  // M5.Lcd.setRotation(1);

  if (otaActive)
  {
    asyncWebServer.end();
    // M5.Lcd.println("OTA Disabled");
    otaActive = false;
    delay (2000);
  }
  else
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      asyncWebServer.begin();
      // M5.Lcd.printf("OTA Enabled");
      otaActive = true;
    }
    else
    {
      // M5.Lcd.println("Error: Enable Wifi First");
    }
    delay (200);
  }

  // M5.Lcd.fillScreen(TFT_BLACK);
}

void toggleWiFiActive()
{
  // M5.Lcd.fillScreen(TFT_ORANGE);
  // M5.Lcd.setCursor(0, 0);

  if (WiFi.status() == WL_CONNECTED)
  {
    if (otaActive)
    {
      asyncWebServer.end();
//      M5.Lcd.println("OTA Disabled");
      otaActive = false;
    }

    WiFi.disconnect();
    ssid_connected = ssid_not_connected;
    // M5.Lcd.printf("Wifi Disabled");
    delay (2000);
  }
  else
  {
    // M5.Lcd.printf("Wifi Connecting");

    const bool wifiOnly = true;
    const int scanAttempts = 3;
    connectToWiFiAndInitOTA(wifiOnly,scanAttempts);
 
    // M5.Lcd.fillScreen(TFT_ORANGE);
    // M5.Lcd.setCursor(10, 10);
    // M5.Lcd.setTextSize(3);
    // M5.Lcd.setRotation(1);
    // M5.Lcd.setTextColor(TFT_WHITE, TFT_BLUE);

    // M5.Lcd.printf(WiFi.status() == WL_CONNECTED ? "Wifi Enabled" : "No Connect");
    
    // M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    delay(2000);
  }
  
  // M5.Lcd.fillScreen(TFT_BLACK);
}

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

const char* scanForKnownNetwork() // return first known network found
{
  const char* network = nullptr;

//  M5.Lcd.println("Scan WiFi\nSSIDs...");
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
      // M5.Lcd.printf("Found:\n%s",network);

    USB_SERIAL_PRINTF("Found:\n%s\n",network);
  }
  else
  {
    // M5.Lcd.println("None\nFound");
    USB_SERIAL_PRINTLN("No networks Found\n");
  }

  // clean up ram
  WiFi.scanDelete();

  return network;
}

bool connectToWiFiAndInitOTA(const bool wifiOnly, int repeatScanAttempts)
{
  if (wifiOnly && WiFi.status() == WL_CONNECTED)
    return true;

  // M5.Lcd.setCursor(0, 0);
  // M5.Lcd.fillScreen(TFT_BLACK);
  // M5.Lcd.setTextSize(2);

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

char* getMQTTPayloadBuffer()
{
  return privateMQTT.getPayloadBuffer();
}

void webSerialReceiveMessage(uint8_t *data, size_t len){
  WebSerial.println("Received Data...");
  String d = "";
  for(int i=0; i < len; i++){
    d += char(data[i]);
  }

  WebSerial.println(d);

  if (d == "ON"){
    statusLEDOn();
  }
  else if (d=="OFF"){
    statusLEDOff();
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
    USB_SERIAL_PRINTF("setupOTAWebServer: attempt to connect wifiOnly, already connected - otaActive=%i\n",otaActive);

    return true;
  }

  USB_SERIAL_PRINTF("setupOTAWebServer: attempt to connect %s wifiOnly=%i when otaActive=%i\n",_ssid, wifiOnly,otaActive);

  bool forcedCancellation = false;

  // M5.Lcd.setCursor(0, 0);
  // M5.Lcd.fillScreen(TFT_BLACK);
  // M5.Lcd.setTextSize(2);
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
  // M5.Lcd.printf("%s Wifi", label);
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

    // M5.Lcd.print(".");
    delay(500);
  }
  // M5.Lcd.print("\n\n");

  if (WiFi.status() == WL_CONNECTED )
  {
    if (wifiOnly == false && !otaActive)
    {
      USB_SERIAL_PRINTLN("setupOTAWebServer: WiFi connected ok, starting up OTA");

      USB_SERIAL_PRINTLN("setupOTAWebServer: calling asyncWebServer.on");

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

      USB_SERIAL_PRINTLN("setupOTAWebServer: calling AsyncElegantOTA.begin");

      AsyncElegantOTA.setID(MERCATOR_OTA_DEVICE_LABEL);
      AsyncElegantOTA.setUploadBeginCallback(uploadOTABeginCallback);
      AsyncElegantOTA.setUploadSucceededCallback(uploadOTASucceededCallback);
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

      USB_SERIAL_PRINTLN("setupOTAWebServer: calling asyncWebServer.begin");

      asyncWebServer.begin();

      dumpHeapUsage("setupOTAWebServer(): after asyncWebServer.begin");

      USB_SERIAL_PRINTLN("setupOTAWebServer: OTA setup complete");

      // M5.Lcd.setRotation(0);
      
      // M5.Lcd.fillScreen(TFT_BLACK);
      // M5.Lcd.setCursor(0,155);
      // M5.Lcd.setTextSize(2);
      // M5.Lcd.printf("%s\n\n",WiFi.localIP().toString());
      // M5.Lcd.println(WiFi.macAddress());
      // connected = true;
      otaActive = true;
    
      delay(2000);

      connected = true;
  
      updateButtonsAndBuzzer();
      }
  }
  else
  {
    if (forcedCancellation)
    {
      // M5.Lcd.print("\nCancelled\nConnect\nAttempts");
    }
    else
    {
      USB_SERIAL_PRINTF("setupOTAWebServer: WiFi failed to connect %s\n",_ssid);

      // M5.Lcd.print("No Connect");
    }
  }

  // M5.Lcd.fillScreen(TFT_BLACK);

  return connected;
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


#define BUILD_INCLUDE_MAIN_PART2

#include "main_part2.cpp"
