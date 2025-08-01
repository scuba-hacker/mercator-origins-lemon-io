#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <ESP32Ping.h>
#include <Update.h>
#include <WebSerial.h>
#include <ArduinoJson.h>
#include "MercatorMQTT.h"
#include "OLEDDisplayManager.h"

#ifdef ENABLE_TELEGRAM_BOT_AT_COMPILE_TIME
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#endif

// Forward declarations
class TelemetryPipeline;
struct MakoUplinkTelemetryForJson;
struct LemonTelemetryForJson;

// Network configuration structure
struct NetworkConfig {
    // WiFi networks
    const char* ssid_1;
    const char* password_1;
    const char* label_1;
    uint32_t timeout_1;
    
    const char* ssid_2;
    const char* password_2;
    const char* label_2;
    uint32_t timeout_2;
    
    const char* ssid_3;
    const char* password_3;
    const char* label_3;
    uint32_t timeout_3;
    
    // Network settings
    const char* ping_name_target;
    const char* ping_ip_target;
    const char* device_hostname;
    
    // Feature flags
    bool enableOTAServer;
    bool enableWebSerial;
    bool publishMQTTTestMessages;
    
    // Web content
    const uint8_t* statsHtml;
    uint32_t statsHtmlSize;
    const uint8_t* mapHtml;
    uint32_t mapHtmlSize;
    
    // OTA device label
    const char* otaDeviceLabel;
};

// MQTT connection test class
class MQTTConnectionTest {
public:
    static const uint32_t periodMQTTConnectCheck = 1000;
    static const int maxMQTTConnectChecks = 10;
    
    bool initialTestPublishDone;
    uint32_t nextMQTTConnectCheck;
    int connectMQTTChecksDone;
    
    MQTTConnectionTest();
    void resetCheckTrigger(const uint32_t period = periodMQTTConnectCheck);
};

// Main NetworkManager class
class NetworkManager {

static const uint32_t timeBetweenInternetConnectivityChecksWhenPipelineNotDraining = 60000;    // 1 minute

private:
    // Configuration
    NetworkConfig config;
    OLEDWideDisplayManager& displayManager;
    
    // Network state
    WiFiClient wifiClient;
    String ssid_connected;
    String ssid_not_connected;
    char IPBuffer[16];
    char IPLocalGateway[16];
    char WiFiSSID[36];
    
    // Status labels
    const char* no_wifi_label;
    const char* wait_ip_label;
    const char* lost_ip_label;
    
    // OTA and web server
    bool otaActive;
    bool restartForGoodOTAScheduled;
    uint32_t restartAfterGoodOTAUpdateAt;
    bool haltAllProcessingDuringOTAUpload;
    AsyncWebServer* asyncWebServer;
    AsyncWebSocket* ws;
    AsyncElegantOtaClass* elegantOTA;
    
    // WebSocket statistics
    int32_t timeOfNextStatUpdate;
    
    // Web interface requests
    String showOnMapRequest;
    int showOnMapRequestIndex;
    String setTargetRequest;
    int setTargetRequestIndex;
    
    // JSON document for statistics - now global in main.cpp
    
    // MQTT functionality
    MercatorMQTT* privateMQTT;
    MQTTConnectionTest mqttCheck;
    uint16_t privateMQTTMessageLength;
    float KBToPrivateMQTT;
    
    // Connectivity checking
    int8_t maxPingAttempts;
    int32_t checkInternetConnectivityDutyCycle;
    
    // Connectivity status tracking for display
    bool lastInternetConnectivityStatus;
    bool lastDNSConnectivityStatus;
    bool lastIPConnectivityStatus;
    bool forceConnectivityCheckForDisplay;  // Control variable for testing
    
    // Telegram (optional)
#ifdef ENABLE_TELEGRAM_BOT_AT_COMPILE_TIME
    WiFiClientSecure* secured_client;
    UniversalTelegramBot* telegramBot;
#endif
    
    // WebSerial state
    bool webSerialInitialised;
    
    // External dependencies (injected)
    TelemetryPipeline* telemetryPipeline;
    std::function<String()> getStatsCallback;
    std::function<void()> updateButtonsCallback;
    std::function<bool()> isDevNetworkCallback;
    std::function<void()> prepareEntireSystemForOTA;
    
    // Private helper methods
    void initWebSocket();
    void addWebSocketToServer();
    void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
    void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
    void webSerialReceiveMessage(uint8_t *data, size_t len);
    void setupWebServerRoutes();
    void prepareNetworkForOTA();
    
    // Static callback wrappers for WiFi events
    static void wifiStationConnectedWrapper(WiFiEvent_t event, WiFiEventInfo_t info);
    static void wifiGotIPWrapper(WiFiEvent_t event, WiFiEventInfo_t info);
    static void wifiLostIPWrapper(WiFiEvent_t event, WiFiEventInfo_t info);
    static void wifiStationDisconnectedWrapper(WiFiEvent_t event, WiFiEventInfo_t info);
    
    // Static callback wrappers for OTA
    static void uploadOTABeginCallbackWrapper(AsyncElegantOtaClass* originator);
    static void uploadOTAProgressCallbackWrapper(AsyncElegantOtaClass* originator, size_t progress, size_t total);
    static void uploadOTASucceededCallbackWrapper(AsyncElegantOtaClass* originator);
    
    // Static instance pointer for callbacks
    static NetworkManager* instance;
    
public:
    // Constructor
    NetworkManager(const NetworkConfig& networkConfig, 
                   OLEDWideDisplayManager& displayMgr,
                   MercatorMQTT& mqtt);
    
    // Destructor
    ~NetworkManager();
    
    // Initialization
    void begin();
    void loop();
    
    // External dependency injection
    void setTelemetryPipeline(TelemetryPipeline* pipeline) { telemetryPipeline = pipeline; }
    void setGetStatsCallback(std::function<String()> callback);
    void setIsDevNetworkCallback(std::function<bool()> callback) { isDevNetworkCallback = callback; }
    void setPrepareEntireSystemForOTA(std::function<void()> callback) { prepareEntireSystemForOTA = callback; }
    
    // Main networking functions
    bool connectToWiFiAndInitOTA(const bool wifiOnly, int repeatScanAttempts);
    void toggleOTAActive();
    void toggleWiFiActive();
    const char* scanForKnownNetworkAsync();
    const char* scanForKnownNetwork();
    bool setupOTAWebServer(const char* _ssid, const char* _password, const char* label, uint32_t timeout, bool wifiOnly);
    
    // WiFi event handlers
    void wifiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info);
    void wifiGotIP(WiFiEvent_t event, WiFiEventInfo_t info);
    void wifiLostIP(WiFiEvent_t event, WiFiEventInfo_t info);
    void wifiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
    
    // Connectivity checking
    void checkConnectivity();
    bool isInternetAccessible();
    bool isScubaMosquittoBrokerAvailable();
    bool getLastInternetConnectivityStatus() const { return lastInternetConnectivityStatus; }
    bool getLastDNSConnectivityStatus() const { return lastDNSConnectivityStatus; }
    bool getLastIPConnectivityStatus() const { return lastIPConnectivityStatus; }
    void setForceConnectivityCheckForDisplay(bool enabled) { forceConnectivityCheckForDisplay = enabled; }
    
    // WebSocket functionality
    void notifyWebSocketClients(const String& sensorReadings);
    void sendStatsWebSocketNotification();
    
    // MQTT functionality
    MQTTConnectionResult publishMQTTTestMessageOnDutyCycle(const char* topic = "test_mqtt", uint32_t testPublishDutyCycle = 1000);
    char* getMQTTPayloadBuffer();
    
    // OTA callbacks
    void uploadOTABeginCallback();
    void uploadOTAProgressCallback(size_t progress, size_t total);
    void uploadOTASucceededCallback();
    
    // Status getters
    bool isOTAActive() const { return otaActive; }
    bool isWiFiConnected() const { return WiFi.status() == WL_CONNECTED; }
    String getConnectedSSID() const { return ssid_connected; }
    String getLocalIP() const { return WiFi.localIP().toString(); }
    String getGatewayIP() const { return String(IPLocalGateway); }
    bool isHaltingForOTA() const { return haltAllProcessingDuringOTAUpload; }
    bool isRestartScheduled() const { return restartForGoodOTAScheduled; }
    uint32_t getRestartTime() const { return restartAfterGoodOTAUpdateAt; }
    
    // Web interface requests
    String getShowOnMapRequest() const { return showOnMapRequest; }
    int getShowOnMapRequestIndex() const { return showOnMapRequestIndex; }
    String getSetTargetRequest() const { return setTargetRequest; }
    int getSetTargetRequestIndex() const { return setTargetRequestIndex; }
    
    // Statistics (some now global in main.cpp)
    uint16_t getPrivateMQTTMessageLength() const { return privateMQTTMessageLength; }
    float getKBToPrivateMQTT() const { return KBToPrivateMQTT; }
    
    // WebSocket client count
    size_t getWebSocketClientCount() const { return ws ? ws->count() : 0; }
    int32_t getTimeOfNextStatUpdate() const { return timeOfNextStatUpdate; }
    void setTimeOfNextStatUpdate(int32_t time) { timeOfNextStatUpdate = time; }
    
    // MQTT connection test access
    MQTTConnectionTest& getMQTTConnectionTest() { return mqttCheck; }
};