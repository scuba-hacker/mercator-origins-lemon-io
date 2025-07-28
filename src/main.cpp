#include <Arduino.h>

// rename the git file "mercator_secrets_template.c" to the filename below, filling in your wifi credentials etc.
#include "mercator_secrets.c"

#include <UMS3.h>
UMS3 ProS3;

#include <U8g2lib.h>
#include "OLEDDisplayManager.h"
#include "SerialConfig.h"
#include "NetworkManager.h"

// External HTML content declarations
extern const uint8_t STATS_HTML[];
extern const uint32_t STATS_HTML_SIZE;
extern const uint8_t MAP_HTML[];
extern const uint32_t MAP_HTML_SIZE;

#define OLED_VCC_3V3_RED            "ABOVE DIN_MOSI_BLUE"
#define OLED_GND_BLACK              "BELOW BATTERY PIN (TOP RIGHT NEXT TO USB-C)"
#define OLED_RST_BROWN              2
#define OLED_DC_PURPLE              1
#define OLED_CS_ORANGE              34  // Standard Arduino Hardware SPI Chip-Select Pro S3
#define OLED_CLK_SCL_YELLOW         36  // Standard Arduino Hardware SPI CLK for Pro S3
#define OLED_DIN_MOSI_SDA_BLUE      35  // Standard Arduino Hardware SPI MOSI for Pro S3
U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI u8g2(U8G2_R0, OLED_CS_ORANGE, OLED_DC_PURPLE, OLED_RST_BROWN);

// Create display manager instance (256px wide, 4 lines max)
OLEDDisplayManager displayManager(u8g2, 256, 4);

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

// ****** Webserver functionality moved to NetworkManager class ****** 

// Override ElegantOTA web page to have the Lemon banner graphic and Lemon-IO device label
#define MERCATOR_ELEGANTOTA_LEMON_BANNER
#define MERCATOR_OTA_DEVICE_LABEL "LEMON-IO" 

// make sure this is disabled if writeLogToSerial is false
//#define USE_WEBSERIAL

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

const uint8_t GPS_TX_GPIO = 39;
const uint8_t GPS_RX_GPIO = 38;

const uint8_t MAKO_GOPRO_TX_GPIO = 43;    // should be called mako gopro GPIO
const uint8_t MAKO_GOPRO_RX_GPIO = 44;    // should be called mako gopro GPIO

const uint8_t TX_TO_NEOPIXELS_GPIO = 40;
const uint8_t RX_TO_NEOPIXELS_GPIO = 41;

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

// ################## START NETWORK MANAGER Configuration
NetworkConfig networkConfig = {
    ssid_1, password_1, label_1, timeout_1,
    ssid_2, password_2, label_2, timeout_2, 
    ssid_3, password_3, label_3, timeout_3,
    ping_target,
    "lemon",  // device hostname
    enableOTAServer,
#ifdef USE_WEBSERIAL
    true,     // enableWebSerial - controlled by USE_WEBSERIAL define
#else
    false,    // enableWebSerial - disabled when USE_WEBSERIAL not defined
#endif
    publishMQTTTestMessages,
    STATS_HTML, STATS_HTML_SIZE,
    MAP_HTML, MAP_HTML_SIZE,
    MERCATOR_OTA_DEVICE_LABEL
};


NetworkManager networkManager(networkConfig, displayManager, privateMQTT);
// ################## END NETWORK MANAGER Configuration

// Json document for sending statistics to web page (used by getStats() in main_part2.cpp)
JsonDocument readings;

// Variables needed by getStats() function in main_part2.cpp
int32_t lastCheckForInternetConnectivityAt = 0;
uint32_t privateMQTTUploadCount = 0;


// #### START IN-MEMORY TELEMETRY-PIPELINE / MESSAGE BUFFER CONFIG
TelemetryPipeline telemetryPipeline;

const uint32_t telemetry_online_head_commit_duty_ms = 1900;
const uint32_t telemetry_offline_head_commit_duty_ms = 10000;
uint32_t last_head_committed_at = 0;
bool g_offlineStorageThrottleApplied = false;
// #### END IN-MEMORY TELEMETRY-PIPELINE / MESSAGE BUFFER CONFIG


// Network configuration now handled by NetworkManager class


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

// OTA and web server now handled by NetworkManager class

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

String getStats();


// Display functionality now handled by OLEDDisplayManager class

// Progress animation methods moved to OLEDDisplayManager class

// All display functions moved to OLEDDisplayManager class

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
int32_t checkInternetConnectivityDutyCycle = 10000; // 30 seconds between each check

const uint16_t pipelineBackedUpLength = 10;

const uint8_t LEAK_DETECTOR_GPIO = 7;

Button* p_primaryButton = nullptr;
void updateButtonsAndBuzzer();

// HTML content moved to after NetworkManager include

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

struct MakoUplinkTelemetryForJson;

struct LemonTelemetryForStorage;

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

bool setupOTAWebServer(const char* _ssid, const char* _password, const char* label, uint32_t timeout, bool wifiOnly);
void buildUplinkTelemetryMessageV6a(char* payload, const struct MakoUplinkTelemetryForJson& m, const struct LemonTelemetryForJson& l);
void buildBasicTelemetryMessage(char* payload);
enum e_q_upload_status uploadTelemetryToPrivateMQTT(MakoUplinkTelemetryForJson* makoTelemetry, struct LemonTelemetryForJson* lemonTelemetry);

// WebSocket functionality moved to NetworkManager class

void getM5ImuSensorData(struct LemonTelemetryForJson& t)
{
  const float uninitialisedIMU = 0.0;  
  t.imu_lin_acc_x = t.imu_lin_acc_y = t.imu_lin_acc_z = uninitialisedIMU;
  t.imu_rot_acc_x = t.imu_rot_acc_y = t.imu_rot_acc_z = uninitialisedIMU;
}

// WiFi event handlers moved to NetworkManager class

bool devNetworkInUse()
{ 
  extern const char* private_local_gateway;
  extern const char* private_dev_ssid;
  String currentGateway = WiFi.gatewayIP().toString();
  String currentSSID = WiFi.SSID();
  return (currentGateway == String(private_local_gateway) && currentSSID == String(private_dev_ssid));
}

// Connectivity checking functions moved to NetworkManager class

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

// OTA callback functions moved to NetworkManager class

uint32_t getSizeOfLemonTelemetryForStorage();
class mqttConnectionTest
{
  public:
    static const uint32_t periodMQTTConnectCheck = 1000;
    static const int maxMQTTConnectChecks = 10;
 
  public:
    bool initialTestPublishDone;
    uint32_t nextMQTTConnectCheck;
    int connectMQTTChecksDone;

    mqttConnectionTest() : nextMQTTConnectCheck(0), connectMQTTChecksDone(0), initialTestPublishDone(false)
    {

    }
    
    void resetCheckTrigger(const uint32_t period=periodMQTTConnectCheck)
    {
      nextMQTTConnectCheck = millis() + period;
    }
};

mqttConnectionTest mqttCheck;

void setup()
{
  Serial.begin(115200);
  Serial.flush();
  delay(500);

  ProS3.begin();
  USB_SERIAL_PRINTF("=== MAIN SETUP START ===\n");
  statusLEDColour();
  statusLEDOn();

  u8g2.begin();
  
  // Display startup status
  u8g2.setFont(u8g2_font_ncenB08_tr);
  displayManager.addDisplayLine("Lemon-IO Starting...");

  privateMQTT.setConnectionCallbacks(
    [&] { USB_SERIAL_PRINTF("Local MQTT connected (%s)\n", privateMQTT.getEncryptionStatus()); },
    [&] { USB_SERIAL_PRINTF("Local MQTT disconnected (%s)\n", privateMQTT.getEncryptionStatus()); },
    [&] { USB_SERIAL_PRINTF("Remote MQTT connected (%s)\n", privateMQTT.getEncryptionStatus()); },
    [&] { USB_SERIAL_PRINTF("Remote MQTT disconnected (%s)\n", privateMQTT.getEncryptionStatus()); }
  );

  mainTaskCoreId = xPortGetCoreID();
  mainTaskHandle = xTaskGetCurrentTaskHandle();

  USB_SERIAL_PRINTLN("Unexpected Maker Pro S3 Initialised...");

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    displayManager.addDisplayLine("SPIFFS Mount Failed");
  } else {
    Serial.println("SPIFFS mounted OK");
    displayManager.addDisplayLine("SPIFFS OK");
  }

  // Initialize NetworkManager
  networkManager.setTelemetryPipeline(&telemetryPipeline);
  networkManager.setGetStatsCallback([]() { return getStats(); });
  networkManager.setSendLemonStatusCallback([](const char* status) { 
    // Note: This would need proper enum conversion
    // sendLemonStatus(status); 
  });
  networkManager.setUpdateButtonsAndBuzzerCallback([]() { updateButtonsAndBuzzer(); });
  networkManager.setIsDevNetworkCallback([]() { return devNetworkInUse(); });
  networkManager.begin();

  USB_SERIAL_PRINTF("sizeof LemonTelemetry: %lu\n",getSizeOfLemonTelemetryForStorage());

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
  
  // Update status display
  displayManager.addDisplayLine("Telemetry Pipeline OK");

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
    USB_SERIAL_PRINTF("=== MAIN: Starting WiFi/OTA initialization (enableOTAServer=%i) ===\n", enableOTAServer);
    sendLemonStatus(LC_SEARCH_WIFI);

    bool wifiOnly = false;
    int repeatScanAttempts = 4;
    USB_SERIAL_PRINTF("=== MAIN: Calling connectToWiFiAndInitOTA with wifiOnly=%i, repeatScanAttempts=%i ===\n", wifiOnly, repeatScanAttempts);
    bool connected = networkManager.connectToWiFiAndInitOTA(wifiOnly, repeatScanAttempts);
    sendLemonStatus(connected ? LC_FOUND_WIFI : LC_NO_WIFI);

    // WiFi connection result already added by connectToWiFiAndInitOTA
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

  displayManager.addDisplayLine("GPS Ready");
  delay(500);

  // cannot use Pin 0 for receive of GPS (resets on startup), can use Pin 36, can use 26
  // cannot use Pin 0 for transmit of GPS (resets on startup), only Pin 26 can be used for transmit.

  if (enableUploadToPrivateMQTT)
  {
    privateMQTT.begin();
    displayManager.addDisplayLine("MQTT Ready");
    delay(500);
  }
  
  // Final setup completion status - add to scrolling display
  displayManager.addDisplayLine("Lemon-IO Online @ " + networkManager.getLocalIP());
  delay(2000);  // Show final status for 2 seconds

  networkManager.getMQTTConnectionTest().resetCheckTrigger(1500);
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

uint32_t mainBackColour = TFT_BLACK;

uint32_t timeOfNextLemonStatus = 0;
const uint32_t lemonStatusDutyCycle = 1000;

uint32_t timeOfNextTelegramBotUpdateSendMsg = 0;
const uint32_t telegramBotDutyCycle = 10000;

const int initNeopixelSerialByteRead = -1;
int neopixelSerialByteRead = initNeopixelSerialByteRead;

// MQTT test message function moved to NetworkManager class

void loop()
{
  // Handle NetworkManager processing (includes MQTT testing, OTA restart, etc.)
  networkManager.loop();
  
  // Check if we should halt processing during OTA upload
  if (networkManager.isHaltingForOTA())
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

  if (!accumulateMissedMessageCount && millis() > delayBeforeCountingMissedMessages)
    accumulateMissedMessageCount = true;
  
  if (p_primaryButton->wasReleasefor(100)) // disable message upload
  {
    updateButtonsAndBuzzer();

    // Note: disableFeaturesForOTA is now handled by NetworkManager
    return;
  }

  // WebSocket stats updates - independent of GPS processing
  if (networkManager.getWebSocketClientCount() && millis() > networkManager.getTimeOfNextStatUpdate())
  {
    networkManager.sendStatsWebSocketNotification();
    networkManager.setTimeOfNextStatUpdate(millis() + 990); // timeBetweenSendingStatsUpdates
    dumpHeapUsage("Sent stats: ");
  }

  while (enableGPSRead && gps_serial.available() > 0)
  {
    checkForLeak(leakAlarmMsg);

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
        MAKO_GOPRO_SERIAL.write(customiseNMEASentence(gps.getSentence(), networkManager.getShowOnMapRequestIndex()));
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
          USB_SERIAL_PRINTLN("NO GPS FIX - BYTES BEING RECEIVED");
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

    USB_SERIAL_PRINTLN("NO GPS FIX - BYTES BEING RECEIVED");

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

    USB_SERIAL_PRINTLN("NO GPS - NO BYTES RECEIVED FROM GPS FROM STARTUP");

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
/*
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
  const int progressStep = 100;
  int count = timeout / progressStep;
  while (WiFi.status() != WL_CONNECTED && --count > 0)
  {
    // check for cancellation button - top button.
    updateButtonsAndBuzzer();

    if (p_primaryButton->isPressed()) // cancel connection attempts
    {
      forcedCancellation = true;
      break;
    }

    // Update progress animation
    displayManager.updateProgressAnimation();

    delay(progressStep);
  }

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
*/

#define BUILD_INCLUDE_MAIN_PART2

#include "main_part2.cpp"
