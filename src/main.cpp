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

#define OLED_CS_ADA_WHITE          "XX" // undefined currently
#define OLED_RST_ADA_GREEN          42  // May not be needed - can also use 0 Strapping Pin - we know nothing will pull low at boot so ok. Could also share with SPI reset line for wide oled.
U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI wideOLEDDisplay(U8G2_R0, OLED_CS_ORANGE, OLED_DC_PURPLE, OLED_RST_BROWN);

// hardware SPI
//Adafruit_SSD1327 display(128, 128, &SPI, OLED_DC_PURPLE, OLED_RST_ADA_GREEN, OLED_CS_ADA_WHITE);

// I2C - check default pins for ProS3 are matching the I2C connector on top of board
// SDA = 8, SCL = 9, resetpin = OLED_RST_ADA_GREEN, preclk = 1000000, postclk = 100000
Adafruit_SSD1327 adafruitDisplay(128, 128, &Wire, OLED_RST_ADA_GREEN, 1000000);
LGFX_I2C_Adafruit_SSD1327_128x128_Grey_OLE lgfxAdafruitDisplay;

// Create display manager instance (256px wide, 4 lines max)
OLEDWideDisplayManager   displayManager(wideOLEDDisplay, 256, 4);
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
bool writeTelemetryLogToSerial = false; // writeLogToSerial must also be true

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


#define STATUS_LED_ON HIGH
#define STATUS_LED_OFF LOW

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
    ping_name_target,
    ping_ip_target,
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

const uint32_t telemetry_online_head_commit_duty_ms = 1000;
const uint32_t telemetry_offline_head_commit_duty_ms = telemetry_online_head_commit_duty_ms;
uint32_t last_head_committed_at = 0;
bool g_offlineStorageThrottleApplied = false;
// #### END IN-MEMORY TELEMETRY-PIPELINE / MESSAGE BUFFER CONFIG

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

uint32_t fixCount = 0;
uint32_t passedChecksumCount = 0;
bool processUplinkMessage = true;

// GPS status tracking for comprehensive display
uint32_t gpsMessagesReceived = 0;
uint32_t gpsFailedChecksumCount = 0;
uint32_t gpsBadLengthCount = 0;
uint32_t lastGPSByteTime = 0;
bool hasGPSDevice = true;  // Assume GPS device present until proven otherwise

TinyGPSPlus gps;

bool diveInProgress = false;

String getStats();

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
int32_t checkInternetConnectivityDutyCycle = 10000; // 10 seconds between each check

const uint16_t pipelineBackedUpLength = 10;

const uint8_t LEAK_DETECTOR_GPIO = 7;

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

TaskHandle_t mainTaskHandle = nullptr;
BaseType_t mainTaskCoreId = 0;

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
bool singleScreenTestAdafruitDisplay = true;
bool testLgfxAdafruitDisplay = false;

bool useGsDisplayManager = true;
bool useLxDisplayManager = false;

// ################### START UART SERIAL CONFIGURATION ############################
#define UART_NUMBER_LANTERN_NEOPIXELS  0
#define LANTERN_NEOPIXELS_ARDUINO_BAUD_RATE 9600
#define LANTERN_NEOPIXELS_TX_GPIO 40
#define LANTERN_NEOPIXELS_RX_GPIO 41
HardwareSerial serial_lantern_neopixels(UART_NUMBER_LANTERN_NEOPIXELS);

#define UART_NUMBER_GPS 1
#define GPS_BAUD_RATE 9600
#define GPS_TX_GPIO 39
#define GPS_RX_GPIO 38
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
#define UART_NUMBER_MAKO_GOPRO 2
#define MAKO_UPLINK_BAUD_RATE 57600       // max working test so far: 2,100,000
#define MAKO_GOPRO_TX_GPIO 43
#define MAKO_GOPRO_RX_GPIO 44
HardwareSerial serial_mako_gopro(UART_NUMBER_MAKO_GOPRO);

// ################### END UART SERIAL CONFIGURATION ############################

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
  serial_lantern_neopixels.begin(LANTERN_NEOPIXELS_ARDUINO_BAUD_RATE, SERIAL_8N1, LANTERN_NEOPIXELS_RX_GPIO, LANTERN_NEOPIXELS_TX_GPIO);
  uart_set_pin(UART_NUM_0, LANTERN_NEOPIXELS_TX_GPIO, LANTERN_NEOPIXELS_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  // UART1 for receiving data from GPS
  serial_gps.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_GPIO, GPS_TX_GPIO);   // pin 33=rx (white M5), pin 32=tx (yellow M5), specifies the grove SCL/SDA pins for Rx/Tx

  // UART2 for sending/receiving data to/from GoPro
  serial_mako_gopro.setRxBufferSize(1024); // was 256 - must set before begin
  serial_mako_gopro.begin(MAKO_UPLINK_BAUD_RATE, SERIAL_8N2, MAKO_GOPRO_RX_GPIO, MAKO_GOPRO_TX_GPIO);

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

  serial_lantern_neopixels.begin(LANTERN_NEOPIXELS_ARDUINO_BAUD_RATE, SERIAL_8N1, LANTERN_NEOPIXELS_RX_GPIO, LANTERN_NEOPIXELS_TX_GPIO);
  uart_set_pin(UART_NUM_0, LANTERN_NEOPIXELS_TX_GPIO, LANTERN_NEOPIXELS_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  
  USB_SERIAL_PRINTF("UART1 configured: RX=GPIO%d, TX=GPIO%d, Baud=%d\n",
                    LANTERN_NEOPIXELS_RX_GPIO, LANTERN_NEOPIXELS_TX_GPIO, LANTERN_NEOPIXELS_ARDUINO_BAUD_RATE);
  uint32_t nextTestMsg = 500;
  while(1)
  {
    if (serial_lantern_neopixels.available())
      Serial.println(serial_lantern_neopixels.read());

    if (millis() > nextTestMsg)
    {
      Serial.println("Test");
      nextTestMsg = millis() + 500;
    }
  }
}

uint32_t timeOfNextLemonStatus = 0;
const uint32_t lemonStatusDutyCycle = 1000;

uint32_t timeOfNextTelegramBotUpdateSendMsg = 0;
const uint32_t telegramBotDutyCycle = 10000;

const int initNeopixelSerialByteRead = -1;
int neopixelSerialByteRead = initNeopixelSerialByteRead;

uint8_t latestLanternReedState = 0;

bool haltAllProcessingDuringOTAUpload = false;

void prepareSystemForOTA()
{
  haltAllProcessingDuringOTAUpload = true;

  enableConnectToPrivateMQTT = false;
  enableUploadToPrivateMQTT = false;
  privateMQTT.setEnabled(false, false);
  enableReadUplinkComms = false;
  processUplinkMessage = false;
  enableAllUplinkMessageIntegrityChecks = false;
  enableGPSRead = false;
  writeLogToSerial = false;
  writeTelemetryLogToSerial = false;

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
  randomSeed(analogRead(A12));  // Use a floating analog pin for entropy  // THIS IS GPIO 13 !!!!

  initialiseUARTS();

  ProS3.begin();
  USB_SERIAL_PRINTF("=== MAIN SETUP START ===\n");
  statusLEDColourYellow();
  statusLEDOn();

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
    USB_SERIAL_PRINTLN("SPIFFS mount failed");
    displayManager.addDisplayLine("SPIFFS Mount Failed");
  } else {
    USB_SERIAL_PRINTLN("SPIFFS mounted OK");
    displayManager.addDisplayLine("SPIFFS OK");
  }

  // Initialize NetworkManager
  networkManager.setTelemetryPipeline(&telemetryPipeline);
  networkManager.setGetStatsCallback([]() { return getStats(); });
  networkManager.setIsDevNetworkCallback([]() { return devNetworkInUse(); });
  networkManager.setPrepareEntireSystemForOTA([]() { prepareSystemForOTA(); });
  networkManager.begin();

  USB_SERIAL_PRINTF("sizeof LemonTelemetry: %lu\n",getSizeOfLemonTelemetryForStorage());

  dumpHeapUsage("main: prior to Telemetry Pipeline creation  ");

  const uint16_t maxPipelineBufferKB = 2048;
  const uint16_t maxPipelineBlockPayloadSize = 256; // was 224 - Assuming 120 byte Mako Telemetry Msg and 104 byte Lemon Telemetry Msg
  BlockHeader::s_overrideMaxPayloadSize(maxPipelineBlockPayloadSize);  // 400 messages with 256 byte max payload. 
  telemetryPipeline.init(&millis,maxPipelineBufferKB);

  dumpHeapUsage("main: after Telemetry Pipeline creation  ");
  
  displayManager.addDisplayLine("Telemetry Pipeline OK");

  statusLEDOff();

  serial_lantern_neopixels.begin(LANTERN_NEOPIXELS_ARDUINO_BAUD_RATE, SERIAL_8N1, LANTERN_NEOPIXELS_RX_GPIO, LANTERN_NEOPIXELS_TX_GPIO);
  USB_SERIAL_PRINTF("UART1 configured: RX=GPIO%d, TX=GPIO%d, Baud=%d\n",
                    LANTERN_NEOPIXELS_RX_GPIO, LANTERN_NEOPIXELS_TX_GPIO, LANTERN_NEOPIXELS_ARDUINO_BAUD_RATE);

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

  // Force connectivity check to update DNS/IP status for display
  networkManager.setForceConnectivityCheckForDisplay(true);
  networkManager.getMQTTConnectionTest().resetCheckTrigger(1500);

  if (useGsDisplayManager)
    GSdisplayManager.clearDisplay();
  else if (useLxDisplayManager)
    LXdisplayManager.clearDisplay();
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

  updateButtonsAndBuzzer();

  if (enableUploadToPrivateMQTT)
      privateMQTT.loop();

  if (publishMQTTTestMessages)
    networkManager.publishMQTTTestMessageOnDutyCycle();

  if (!accumulateMissedMessageCount && millis() > delayBeforeCountingMissedMessages)
    accumulateMissedMessageCount = true;
  
  static uint32_t lastWebSocketUpdate = 0;  

  const int webSocketTick = 1000;
  if (networkManager.getWebSocketClientCount() > 0 && millis() > lastWebSocketUpdate + webSocketTick)
  {
    networkManager.sendStatsWebSocketNotification();
    lastWebSocketUpdate = millis();
  }
  
  // GPS device timeout detection (10 seconds)
  if (millis() - lastGPSByteTime > 10000) {
    hasGPSDevice = false;
  }
  
  // Update status display every 2 seconds
  static uint32_t lastStatusUpdate = 0;
  if (millis() > lastStatusUpdate + 2000) {
    // Calculate GPS statistics
    uint32_t gpsNoFixCount = gpsMessagesReceived - fixCount;
    bool hasGPSFix = gps.location.isValid();
    double gpsHdop = gps.hdop.hdop();
    uint8_t gpsSatellites = gps.satellites.value();
    
    // Get network status
    String ipAddress = networkManager.getLocalIP();
    bool wifiConnected = networkManager.isWiFiConnected();
    String wifiSSID = networkManager.getConnectedSSID();
    bool dnsConnected = networkManager.getLastDNSConnectivityStatus();
    bool ipConnected = networkManager.getLastIPConnectivityStatus();
    bool mqttConnected = privateMQTT.isConnected();
    
    displayManager.displayStatusScreen(
      gpsMessagesReceived, fixCount, gpsNoFixCount,
      gpsFailedChecksumCount, gpsBadLengthCount, hasGPSDevice,
      hasGPSFix, gpsHdop, gpsSatellites,
      ipAddress, privateMQTTUploadCount, wifiConnected,
      wifiSSID, dnsConnected, ipConnected, mqttConnected,
      latestLanternReedState
    );
    
    lastStatusUpdate = millis();
  }
  
  // *************  START CODE FOR RECEIVING GPS MESSAGE
  // Process GPS data - limit bytes per loop iteration to avoid blocking WebSocket updates
  const int maxGPSBytesPerLoop = 1000; // Process max 50 bytes per loop iteration
  int gpsDataBytesProcessed = 0;
  
  while (enableGPSRead && serial_gps.available() > 0 && gpsDataBytesProcessed < maxGPSBytesPerLoop)
  {
    char nextByte = serial_gps.read();
    gpsDataBytesProcessed++; // Count processed bytes to limit loop iterations
    
    // Update GPS device detection
    lastGPSByteTime = millis();
    hasGPSDevice = true;

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
        serial_mako_gopro.write(customiseNMEASentence(gps.getSentence(), networkManager.getShowOnMapRequestIndex()));
        consoleDownlinkMsgCount++;

        if (gps.isSentenceGGA())
        {
          processUplinkMessage = true;  // triggers listen for uplink msg
          uplinkMessageListenTimer = millis();
          downlinkSendMessageDurationMicroSeconds = micros();
        }
        
        uint32_t newFixCount = gps.sentencesWithFix();
        uint32_t newPassedChecksum = gps.passedChecksum();
        uint32_t newFailedChecksum = gps.failedChecksum();
        
        // Update comprehensive GPS statistics
        gpsMessagesReceived = newPassedChecksum + newFailedChecksum;
        gpsFailedChecksumCount = newFailedChecksum;
        
        if (newFixCount > fixCount)
        {
          fixCount = newFixCount;
          USB_SERIAL_PRINTF("\nFix: %lu Good GPS Msg: %lu Bad GPS Msg: %lu\n", fixCount, newPassedChecksum, gps.failedChecksum());
        }

        if (nofix_byte_loop_count > -1)
        {
          nofix_byte_loop_count = -1;
        }

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
        if (nofix_byte_loop_count > -1)
        {
          // Bytes are being received but no valid location fix has been seen since startup
          // Increment byte count shown until first fix received.
          nofix_byte_loop_count++;
          USB_SERIAL_PRINTLN("NO GPS FIX - BYTES BEING RECEIVED");
        }
      }
    }
    else
    {
      // no byte received.
    }
  }
  // *************  END CODE FOR RECEIVING GPS MESSAGE

  // *************  START CODE FOR TELEMETRY PROCESSING FOR GPS MESSAGE RECEIVED
  if (nofix_byte_loop_count > 0)
  {
    sendLemonStatus(LC_NO_FIX);

    sendFakeGPSData_No_Fix();

    delay(250); // no fix wait
  }
  else if (nofix_byte_loop_count != -1)
  {
    sendLemonStatus(LC_NO_GPS);

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

  // *************  END CODE FOR TELEMETRY PROCESSING FOR GPS MESSAGE RECEIVED


  // *************  START CODE FOR SEND LEMON STATUS TO THE ARDUINO CALLED LANTERN
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
    // *************  END CODE FOR SEND LEMON STATUS TO THE ARDUINO CALLED LANTERN

  }

  // This is for test - shows value on display, good to make sure reed switches are being read ok.
  latestLanternReedState = checkForLanternLatestReedEvent();

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

#define BUILD_INCLUDE_MAIN_PART2

#include "main_part2.cpp"
