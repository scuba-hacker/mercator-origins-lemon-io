////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool writeLogToSerial = true;
bool writeTelemetryLogToSerial = false; // writeLogToSerial must also be true if this is set to true
bool writeMakoMsgDecodingLogToSerial = false; // writeLogToSerial must also be true if this is set to true

// set USE_WEB_SERIAL in SerialConfig.h if required
#include "SerialConfig.h"

// DEBUG: Set to true to simulate GPS NO FIX for testing Mako timeout system
bool forceGPSMissingGGARMCForTesting = false;
bool overrideGPSToNoFixForTesting = false;
bool sendOneReEnableFixCommand = false;
bool sendOneCeaseFixCommand = false;
bool fastStartup = true;

bool enableWebSerialFrame=false;    // stats page iframe - use /logs instead

/**
 * MARINE FLASH PERSISTENCE CONTROL
 * 
 * Uncomment to use Flash persistence instead of PSRAM telemetry pipeline
 * 
 * Marine Operational Modes:
 * - PSRAM Mode (commented): Battle-tested for reliability
 * - Flash Mode (uncommented): Extended 8+ hour marine logging capability
 * 
 * Flash mode enables:
 * - Data persistence across power cycles
 * - Extended dive logging (8+ hours without connectivity)
 * - Automatic power-loss recovery
 * - Marine-grade data integrity protection
 */
// #define USE_FLASH_TELEMETRY
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <Arduino.h>

// rename the git file "mercator_secrets_template.c" to the filename below, filling in your wifi credentials etc.
#include "mercator_secrets.c"

#include <UMS3.h>
UMS3 ProS3;

#include <U8g2lib.h>

#include <Adafruit_SSD1327.h>
#include "LGFX_Adafruit_SSD1327.h"

#include "driver/uart.h"

#include "OLEDDisplayManager.h"
#include "OLEDGSDisplayManager.h"
#include "OLEDLXDisplayManager.h"

#include "NetworkManager.h"
#include <Preferences.h>

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

#define OLED_CS_ADA_WHITE          "XX" // undefined currently
#define OLED_RST_ADA_BROWN          37  // May not be needed - can also use 0 Strapping Pin - we know nothing will pull low at boot so ok. Could also share with SPI reset line for wide oled.
U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI wideOLEDDisplay(U8G2_R0, OLED_CS_ORANGE, OLED_DC_PURPLE, OLED_RST_BROWN);

#define RANDOM_NUMBER_ADC_GPIO_13 A12       // no connection required - floating

// ################### START UART SERIAL CONFIGURATION ############################
#define UART_NUMBER_LANTERN_NEOPIXELS  0
#define LANTERN_NEOPIXELS_ARDUINO_BAUD_RATE 9600
#define LANTERN_NEOPIXELS_TX_YELLOW_GPIO 40                             // Black wire connected to Arduino RX Pin
#define LANTERN_NEOPIXELS_RX_ORANGE_GPIO 41                             // RED wire connected to Arduino TX Pin
HardwareSerial serial_lantern_neopixels(UART_NUMBER_LANTERN_NEOPIXELS);

#define UART_NUMBER_GPS    1
#define GPS_BAUD_RATE      9600
#define GPS_TX_GREY_GPIO   39
#define GPS_RX_WHITE_GPIO  38

static constexpr int GPS_RX_BUFFER_SIZE = 1024;
static constexpr size_t GPS_RX_READ_CHUNK = 256;
static constexpr int GPS_QUEUE_SIZE = 10;
static constexpr TickType_t GPS_RX_TIMEOUT = pdMS_TO_TICKS(100);

QueueHandle_t gpsQueue = nullptr;

// Data structure to send via queue
struct GPSDataPacket
{
  uint8_t data[GPS_RX_READ_CHUNK];
  int length;
};

HardwareSerial serial_gps(UART_NUMBER_GPS);

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
#define UART_NUMBER_MAKO_GOPRO   2
#define MAKO_UPLINK_BAUD_RATE    57600    // max working test so far: 2,100,000
#define MAKO_GOPRO_TX_BLUE_GPIO  43       // marked TX on board
#define MAKO_GOPRO_RX_GREEN_GPIO 44       // marked RX on board

static constexpr int MAKO_RX_BUFFER_SIZE = 1024;
static constexpr size_t MAKO_RX_READ_CHUNK = 256;
static constexpr int MAKO_QUEUE_SIZE = 10;            
static constexpr TickType_t MAKO_RX_TIMEOUT = pdMS_TO_TICKS(100);

QueueHandle_t makoQueue = nullptr;
struct MakoDataPacket
{
    uint8_t data[MAKO_RX_READ_CHUNK];
    int length;
    uint32_t timestamp;  // Add timestamp for latency tracking
};

HardwareSerial serial_mako_gopro(UART_NUMBER_MAKO_GOPRO);

// ################### END UART SERIAL CONFIGURATION ############################

// hardware SPI
//Adafruit_SSD1327 display(128, 128, &SPI, OLED_DC_PURPLE, OLED_RST_ADA_BROWN, OLED_CS_ADA_WHITE);

// I2C - check default pins for ProS3 are matching the I2C connector on top of board
// SDA = 8, SCL = 9, resetpin = OLED_RST_ADA_BROWN, preclk = 1000000, postclk = 100000
Adafruit_SSD1327 adafruitDisplay(128, 128, &Wire, OLED_RST_ADA_BROWN, 1000000);
LGFX_I2C_Adafruit_SSD1327_128x128_Grey_OLE lgfxAdafruitDisplay;

// Create display manager instance (256px wide, 4 lines max)
OLEDWideDisplayManager   wideDisplayManager(wideOLEDDisplay, 256, 4);
OLEDGSDisplayManager GSdisplayManager(adafruitDisplay);
OLEDLXDisplayManager LXdisplayManager(lgfxAdafruitDisplay);

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
#include "FlashTelemetryManager.h"
#define PICOMQTT_MAX_MESSAGE_SIZE 4096
#include "MercatorMQTT.h"

#include "root_ca.h"
extern const char* isrg_root_ca;

#include <WiFi.h>
#include <ESP32Ping.h>

#define DEBOUNCE_MS 10
#define RED_BUTTON_GPIO 42    // Not currently used
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

bool haltAllProcessingDuringOTAUpload = false;

// START FEATURE ENABLE FLAGS
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

bool readTempHumidityCJMCU_1080_Sensor(double* temperature, double* humidity);
void initializeTempHumiditySensor();


#define STATUS_LED_ON HIGH
#define STATUS_LED_OFF LOW

uint8_t statusLED = STATUS_LED_OFF;

// ################## START LANTERN NEO-PIXEL CONFIGURATION
enum e_display_brightness
{
  OFF_DISPLAY = 0,
  DIM_DISPLAY = 25,
  HALF_BRIGHT_DISPLAY = 50,
  BRIGHTEST_DISPLAY = 100
};
const e_display_brightness ScreenBrightness = BRIGHTEST_DISPLAY;

enum e_lemon_status
{
  LC_NONE = 0,
  LC_STARTUP = 1,
  LC_SEARCH_WIFI = 2,
  LC_FOUND_WIFI = 3,
  LC_NO_WIFI = 4,
  LC_NO_GPS = 5,
  LC_NO_FIX = 6,
  LC_GOOD_FIX = 7,
  LC_ALL_OFF = 8,
  LC_DIVE_IN_PROGRESS = 64,
  LC_NO_STATUS_UPDATE = 127,
  LC_NO_INTERNET = 128
};

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
    ping_name_target,
    ping_ip_target,
    "lemon",  // device hostname
    enableOTAServer,
#ifdef USE_WEBSERIAL
    true,     // enableWebSerial - controlled by USE_WEBSERIAL define
#else
    false,    // enableWebSerial - disabled when USE_WEBSERIAL not defined
#endif
    enableWebSerialFrame,
    publishMQTTTestMessages,
    STATS_HTML, STATS_HTML_SIZE,
    MAP_HTML, MAP_HTML_SIZE,
    MERCATOR_OTA_DEVICE_LABEL
};

NetworkManager networkManager(networkConfig, wideDisplayManager, privateMQTT);
// ################## END NETWORK MANAGER Configuration

// Json document for sending statistics to web page (used by getStats() in main_part2.cpp)
JsonDocument readings;

// Variables needed by getStats() function in main_part2.cpp
int32_t lastCheckForInternetConnectivityAt = 0;
uint32_t privateMQTTUploadCount = 0;

// System configuration and testing variables
Preferences testingPrefs;
bool wifiTestingBlocked = false;

// #### START TELEMETRY STORAGE SYSTEM SELECTION ####
/**
 * Marine Telemetry Storage System Selection
 * 
 * This conditional compilation provides seamless switching between:
 * 
 * FLASH MODE (USE_FLASH_TELEMETRY defined):
 * - FlashTelemetryManager: Marine-grade persistent storage
 * - 10MB flash ring buffer survives power cycles
 * - 8+ hour dive logging capability without connectivity
 * - Power-safe atomic operations
 * - Automatic fallback to PSRAM if flash fails
 * 
 * PSRAM MODE (USE_FLASH_TELEMETRY not defined):
 * - TelemetryPipeline: Battle-tested PSRAM-only storage
 * - Proven reliability in marine environments
 * - Limited to active power session duration
 * - Immediate fallback for flash system issues
 * 
 * KEY: Both systems share identical API - existing code unchanged
 */
#ifdef USE_FLASH_TELEMETRY
FlashTelemetryManager telemetryPipeline;    // Flash-based persistent pipeline with PSRAM fallback
#else
TelemetryPipeline telemetryPipeline;        // Original PSRAM-based pipeline (battle-tested fallback)
#endif

const uint32_t telemetry_online_head_commit_duty_ms = 1000;
const uint32_t telemetry_offline_head_commit_duty_ms = telemetry_online_head_commit_duty_ms;
uint32_t last_head_committed_at = 0;
bool g_offlineStorageThrottleApplied = false;
// #### END IN-MEMORY TELEMETRY-PIPELINE / MESSAGE BUFFER CONFIG

const uint32_t maxTimeBeforeAlertNoFix = 3000;
const uint32_t maxTimeBeforeAlertNoGPSByte = 2000;
uint32_t timeNextGoodFixExpectedBy = 0;
uint32_t timeNextGPSByteExpectedBy = 0;
bool hasGPSFix = false;

enum e_user_action
{
  NO_USER_ACTION = 0x0000,
  HIGHLIGHT_USER_ACTION = 0x0001,
  RECORD_BREADCRUMB_TRAIL_USER_ACTION = 0x0002,
  LEAK_DETECTED_USER_ACTION = 0x0004
};

// Mask with 0x01 to see if successful
enum e_q_upload_status
{
  Q_SUCCESS = 1,
  Q_SUCCESS_SEND = 3,
  Q_SUCCESS_NO_SEND = 5,
  Q_SUCCESS_NOT_ENABLED = 7,
  Q_NO_WIFI_CONNECTION = 8,
  Q_SERVER_CONNECT_ERROR = 10,
  Q_MQTT_CLIENT_CONNECT_ERROR = 12,
  Q_MQTT_CLIENT_SEND_ERROR = 14,
  Q_UNDEFINED_ERROR = 254
};

uint32_t fixCount = 0;  // Count of GGA messages with valid FIX
uint32_t noFixCount = 0;  // Count of GGA messages with NO FIX
uint32_t passedChecksumCount = 0;
bool processUplinkMessage = true;

// Cached GPS fix status specifically for telemetry - updated only when GPS messages are processed
bool gpsFixStatusForTelemetry = false;

// GPS status tracking for comprehensive display
uint32_t gpsMessagesReceived = 0;
uint32_t gpsFailedChecksumCount = 0;
uint32_t gpsBadLengthCount = 0;
uint32_t lastGPSMessageTime = 0;
bool hasGPSDevice = true;  // Assume GPS device present until proven otherwise

TinyGPSPlus gps;

bool diveInProgress = false;

String getStats();

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

uint32_t startAccumulatingMissedMessagesAt = 0;
uint32_t setupCompletedAt = 0;
bool accumulateMissedMessageCount = false;    // start-up
const uint32_t delayBeforeCountingMissedMessages = 10000;
void incrementUplinkMessageMissedCount();

// was 30 - using m5 gps temporarily 
const uint32_t uplinkMessageLingerPeriodMs = 30;   // max milliseconds to wait for Mako pre-amble to reply
uint32_t uplinkLingerTimeoutAt = 0;
uint32_t downlinkSendMessageDurationMicroSeconds = 0;   // Latency processing GPS message and downlink msg send to Mako complete.
uint32_t preambleReceivedAfterMicroSeconds = 0;         // Latency between start of preamble and end of preamble received from Mako.
uint32_t uplinkRxMicroSeconds = 0;                      // Latency between end of pre-amble received and good complete message received from Mako.
uint32_t uplinkMessageListenTimer = 0;                  // Latency processing GPS message, send to mako and valid msg received from Mako.

const int8_t maxPingAttempts = 1;
int32_t checkInternetConnectivityDutyCycle = 10000; // 10 seconds between each check

const uint16_t pipelineBackedUpLength = 10;

const uint8_t LEAK_DETECTOR_GPIO = 7;       // Not currently in use

Button* p_primaryButton = nullptr;
void updateButtonsAndBuzzer();

void toggleOTAActive();
void toggleWiFiActive();

uint8_t checkForLanternLatestReedEvent();

bool setupOTAWebServer(const char* _ssid, const char* _password, const char* label, uint32_t timeout, bool wifiOnly);

void updateButtonsAndBuzzer()
{
  p_primaryButton->read();
}
void sendLemonStatus(const e_lemon_status status);

bool nmea_get_field(const char *s, int index, char *out, size_t outsz);

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
  bool      isFix;
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
bool checkForValidPreambleInReceiveBuffer(MakoDataPacket& makoPacket, int& preambleStart);
bool checkForValidPreambleOnUplink();
bool populateHeadWithMakoTelemetry(BlockHeader& headBlock, const bool validPreambleFound, const uint8_t* packetData = nullptr, int dataLength = 0);
void populateHeadWithLemonTelemetryAndCommit(BlockHeader& headBlock);
void getNextTelemetryMessagesUploadedToPrivateMQTT();
void populateCurrentLemonTelemetry(LemonTelemetryForJson& l, TinyGPSPlus& g);
void populateFinalLemonTelemetry(LemonTelemetryForJson& l);
void constructLemonTelemetryForStorage(struct LemonTelemetryForStorage& s, const LemonTelemetryForJson l, const uint16_t uplinkMessageLength);

// Serial command processing
void processSerialCommands();
void processSerialCommand(char command);
void processExtendedCommand(const String& command);
void initializeTelemetrySystem();
void loadTestingPreferences();
void saveTestingPreferences();
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
bool configureUBLOXGps(); 
void sendCeaseFixMessagesNMEAMessage(bool cease, const char* context);
void toggleOTAActive();
void toggleWiFiActive();

bool setupOTAWebServer(const char* _ssid, const char* _password, const char* label, uint32_t timeout, bool wifiOnly);
void buildUplinkTelemetryMessageV6a(char* payload, const struct MakoUplinkTelemetryForJson& m, const struct LemonTelemetryForJson& l);
void buildBasicTelemetryMessage(char* payload);
enum e_q_upload_status uploadTelemetryToPrivateMQTT(MakoUplinkTelemetryForJson* makoTelemetry, struct LemonTelemetryForJson* lemonTelemetry);

void getM5ImuSensorData(struct LemonTelemetryForJson& t)
{
  const float uninitialisedIMU = 0.0;  
  t.imu_lin_acc_x = t.imu_lin_acc_y = t.imu_lin_acc_z = uninitialisedIMU;
  t.imu_rot_acc_x = t.imu_rot_acc_y = t.imu_rot_acc_z = uninitialisedIMU;
}

bool devNetworkInUse()
{ 
  extern const char* private_local_gateway;
  extern const char* private_dev_ssid;
  String currentGateway = WiFi.gatewayIP().toString();
  String currentSSID = WiFi.SSID();
  return (currentGateway == String(private_local_gateway) && currentSSID == String(private_dev_ssid));
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
void statusLEDColourYellow() { ProS3.setPixelColor(128,128,0); }
void statusLEDColourRed() { ProS3.setPixelColor(255,0,0); }
void statusLEDColourPurple() { ProS3.setPixelColor(35,31,42); }

TaskHandle_t mainTaskHandle = nullptr;
BaseType_t mainTaskCoreId = 0;
TaskHandle_t gpsTaskHandle = nullptr;
TaskHandle_t makoTaskHandle = nullptr;

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

bool fullTestAdafruitDisplay = false;
bool singleScreenTestAdafruitDisplay = !fastStartup;
bool testLgfxAdafruitDisplay = false;

bool useGsDisplayManager = true;
bool useLxDisplayManager = false;

void gpsRxTask(void *arg)
{
  GPSDataPacket packet;
  for (;;)
  {
    if (!haltAllProcessingDuringOTAUpload)
    {
      int bytesRead = uart_read_bytes(UART_NUMBER_GPS, packet.data, sizeof(packet.data), GPS_RX_TIMEOUT);
      if (bytesRead > 0)
      {
        packet.length = bytesRead;
        // Send packet to main loop via FreeRTOS queue (don't block if queue is full)
        if (xQueueSend(gpsQueue, &packet, 0) != pdTRUE)
        {
          // Queue full - could increment a dropped packet counter here
        }
      }
    }
    else
    {
      delay(100);
    }
  }
}

void makoRxTask(void *arg)
{
  MakoDataPacket packet;
  for (;;)
  {
    if (!haltAllProcessingDuringOTAUpload)
    {
      int bytesRead = uart_read_bytes(UART_NUMBER_MAKO_GOPRO, packet.data, sizeof(packet.data), MAKO_RX_TIMEOUT);
      if (bytesRead > 0)
      {
        packet.length = bytesRead;
        packet.timestamp = millis();  // Capture receive timestamp

        // Send packet to main loop via FreeRTOS queue (don't block if queue is full)
        if (xQueueSend(makoQueue, &packet, 0) != pdTRUE)
        {
            // Queue full - increment dropped packet counter
            // Could add statistics tracking here
        }
      }
    }
    else
    {
      delay(100);
    }
  }
}

void initialiseUARTS()
{
  // Prevent UART0 interference from bootloader / panic handler
  esp_log_level_set("*", ESP_LOG_NONE);          // Disable logging - this is needed to prevent the bootloader from interfering with the UART
  esp_deep_sleep_disable_rom_logging();          // Stop ROM from using UART0
  uart_driver_delete(UART_NUM_0);                 // Force-remove any driver on UART0

  // Begin USB CDC Serial for Debug - not UART0 on an ESP32-S3 (unlike ESP32)
  Serial.begin(115200);
  Serial.flush();
  delay(500);

  // Begin UART0 for serial comms with Lantern Arduino Nano Every for Neo-Pixel Lights and Reed Relay Control
  // Must use the uart_set_pin as well on ESP32-S3. Remove it and Rx will not work.
  serial_lantern_neopixels.begin(LANTERN_NEOPIXELS_ARDUINO_BAUD_RATE, SERIAL_8N1, LANTERN_NEOPIXELS_RX_ORANGE_GPIO, LANTERN_NEOPIXELS_TX_YELLOW_GPIO);
  uart_set_pin(UART_NUM_0, LANTERN_NEOPIXELS_TX_YELLOW_GPIO, LANTERN_NEOPIXELS_RX_ORANGE_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  // UART1 for receiving data from GPS
  serial_gps.setRxBufferSize(GPS_RX_BUFFER_SIZE); // must set before begin
  serial_gps.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_WHITE_GPIO, GPS_TX_GREY_GPIO);
//  sendCeaseFixMessagesNMEAMessage(false, "setup() - clear any existing test block of FIX msg as is persistent across boots"); // ensure test block of FIX msg is off

  BUFFER_LOG_RESET();

  BUFFER_LOG_PRINTLN("###########################################################################");

  BUFFER_LOG_PRINTLN("\nSetting GPS Module to Pedestrian Dynamic Model, 1 Hz, GGA+RMC only, Enable NMEA send NO FIX msgs...");

  bool ok = configureUBLOXGps();    // Populates the BUFFER_LOG with diagnostics

  BUFFER_LOG_PRINTLN(ok ? "GPS configured OK (ACKs received)."
                    : "GPS configuration partially failed (some ACKs missed).");

  BUFFER_LOG_PRINTLN("###########################################################################");

  xTaskCreatePinnedToCore(gpsRxTask,
                          "gpsRxTask",
                          4096,    // stack size
                          nullptr, // user parameters to pass to task
                          6,       // Priority
                          &gpsTaskHandle, // task handle
                          1);      // core id

  // UART2 for sending/receiving data to/from GoPro
  serial_mako_gopro.setRxBufferSize(1024); // was 256 - must set before begin
  serial_mako_gopro.begin(MAKO_UPLINK_BAUD_RATE, SERIAL_8N2, MAKO_GOPRO_RX_GREEN_GPIO, MAKO_GOPRO_TX_BLUE_GPIO);

  // Create Mako RS485 receive task on Core 1 (opposite core from GPS)
  xTaskCreatePinnedToCore(makoRxTask,
                          "makoRxTask",
                          4096,    // stack size
                          nullptr, // user parameters to pass to task
                          7,       // Higher priority than GPS (more time-critical)
                          &makoTaskHandle, // task handle
                          1);      // core id (different from GPS task)

  // NOTES FOR UPGRADING RS485 Interface linking Mako <--> Lemon
  // If using a MAX485 board which exposes driver enable control DE / RE then can use this mode which would be better than now
  // would prevent bytes echoing back and having to chuck out trash bytes. It uses an RTS pin.

  // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/uart.html
  // uart_set_pin(UART_NUM_1, txPin, rxPin, rtsPin, UART_PIN_NO_CHANGE);
  // uart_set_mode(UART_NUM_1, UART_MODE_RS485_HALF_DUPLEX);

  // https://www.keyestudio.com/products/max485-module-rs-485-module-ttl-rs-485-module-for-arduino
}

void testTightSerialRxLoop()
{
  // Prevent UART0 interference from bootloader / panic handler
  esp_log_level_set("*", ESP_LOG_NONE);          // Disable logging - this is needed to prevent the bootloader from interfering with the UART
  esp_deep_sleep_disable_rom_logging();          // Stop ROM from using UART0
  uart_driver_delete(UART_NUM_0);                 // Force-remove any driver on UART0

  Serial.begin(115200);         // This is USB CDC Serial for Debug - not UART0
  Serial.flush();
  delay(500);

  serial_lantern_neopixels.begin(LANTERN_NEOPIXELS_ARDUINO_BAUD_RATE, SERIAL_8N1, LANTERN_NEOPIXELS_RX_ORANGE_GPIO, LANTERN_NEOPIXELS_TX_YELLOW_GPIO);
  uart_set_pin(UART_NUM_0, LANTERN_NEOPIXELS_TX_YELLOW_GPIO, LANTERN_NEOPIXELS_RX_ORANGE_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  
  USB_SERIAL_PRINTF("UART1 configured: RX=GPIO%d, TX=GPIO%d, Baud=%d\n",
                    LANTERN_NEOPIXELS_RX_ORANGE_GPIO, LANTERN_NEOPIXELS_TX_YELLOW_GPIO, LANTERN_NEOPIXELS_ARDUINO_BAUD_RATE);
  uint32_t nextTestMsg = 500;
  while(1)
  {
    if (serial_lantern_neopixels.available())
      USB_SERIAL_PRINTLN(serial_lantern_neopixels.read());

    if (millis() > nextTestMsg)
    {
      USB_SERIAL_PRINTLN("Test");
      nextTestMsg = millis() + 500;
    }
  }
}

uint32_t timeOfNextLemonStatus = 0;
const uint32_t lemonStatusDutyCycle = 500;

uint32_t timeOfNextTelegramBotUpdateSendMsg = 0;
const uint32_t telegramBotDutyCycle = 10000;

const int initNeopixelSerialByteRead = -1;
int neopixelSerialByteRead = initNeopixelSerialByteRead;

uint8_t latestLanternReedState = 0;

void prepareSystemForOTA()
{
  haltAllProcessingDuringOTAUpload = true;

  // Disable processing flags first
  enableConnectToPrivateMQTT = false;
  enableUploadToPrivateMQTT = false;
  privateMQTT.setEnabled(false, false);
  enableReadUplinkComms = false;
  processUplinkMessage = false;
  enableAllUplinkMessageIntegrityChecks = false;
  enableGPSRead = false;
  writeLogToSerial = false;
  writeTelemetryLogToSerial = false;

  // Delete UART tasks to prevent interference with OTA
  if (gpsTaskHandle != nullptr) {
    vTaskDelete(gpsTaskHandle);
    gpsTaskHandle = nullptr;
  }
  
  if (makoTaskHandle != nullptr) {
    vTaskDelete(makoTaskHandle);
    makoTaskHandle = nullptr;
  }
  
  // Clean up queues
  if (gpsQueue != nullptr) {
    vQueueDelete(gpsQueue);
    gpsQueue = nullptr;
  }
  
  if (makoQueue != nullptr) {
    vQueueDelete(makoQueue);
    makoQueue = nullptr;
  }

  // Small delay to ensure cleanup is complete
  delay(100);

  // End serial communications
  serial_gps.end();
  serial_mako_gopro.end();
  serial_lantern_neopixels.end();
  
  statusLEDColourRed();

  telemetryPipeline.teardown();
}

void sendLemonStatus(const e_lemon_status status)
{
  if (diveInProgress)
    serial_lantern_neopixels.write(status | LC_DIVE_IN_PROGRESS);
  else
    serial_lantern_neopixels.write(status);
}

void setup()
{
  randomSeed(analogRead(RANDOM_NUMBER_ADC_GPIO_13));  // Use a floating analog pin for entropy - for OLED screen saver random movements

  ProS3.begin();
  USB_SERIAL_PRINTF("=== MAIN SETUP START ===\n");
  statusLEDColourPurple();
  statusLEDOn();

  initialiseUARTS();

  statusLEDColourYellow();

  // Initialize GPS queue - 10 packets deep should be sufficient
  gpsQueue = xQueueCreate(GPS_QUEUE_SIZE, sizeof(GPSDataPacket));
  if (gpsQueue == nullptr)
  {
    USB_SERIAL_PRINTLN("Failed to create GPS queue!");
    // Handle error appropriately
  }
  else
  {
    USB_SERIAL_PRINTLN("GPS queue created successfully");
  }

  // Initialize Mako RS485 queue
  makoQueue = xQueueCreate(MAKO_QUEUE_SIZE, sizeof(MakoDataPacket));
  if (makoQueue == nullptr) 
  {
    USB_SERIAL_PRINTLN("Failed to create Mako RS485 queue!");
  } 
  else 
  {
    USB_SERIAL_PRINTLN("Mako RS485 queue created successfully");
  }

  if (useLxDisplayManager)
  {
    LXdisplayManager.begin();
    USB_SERIAL_PRINTLN("=== ADAFRUIT GREYSCALE OLED STARTED - LoyvanGFX Driver ===");
    useGsDisplayManager = fullTestAdafruitDisplay = singleScreenTestAdafruitDisplay = false;
  }
  else if (useGsDisplayManager)
  {
    useLxDisplayManager = testLgfxAdafruitDisplay = false;
    if (adafruitDisplay.begin(0x3D)) 
      USB_SERIAL_PRINTLN("=== ADAFRUIT GREYSCALE OLED STARTED - Adafruit Driver ===");
    else
      USB_SERIAL_PRINTLN("Unable to initialize Adafruit Greyscale OLED - Adafruit Driver");
  }

  if (testLgfxAdafruitDisplay)
    LXdisplayManager.rotatedGrayBarTest();
  else if (singleScreenTestAdafruitDisplay)
    GSdisplayManager.drawAFewSnowflakes();
  else if (fullTestAdafruitDisplay)
    GSdisplayManager.fullDisplayTest();     // blocking 

  // Display startup status
  wideOLEDDisplay.begin();
  wideOLEDDisplay.setFont(u8g2_font_ncenB08_tr);
  wideDisplayManager.addDisplayLine("Lemon-IO Starting...");

  initializeTempHumiditySensor();

  privateMQTT.setConnectionCallbacks(
    [&] { USB_SERIAL_PRINTF("Local MQTT connected (%s)\n", privateMQTT.getEncryptionStatus()); },
    [&] { USB_SERIAL_PRINTF("Local MQTT disconnected (%s)\n", privateMQTT.getEncryptionStatus()); },
    [&] { USB_SERIAL_PRINTF("Remote MQTT connected (%s)\n", privateMQTT.getEncryptionStatus()); },
    [&] { USB_SERIAL_PRINTF("Remote MQTT disconnected (%s)\n", privateMQTT.getEncryptionStatus()); }
  );

  mainTaskCoreId = xPortGetCoreID();
  mainTaskHandle = xTaskGetCurrentTaskHandle();

  USB_SERIAL_PRINTLN("Unexpected Maker Pro S3 Initialised...");

  SPIFFS.begin(true);

  // Initialize NetworkManager
  networkManager.setTelemetryPipeline(&telemetryPipeline);
  networkManager.setGetStatsCallback([]() { return getStats(); });
  networkManager.setIsDevNetworkCallback([]() { return devNetworkInUse(); });
  networkManager.setPrepareEntireSystemForOTA([]() { prepareSystemForOTA(); });
  
  // Set up WebSerial command callbacks
  networkManager.setWebSerialCommandCallback([](char command) {
    processSerialCommand(command);
  });
  networkManager.setWebSerialExtendedCommandCallback([](const String& command) {
    processExtendedCommand(command);
  });
  
  networkManager.begin();

  USB_SERIAL_PRINTF("sizeof LemonTelemetry: %lu\n",getSizeOfLemonTelemetryForStorage());

  dumpHeapUsage("main: prior to Telemetry Pipeline creation  ");

  const uint16_t maxPipelineBufferKB = 2048;
  const uint16_t maxPipelineBlockPayloadSize = 256; // was 224 - Assuming 120 byte Mako Telemetry Msg and 104 byte Lemon Telemetry Msg
  BlockHeader::s_overrideMaxPayloadSize(maxPipelineBlockPayloadSize);  // 400 messages with 256 byte max payload. 
  
  /**
   * Initialize Marine Telemetry System
   * 
   * Initializes the selected telemetry storage system (Flash or PSRAM).
   * Critical for marine operation - handles:
   * - Flash system initialization and validation (if USE_FLASH_TELEMETRY defined)
   * - PSRAM system initialization (always available as fallback)
   * - Power-on self-test and automatic repair (flash mode)
   * - System health reporting and diagnostic logging
   * 
   * Marine Safety: System will fall back to PSRAM mode if flash initialization fails
   */
  initializeTelemetrySystem();

  dumpHeapUsage("main: after Telemetry Pipeline creation  ");
  
  statusLEDOff();

  serial_lantern_neopixels.begin(LANTERN_NEOPIXELS_ARDUINO_BAUD_RATE, SERIAL_8N1, LANTERN_NEOPIXELS_RX_ORANGE_GPIO, LANTERN_NEOPIXELS_TX_YELLOW_GPIO);
  USB_SERIAL_PRINTF("UART1 configured: RX=GPIO%d, TX=GPIO%d, Baud=%d\n",
                    LANTERN_NEOPIXELS_RX_ORANGE_GPIO, LANTERN_NEOPIXELS_TX_YELLOW_GPIO, LANTERN_NEOPIXELS_ARDUINO_BAUD_RATE);

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
  }

  if (enableUploadToPrivateMQTT)
    privateMQTT.begin();
  
  wideDisplayManager.addDisplayLine("Lemon-IO Online @ " + networkManager.getLocalIP());
  if (!fastStartup)
    delay(1000);

  delay(5000);    // allow time to connect to /logs page for checking GPS results

  USB_SERIAL_PRINTLN("++++++++++++++  BUFFER LOG ++++++++++++++++++");
  USB_SERIAL_PRINTLN(BUFFER_LOG_GET_BUFFER());
  BUFFER_LOG_RESET();
  USB_SERIAL_PRINTLN("++++++++++++++++++++++++++++++++++++++++++++");

  wideDisplayManager.clearDisplay();

  if (useGsDisplayManager)
    GSdisplayManager.clearDisplay();
  else if (useLxDisplayManager)
    LXdisplayManager.clearDisplay();

  // Force connectivity check to update DNS/IP status for display
  networkManager.setForceConnectivityCheckForDisplay(true);
  networkManager.getMQTTConnectionTest().resetCheckTrigger(1500);

  setupCompletedAt = millis();
  startAccumulatingMissedMessagesAt = setupCompletedAt + delayBeforeCountingMissedMessages;

  // Load testing preferences
  loadTestingPreferences();  

  USB_SERIAL_PRINTLN("Setup() completed");
}

double tempFloat=0.0;
double humidFloat=0.0;
bool newTempHumidRead=false;


const uint32_t timeoutUntilNoGPSDetected = 10000;

void loop()
{  
  // Handle NetworkManager processing (includes MQTT testing, OTA restart, etc.)
  networkManager.loop();
  
  // Cut short event loop when halting processing during OTA upload
  if (networkManager.isHaltingForOTA())
  {  
    delay(100);
    toggleStatusLED();
    return;
  }
  
  newTempHumidRead = readTempHumidityCJMCU_1080_Sensor(&tempFloat, &humidFloat);

  // Process serial commands for testing
  processSerialCommands();

  updateButtonsAndBuzzer();

  // Skip MQTT if WiFi is blocked for testing flash persistence
  if (enableUploadToPrivateMQTT && !wifiTestingBlocked)
    privateMQTT.loop();

  if (enableUploadToPrivateMQTT)
    privateMQTT.loop();

  if (publishMQTTTestMessages)
    networkManager.publishMQTTTestMessageOnDutyCycle();

  uint32_t now = millis();

  static uint32_t lastWebSocketUpdate = 0;  

  const int webSocketTick = 1000;
  if (networkManager.getWebSocketClientCount() > 0 && now > lastWebSocketUpdate + webSocketTick)
  {
    networkManager.sendStatsWebSocketNotification();
    lastWebSocketUpdate = now;
  }
  
  hasGPSDevice = (now < lastGPSMessageTime + timeoutUntilNoGPSDetected);
  hasGPSFix    = !overrideGPSToNoFixForTesting && gps.isSentenceContainingValidFix() && now < timeNextGoodFixExpectedBy;

  // *************  START CODE FOR RECEIVING GPS MESSAGE
  GPSDataPacket gpsPacket;

  if (enableGPSRead && xQueueReceive(gpsQueue, &gpsPacket, 0) == pdTRUE)
  {
    // Update GPS device detection
    lastGPSMessageTime = now;
    hasGPSDevice = true;

    timeNextGPSByteExpectedBy = now + maxTimeBeforeAlertNoGPSByte;

    for (int i = 0; i < gpsPacket.length; i++)
    {
      if (gps.encode(gpsPacket.data[i]))
      {
        // Calculate GPS fix status for all sentence types (needed for timeout reset)
        bool reallyHasFix = !overrideGPSToNoFixForTesting && gps.isSentenceContainingValidFix();

        // Count GGA messages specifically (accounting for simulation) - moved here to count ALL GGA messages
        if (gps.isSentenceGGA())
        {
//          USB_SERIAL_PRINTF("\nDEBUG GGA: NMEA='%s', override=%d, isSentenceContainingValidFix=%d, reallyHasFix=%d\n",
//                           gps.getSentence(), overrideGPSToNoFixForTesting, gps.isSentenceContainingValidFix(), reallyHasFix);

          // Update hasGPSFix for ALL GGA messages (not just ones with valid location)
          hasGPSFix = reallyHasFix && now < timeNextGoodFixExpectedBy;

          // Cache GPS fix status specifically for telemetry (use real fix status, not time-based expiry)
          // But respect simulation override - if override is active, force to false
          gpsFixStatusForTelemetry = overrideGPSToNoFixForTesting ? false : reallyHasFix;

          // Update the isFix field in telemetry for ALL GGA messages
          latestLemonTelemetry.isFix = gpsFixStatusForTelemetry;

//          USB_SERIAL_PRINTF("CACHED FIX STATUS: reallyHasFix=%d -> gpsFixStatusForTelemetry=%d, hasGPSFix=%d\n",
//                           reallyHasFix, gpsFixStatusForTelemetry, hasGPSFix);

//          USB_SERIAL_PRINTF("\nCACHED FIX STATUS: gpsFixStatusForTelemetry=%d, latestLemonTelemetry.isFix=%d\n",
//                           gpsFixStatusForTelemetry, latestLemonTelemetry.isFix);

          if (reallyHasFix)
          {
            fixCount++;
            // Reset GPS fix timeout for any valid GGA fix (only when not overriding GPS to no fix for testing)
            if (!overrideGPSToNoFixForTesting)
              timeNextGoodFixExpectedBy = now + maxTimeBeforeAlertNoFix;
            USB_SERIAL_PRINTF("\nGGA FIX: %lu  GGA NO FIX: %lu  Total GPS Msg: %lu Bad GPS Msg: %lu\n", fixCount, noFixCount, gps.passedChecksum(), gps.failedChecksum());
          }
          else
          {
            noFixCount++;
            USB_SERIAL_PRINTF("\nGGA NO FIX: %lu  GGA FIX: %lu  Total GPS Msg: %lu Bad GPS Msg: %lu%s\n", noFixCount, fixCount, gps.passedChecksum(), gps.failedChecksum(), overrideGPSToNoFixForTesting ? " [SIMULATED]" : "");
          }
        }

        // Must extract longitude and latitude for the updated flag to be set on next location update.
        if (gps.location.isValid() && gps.location.isUpdated() && gps.isSentenceFixMsgType())
        {
          // hasGPSFix already set correctly above for GGA messages, don't overwrite it

          if (now > timeOfNextLemonStatus)
          {
            sendLemonStatus(hasGPSFix ? LC_GOOD_FIX : LC_NO_FIX);
            timeOfNextLemonStatus = now + lemonStatusDutyCycle;
          }

          // Reset the GPS fix timeout whenever we receive ANY valid GPS fix (GGA or RMC)
          if (reallyHasFix)
            timeNextGoodFixExpectedBy = now + maxTimeBeforeAlertNoFix;

          // Only require uplink message for GGA, whether FIX or NO FIX.
          //////////////////////////////////////////////////////////
          // send message to outgoing serial connection to mako gopro
          if (forceGPSMissingGGARMCForTesting && (gps.isSentenceGGA() || gps.isSentenceRMC()))
          {
            if (sendOneCeaseFixCommand)
            {
              sendOneCeaseFixCommand = false;
              sendCeaseFixMessagesNMEAMessage(true,"User Input: One shot enable cease fix messages, GMA/RMC msgs now stopped, Fake No Fix will be sent after timeout");
              float f = gps.location.lat();   // dummy read to clear the updated flag
              return;
            }
          }
          else
          {
            if (sendOneReEnableFixCommand)
            {
              sendOneReEnableFixCommand = false;
              sendCeaseFixMessagesNMEAMessage(false,"User Input: One shot disable cease fix messages, wait for next GGA/RMC to send as a FIX");
              float f = gps.location.lat();   // dummy read to clear the updated flag 
  
              // don't send the just-received FIX/GGA message, wait for the next one
              return;
            }

            int fixQualityGGA=-1;
            char validFixRMC='-';

            // Send real GPS data normally
            USB_SERIAL_PRINTF("\nOriginal NMEA  %s\n",gps.getSentence()+1);

            serial_mako_gopro.write(customiseNMEASentence(gps.getSentence(), networkManager.getShowOnMapRequestIndex()));

            if (writeLogToSerial)
            {
              char* nmea_orig = gps.getSentence(); // first char is always new line
              char* nmea = nmea_orig+1;

              const char* fixType = "UNKNOWN FIX TYPE";
              const char* msgType = "UNKNOWN MSG TYPE";

              if (!strncmp(nmea, "$GPGGA", 6) || 
                  !strncmp(nmea, "$GNGGA", 6)) 
              {
                msgType = "$G-GGA";
                char fld[8];
                if (nmea_get_field(nmea, 6, fld, sizeof fld))    // 7th field
                    fixQualityGGA = atoi(fld);                       // 0=no fix, 1=GPS, 2=DGPS, ...
                fixType = (fixQualityGGA == 0) ? "NO FIX" : "FIX";
              }
              else if (!strncmp(nmea, "$GPRMC", 6) || 
                      !strncmp(nmea, "$GNRMC", 6)) 
              {
                msgType = "$G-RMC";
                char status[4] = {0};
                if (nmea_get_field(nmea, 2, status, sizeof status)) 
                {  // 3rd field
                  // status[0] == 'A' (valid) or 'V' (void)
                  validFixRMC = status[0];
                  fixType = (validFixRMC == 'A') ? "FIX" : "NO FIX";    // was A
                } 
                else 
                {
                  fixType = "NO FIX";
                }
              }

              if (!overrideGPSToNoFixForTesting)
                USB_SERIAL_PRINTF("**** SEND TO MAKO ****  Real GPS Message %s - actual %s GGA:%i RMC:%c  %s\n", msgType, fixType, fixQualityGGA, validFixRMC, nmea);
              else
                USB_SERIAL_PRINTF("**** SEND TO MAKO ****  Real GPS Message %s - under test - %s GGA:%i RMC:%c  %s\n", msgType, fixType, fixQualityGGA, validFixRMC, nmea);
            }
          }
          consoleDownlinkMsgCount++;

          if (gps.isSentenceGGA())
          {
            processUplinkMessage = true; // triggers listen for uplink msg
            uplinkMessageListenTimer = millis();
            downlinkSendMessageDurationMicroSeconds = micros();
          }

          uint32_t newPassedChecksum = gps.passedChecksum();
          uint32_t newFailedChecksum = gps.failedChecksum();

          // Update comprehensive GPS statistics
          gpsMessagesReceived = newPassedChecksum + newFailedChecksum;
          gpsFailedChecksumCount = newFailedChecksum;

          updateButtonsAndBuzzer();

          if (newPassedChecksum <= passedChecksumCount)
          {
            // incomplete message received
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
          // Log all NO FIX messages for debugging
          static char msgType[10];
          strncpy(msgType,gps.getSentence(),8);

          // first byte in getSentence() is currently always newline
          USB_SERIAL_PRINTF("NO GPS FIX - MESSAGE RECEIVED: %s  %s\n",msgType + (msgType[0] == '\n' ? 1 : 0),gps.getSentence());

          // Process NO FIX messages for telemetry if they are GGA messages
          if (gps.isSentenceGGA())
          {
            USB_SERIAL_PRINTLN("Processing NO FIX GGA message for telemetry");

            // Populate telemetry for NO FIX messages (using cached GPS fix status)
            populateCurrentLemonTelemetry(latestLemonTelemetry, gps);
            populateFinalLemonTelemetry(latestLemonTelemetry);

            // Create and commit telemetry for NO FIX messages
            BlockHeader headBlock = telemetryPipeline.getHeadBlockForPopulating();
            populateHeadWithLemonTelemetryAndCommit(headBlock);
            USB_SERIAL_PRINTLN("NO FIX: Committed telemetry to pipeline");
          }
        }
      }
      else
      {
        // no byte received.
      }
    }
  }
  // *************  END CODE FOR RECEIVING GPS MESSAGE


  // *************  START CODE FOR TELEMETRY PROCESSING FOR GPS MESSAGE RECEIVED
  // GPS module now sends real NO FIX messages, so fake GPS logic is no longer needed

  // Always allow telemetry processing - removed the 'else' to enable MQTT before GPS fix
  {
    if (processUplinkMessage && enableReadUplinkComms)
    {
      MakoDataPacket makoPacket;
      bool packetReceived = false;
      bool validMessageProcessed = false;

      // Check for received RS485 data from queue
      if (xQueueReceive(makoQueue, &makoPacket, 0) == pdTRUE)
      {
        USB_SERIAL_PRINTLN("1.0 xQueueMessage: Mako Message Received");
        
        packetReceived = true;

        // Calculate communication latency
        uint32_t receiveLatency = millis() - makoPacket.timestamp;

        int preambleStart = -1;

        bool validPreambleFound = checkForValidPreambleInReceiveBuffer(makoPacket, preambleStart);

        if (writeMakoMsgDecodingLogToSerial)
        {
          if (validPreambleFound)
            USB_SERIAL_PRINTLN("3.0 preamble: Found");
          else
            USB_SERIAL_PRINTLN("3.1 preamble: ******** MISSING *********");
        }

        if (validPreambleFound && (makoPacket.length - preambleStart) >= makoHardcodedUplinkMessageLength)
        {
          if (writeMakoMsgDecodingLogToSerial)
            USB_SERIAL_PRINTLN("4.0 Decoding Mako Message");

          uplinkMessageListenTimer = millis() - uplinkMessageListenTimer;

          // 2. Get the next free head block to populate in the telemetry pipeline
          uint16_t blockMaxPayload=0;
          BlockHeader headBlock = telemetryPipeline.getHeadBlockForPopulating();

          // 3. Populate the head block with the binary telemetry data received from Mako (or zero's if no data)
          bool messageValidatedOk = populateHeadWithMakoTelemetry(headBlock, validPreambleFound, 
                                                                  &makoPacket.data[preambleStart], 
                                                                  makoPacket.length - preambleStart);

          float tempDenominator = float(goodUplinkMessageCount+badUplinkMessageCount+uplinkMessageMissingCount);

          if (tempDenominator > 0)
            uplinkBadMessagePercentage = 100.0*float(badUplinkMessageCount+uplinkMessageMissingCount)/tempDenominator;
          
          // vars to consider for Front End
          //   g_offlineStorageThrottleApplied
          //   telemetryPipeline.isPipelineDraining()
          //   telemetryPipeline.getPipelineLength(), uplinkMessageMissingCount, badLengthUplinkMsgCount, badChkSumUplinkMsgCount (needs implementing), uplinkMessageMissingCount
          //   WiFi.status() != WL_CONNECTED
          //   privateMQTTUploadCount, uplinkMessageListenTimer, uplinkBadMessagePercentage
          //   WiFi.localIP().toString() or "No WiFi"
          //   messageValidatedOk, showOnMapRequestIndex, setTargetRequestIndex, setTargetRequest.c_str(), showOnMapRequest.c_str()
          //   newFixCount, newPassedChecksum, gps.failedChecksum()
          //   Any temperature sensors?
          
          uplinkMessageListenTimer = 0;
          
          if (!messageValidatedOk)
          {
            if (writeMakoMsgDecodingLogToSerial)
              USB_SERIAL_PRINTLN("5.0 Message Not Validated ok");
            processUplinkMessage = false;
            return;
          }
          else
          {
            if (writeMakoMsgDecodingLogToSerial)
              USB_SERIAL_PRINTLN("5.1 Message IS Validated ok");
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
            if (writeMakoMsgDecodingLogToSerial)
              USB_SERIAL_PRINTLN("6.1 Populated final lemon telemetry");

            // 4.2 Populate the head block with the binary Lemon telemetry data and commit to the telemetry pipeline.
            populateHeadWithLemonTelemetryAndCommit(headBlock);
            if (writeMakoMsgDecodingLogToSerial)
              USB_SERIAL_PRINTLN("6.2 Populated Head with lemon Telemetry and Commit");
          }
          else
          {
            // do not commit the head block - throw away the entire message
          }

          if (writeMakoMsgDecodingLogToSerial)
            USB_SERIAL_PRINTLN("7.1 Sending next msg to mqtt");
          // 5. Send the next message(s) from pipeline to private MQTT
          getNextTelemetryMessagesUploadedToPrivateMQTT();
          
          USB_SERIAL_PRINTLN("8.1 Sent next msg to mqtt");
          validMessageProcessed = true;
        }
        else if (packetReceived)
        {
          if (writeMakoMsgDecodingLogToSerial)
            USB_SERIAL_PRINTLN("9.1 Uplink msg missed count incremented - place 1");
          // Packet was received but preamble invalid or message too short
          incrementUplinkMessageMissedCount();
        }

        processUplinkMessage = false; // finished processing the uplink message
      }
      else if (now > timeNextGoodFixExpectedBy && (millis() - uplinkMessageListenTimer) > uplinkMessageLingerPeriodMs)
      {
        // No GPS fix available or GPS fix lost - create telemetry message with zero'd Mako data
        if (writeMakoMsgDecodingLogToSerial)
          USB_SERIAL_PRINTLN("11.0 No GPS fix - creating telemetry with zero'd Mako data");
        
        // Get the next free head block to populate in the telemetry pipeline
        BlockHeader headBlock = telemetryPipeline.getHeadBlockForPopulating();
        
        // Populate head block with zero'd Mako telemetry (no valid data)
        bool messageValidatedOk = populateHeadWithMakoTelemetry(headBlock, false, nullptr, 0);
        
        uint32_t timeNow = millis();
        
        // Always commit telemetry when no GPS fix to ensure MQTT uploads continue
        if (timeNow >= last_head_committed_at + telemetry_online_head_commit_duty_ms)
        {
          last_head_committed_at = timeNow;
          
          populateFinalLemonTelemetry(latestLemonTelemetry);
          if (writeMakoMsgDecodingLogToSerial)
            USB_SERIAL_PRINTLN("11.1 Populated final lemon telemetry (no GPS fix)");
          
          // Populate head block with Lemon telemetry data and commit
          populateHeadWithLemonTelemetryAndCommit(headBlock);
          if (writeMakoMsgDecodingLogToSerial)
            USB_SERIAL_PRINTLN("11.2 Populated Head with lemon Telemetry and Commit (no GPS fix)");
          
          if (writeMakoMsgDecodingLogToSerial)
            USB_SERIAL_PRINTLN("11.3 Sending next msg to mqtt (no GPS fix)");

          // Send the next message(s) from pipeline to private MQTT
          getNextTelemetryMessagesUploadedToPrivateMQTT();
          if (writeMakoMsgDecodingLogToSerial)
            USB_SERIAL_PRINTLN("11.4 Sent next msg to mqtt (no GPS fix)");
        }
        
        processUplinkMessage = false; // finished processing
      }
      else if (accumulateMissedMessageCount && (millis() - uplinkMessageListenTimer) > uplinkMessageLingerPeriodMs)
      {
        // Timeout waiting for Mako response - only count as missed if we were actually waiting
        if (processUplinkMessage)
        {
          if (writeMakoMsgDecodingLogToSerial)
            USB_SERIAL_PRINTLN("10.1 Uplink msg missed count incremented - place 2");
          incrementUplinkMessageMissedCount();
        }
        processUplinkMessage = false; // stop waiting
      }
    }
  }
  // *************  END CODE FOR TELEMETRY PROCESSING FOR GPS MESSAGE RECEIVED

  // *************  START CODE FOR SEND LEMON STATUS TO THE ARDUINO CALLED LANTERN
  now = millis();

  // Handle timeout for telemetry GPS fix status (3 second timeout)
  if (now > timeNextGoodFixExpectedBy)
  {
    // No GPS fix received within expected time, set fix status to false for telemetry
    if (gpsFixStatusForTelemetry)
    {
      USB_SERIAL_PRINTLN("GPS FIX TIMEOUT: No GPS fix within 3 seconds, setting telemetry fix status to false");
      gpsFixStatusForTelemetry = false;
      latestLemonTelemetry.isFix = gpsFixStatusForTelemetry;

      // Trigger telemetry upload even when GPS stops sending messages completely
      processUplinkMessage = true;
      uplinkMessageListenTimer = millis();
      downlinkSendMessageDurationMicroSeconds = micros();
      USB_SERIAL_PRINTLN("GPS TIMEOUT: Triggering telemetry upload for timeout condition");
    }
  }

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
  // *************  END CODE FOR SEND LEMON STATUS TO THE ARDUINO CALLED LANTERN

  // This is for test - shows value on display, good to make sure reed switches are being read ok.
//  latestLanternReedState = checkForLanternLatestReedEvent();

  // Update status display every 500 ms
  static uint32_t lastStatusUpdate = 0;
  const uint32_t wideScreenOLEDUpdatePeriod = 500;
  if (now > lastStatusUpdate + wideScreenOLEDUpdatePeriod)
  {
    // Calculate GPS statistics
    uint32_t gpsNoFixCount = noFixCount;  // Use explicit GGA NO FIX counter
    double gpsHdop = gps.hdop.hdop();
    uint8_t gpsSatellites = gps.satellites.value();
    
    // Get network status
    String ipAddress = networkManager.getLocalIP();
    bool wifiConnected = networkManager.isWiFiConnected();
    String wifiSSID = networkManager.getConnectedSSID();
    bool dnsConnected = networkManager.getLastDNSConnectivityStatus();
    bool ipConnected = networkManager.getLastIPConnectivityStatus();
    bool mqttConnected = privateMQTT.isConnected();
    
    wideDisplayManager.displayStatusScreen(
      gpsMessagesReceived, fixCount, gpsNoFixCount,
      gpsFailedChecksumCount, gpsBadLengthCount, hasGPSDevice,
      hasGPSFix, gpsHdop, gpsSatellites,
      ipAddress, privateMQTTUploadCount, wifiConnected,
      wifiSSID, dnsConnected, ipConnected, mqttConnected,
      latestLanternReedState, tempFloat, humidFloat
    );
    
    lastStatusUpdate = now;
  }
  
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

// index is the comma separate index of the field
bool nmea_get_field(const char *s, int index, char *out, size_t outsz) {
    // Skip leading control chars/newlines/spaces
    while (*s && (unsigned char)*s <= ' ') s++;
    if (*s == '$') s++;

    int cur = 0;
    const char *start = s;
    for (const char *p = s; ; ++p) {
        if (*p == ',' || *p == '*' || *p == '\0' || *p == '\r' || *p == '\n') {
            if (cur == index) {
                size_t len = (size_t)(p - start);
                if (len >= outsz) len = outsz - 1;
                memcpy(out, start, len);
                out[len] = '\0';
                return true;
            }
            if (*p == '*' || *p == '\0' || *p == '\r' || *p == '\n') break;
            cur++;
            start = p + 1;
        }
    }
    return false;
}

#define BUILD_INCLUDE_MAIN_PART2

#include "main_part2.cpp"
