#include "NetworkManager.h"
#include "SerialConfig.h"
#include "TelemetryPipeline.h"
#include <NavigationWaypoints.h>

#include "logs_page.h"

extern "C" {
  #include "lwip/dns.h"
}

// Access global variables from main.cpp
extern int32_t lastCheckForInternetConnectivityAt;
extern uint32_t privateMQTTUploadCount;
extern bool forceGPSNoFixForTesting;

// Static instance pointer for callbacks
NetworkManager* NetworkManager::instance = nullptr;

// MQTTConnectionTest implementation
MQTTConnectionTest::MQTTConnectionTest() 
    : nextMQTTConnectCheck(0), connectMQTTChecksDone(0), initialTestPublishDone(false) {
}

void MQTTConnectionTest::resetCheckTrigger(const uint32_t period) {
    nextMQTTConnectCheck = millis() + period;
}

// NetworkManager implementation
NetworkManager::NetworkManager(const NetworkConfig& networkConfig, 
                               OLEDWideDisplayManager& displayMgr,
                               MercatorMQTT& mqtt)
    : config(networkConfig)
    , displayManager(displayMgr)
    , ssid_not_connected("-")
    , ssid_connected(ssid_not_connected)
    , no_wifi_label("No WiFi")
    , wait_ip_label("Wait IP")
    , lost_ip_label("Lost IP")
    , otaActive(false)
    , restartForGoodOTAScheduled(false)
    , restartAfterGoodOTAUpdateAt(0)
    , haltAllProcessingDuringOTAUpload(false)
    , asyncWebServer(nullptr)
    , ws(nullptr)
    , elegantOTA(nullptr)
    , timeOfNextStatUpdate(0)
    , showOnMapRequestIndex(-1)
    , setTargetRequestIndex(-1)
    , privateMQTT(&mqtt)
    , privateMQTTMessageLength(0)
    , KBToPrivateMQTT(0.0)
    , maxPingAttempts(1)
    , checkInternetConnectivityDutyCycle(timeBetweenInternetConnectivityChecksWhenPipelineNotDraining)
    , lastInternetConnectivityStatus(false)
    , lastDNSConnectivityStatus(false)
    , lastIPConnectivityStatus(false)
    , forceConnectivityCheckForDisplay(false)  // Disable force which is used for testing
#ifdef ENABLE_TELEGRAM_BOT_AT_COMPILE_TIME
    , secured_client(nullptr)
    , telegramBot(nullptr)
#endif
    , webSerialInitialised(false)
    , telemetryPipeline(nullptr)
{
    // Set static instance for callbacks
    instance = this;
    
    // Initialize buffers
    strcpy(IPBuffer, no_wifi_label);
    strcpy(IPLocalGateway, "");
    strcpy(WiFiSSID, "");
    
    // Create web server and WebSocket instances
    asyncWebServer = new AsyncWebServer(80);
    ws = new AsyncWebSocket("/ws");
    elegantOTA = new AsyncElegantOtaClass();
    
#ifdef ENABLE_TELEGRAM_BOT_AT_COMPILE_TIME
    secured_client = new WiFiClientSecure();
    telegramBot = new UniversalTelegramBot(TELEGRAM_BOT_TOKEN, *secured_client);
#endif
}

NetworkManager::~NetworkManager() {
    delete asyncWebServer;
    delete ws;
    delete elegantOTA;
    
#ifdef ENABLE_TELEGRAM_BOT_AT_COMPILE_TIME
    delete secured_client;
    delete telegramBot;
#endif
    
    if (instance == this) {
        instance = nullptr;
    }
}

void NetworkManager::begin() {
    // Register WiFi event handlers
    WiFi.onEvent(wifiStationConnectedWrapper, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
    WiFi.onEvent(wifiGotIPWrapper, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(wifiLostIPWrapper, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_LOST_IP);
    WiFi.onEvent(wifiStationDisconnectedWrapper, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    
    // Initialize MQTT connection test
    mqttCheck.resetCheckTrigger(1500);
}

void NetworkManager::loop() {
    // Handle OTA restart if scheduled
    if (restartForGoodOTAScheduled && millis() >= restartAfterGoodOTAUpdateAt) {
        ESP.restart();
    }
    
    // Skip processing during OTA upload
    if (haltAllProcessingDuringOTAUpload) {
        return;
    }
    
    // Handle MQTT connection testing
    if (config.publishMQTTTestMessages && WiFi.status() == WL_CONNECTED && !mqttCheck.initialTestPublishDone) {
        if (mqttCheck.connectMQTTChecksDone < mqttCheck.maxMQTTConnectChecks) {
            if (millis() > mqttCheck.nextMQTTConnectCheck) {
                mqttCheck.connectMQTTChecksDone++;
                mqttCheck.nextMQTTConnectCheck = millis() + mqttCheck.periodMQTTConnectCheck;
                
                if (privateMQTT->isConnected()) {
                    const char* topic = "test-connection";
                    const char* message = "First message to test the connection";
                    MQTTConnectionResult result = privateMQTT->publish(topic, message);
                    if (result == MQTTConnectionResult::SUCCESS) {
                        USB_SERIAL_PRINTF("[%lu] Publish MQTT Connect Validation message on topic %s (%s)  Result = %s\n", millis(), topic, privateMQTT->getEncryptionStatus(), MercatorMQTT::resultToText(result));
                        displayManager.addDisplayLine(String("MQTT Broker Connected @ ") + privateMQTT->getCurrentHostname());
                    } else {
                        USB_SERIAL_PRINTF("[%lu] Publish MQTT Connect Validation message on topic %s (%s)  Result = %s\n", millis(), topic, privateMQTT->getEncryptionStatus(), MercatorMQTT::resultToText(result));
                        displayManager.addDisplayLine("MQTT Connected & Publish Failed");
                    }
                    mqttCheck.initialTestPublishDone = true;
                } else {
                    char tmp[64];
                    snprintf(tmp, sizeof(tmp), "MQTT: Trying to Connect %i of %i", 
                             mqttCheck.connectMQTTChecksDone, mqttCheck.maxMQTTConnectChecks);
                    USB_SERIAL_PRINTLN(tmp);
                    displayManager.addDisplayLine(tmp);
                }
            }
        } else {
            USB_SERIAL_PRINTLN("[MQTT: Cannot connect to broker");
            displayManager.addDisplayLine("MQTT: Cannot connect to broker");
            delay(2000);
            mqttCheck.initialTestPublishDone = true;
        }
    }
    
    // Handle MQTT test messages
    if (config.publishMQTTTestMessages) {
        publishMQTTTestMessageOnDutyCycle();
    }
    
    // Handle connectivity checking
    checkConnectivity();
}

const char* NetworkManager::scanForKnownNetworkAsync() {
    const char* network = nullptr;

    // Append scanning status to scrolling line
    displayManager.updateScrollingStatusLine("Scanning");
    
    // Start progress animation for scanning
    displayManager.startProgressAnimation();

    // Clear any previous scan results first
    WiFi.scanDelete();
    
    // Ensure WiFi is in station mode and ready for scanning
    USB_SERIAL_PRINTF("WiFi status before scan: %d\n", WiFi.status());
    WiFi.mode(WIFI_STA);
    delay(100);  // Give more time for mode change
    USB_SERIAL_PRINTF("WiFi status after mode set: %d\n", WiFi.status());
    
    // Check if there's an ongoing scan and wait for it to complete
    int16_t preScanCheck = WiFi.scanComplete();
    USB_SERIAL_PRINTF("Pre-scan check: %d\n", preScanCheck);
    if (preScanCheck == -1) {
        USB_SERIAL_PRINTF("Previous scan still running, waiting for completion...\n");
        int waitCount = 0;
        while (WiFi.scanComplete() == -1 && waitCount < 50) {  // Wait up to 5 seconds
            delay(100);
            waitCount++;
        }
        USB_SERIAL_PRINTF("Previous scan wait completed after %d iterations\n", waitCount);
        WiFi.scanDelete();  // Clean up
    }
    
    // Start async WiFi scan
    int16_t startResult = WiFi.scanNetworks(true, false);  // async=true, show_hidden=false
    
    USB_SERIAL_PRINTF("Async WiFi scan start result: %d\n", startResult);
    
    // Give a moment for scan to actually start
    delay(50);
    
    // Check if scan actually started
    int16_t initialCheck = WiFi.scanComplete();
    USB_SERIAL_PRINTF("Initial scan status check: %d\n", initialCheck);
    
    if (initialCheck == -2) {
        displayManager.stopProgressAnimation();
        displayManager.updateScrollingStatusLine("Scan failed to start");
        return nullptr;
    }
    
    // Poll for scan completion while showing progress
    int16_t scanResults = -1;  // -1 means scan is running
    int timeoutCount = 0;
    const int maxTimeout = 100; // 10 seconds max (100 * 100ms)
    
    USB_SERIAL_PRINTF("Starting scan polling loop (max %d iterations)\n", maxTimeout);
    
    while (timeoutCount < maxTimeout) {
        scanResults = WiFi.scanComplete();
        
        if (scanResults == -1) {
            // Scan still running
            if (timeoutCount % 10 == 0) {  // Log every 1 second
                USB_SERIAL_PRINTF("Scan still running... (iteration %d/%d)\n", timeoutCount, maxTimeout);
            }
            displayManager.updateProgressAnimation();  // Show scanning progress
            delay(100);  // Check every 100ms
            timeoutCount++;
        } else if (scanResults == -2) {
            // No scan was started
            USB_SERIAL_PRINTF("ERROR: scanComplete() returned -2 (no scan) at iteration %d\n", timeoutCount);
            displayManager.stopProgressAnimation();
            displayManager.updateScrollingStatusLine("No scan started");
            return nullptr;
        } else {
            // Scan completed (scanResults >= 0)
            USB_SERIAL_PRINTF("Scan completed! Found %d networks after %d iterations\n", scanResults, timeoutCount);
            break;
        }
    }
    
    // Stop progress animation
    displayManager.stopProgressAnimation();
    
    // Handle timeout
    if (timeoutCount >= maxTimeout) {
        USB_SERIAL_PRINTF("TIMEOUT: Scan timed out after %d iterations (%d seconds)\n", timeoutCount, timeoutCount/10);
        displayManager.updateScrollingStatusLine("Scan timeout");
        WiFi.scanDelete();
        return nullptr;
    }
    
    if (scanResults > 0) {
        USB_SERIAL_PRINTF("Processing %d scan results:\n", scanResults);
        for (int i = 0; i < scanResults; ++i) {
            // Print SSID and RSSI for each device found
            String SSID = WiFi.SSID(i);
            int32_t RSSI = WiFi.RSSI(i);
            USB_SERIAL_PRINTF("  [%d] %s (RSSI: %d)\n", i, SSID.c_str(), RSSI);

            // Check if the current device matches known networks
            if (strcmp(SSID.c_str(), config.ssid_1) == 0) {
                network = config.ssid_1;
                USB_SERIAL_PRINTF("  -> MATCH: Found known network %s\n", config.ssid_1);
            } else if (strcmp(SSID.c_str(), config.ssid_2) == 0) {
                network = config.ssid_2;
                USB_SERIAL_PRINTF("  -> MATCH: Found known network %s\n", config.ssid_2);
            } else if (strcmp(SSID.c_str(), config.ssid_3) == 0) {
                network = config.ssid_3;
                USB_SERIAL_PRINTF("  -> MATCH: Found known network %s\n", config.ssid_3);
            }

            if (network)
                break;
        }    
    } else {
        USB_SERIAL_PRINTF("No networks found in scan (scanResults = %d)\n", scanResults);
    }

    if (network) {
        // Append found network to scrolling line
        displayManager.updateScrollingStatusLine("Found " + String(network));
    } else {
        // Append not found to scrolling line
        displayManager.updateScrollingStatusLine("No known networks found");
    }

    // Clean up scan results
    WiFi.scanDelete();
    return network;
}

const char* NetworkManager::scanForKnownNetwork() {
    const char* network = nullptr;

    // Append scanning status to scrolling line
    displayManager.updateScrollingStatusLine("Scanning");

    // Perform sync WiFi scan (fallback method)
    int8_t scanResults = WiFi.scanNetworks();
    
    if (scanResults > 0) {
        for (int i = 0; i < scanResults; ++i) {
            // Print SSID and RSSI for each device found
            String SSID = WiFi.SSID(i);

            // Check if the current device starts with the peerSSIDPrefix
            if (strcmp(SSID.c_str(), config.ssid_1) == 0)
                network = config.ssid_1;
            else if (strcmp(SSID.c_str(), config.ssid_2) == 0)
                network = config.ssid_2;
            else if (strcmp(SSID.c_str(), config.ssid_3) == 0)
                network = config.ssid_3;

            if (network)
                break;
        }    
    }

    if (network) {
        // Append found network to scrolling line
        displayManager.updateScrollingStatusLine("Found " + String(network));
    } else {
        // Append not found to scrolling line
        displayManager.updateScrollingStatusLine("No known networks found");
    }

    // clean up ram
    WiFi.scanDelete();

    return network;
}

bool NetworkManager::connectToWiFiAndInitOTA(const bool wifiOnly, int repeatScanAttempts) {
    USB_SERIAL_PRINTF("=== connectToWiFiAndInitOTA ENTRY: wifiOnly=%i repeatScanAttempts=%i WiFiStatus=%i otaActive=%i ===\n", wifiOnly, repeatScanAttempts, WiFi.status(), otaActive);
    
    if (wifiOnly && WiFi.status() == WL_CONNECTED)
        return true;

    // Initialize scrolling status line
    displayManager.updateScrollingStatusLine("WiFi: Searching", false);

    while (repeatScanAttempts-- &&
           (WiFi.status() != WL_CONNECTED ||
            WiFi.status() == WL_CONNECTED && wifiOnly == false && otaActive == false)) {
        const char* network = scanForKnownNetworkAsync();
    
        if (!network) {
            // Append retry status to scrolling line
            displayManager.updateScrollingStatusLine("No networks found, retrying");
            continue;
        }
        
        int connectToFoundNetworkAttempts = 3;
        const int repeatDelay = 1000;
        
        // Append connecting status to scrolling line
        displayManager.updateScrollingStatusLine("Connecting to " + String(network), true, true);
        
        // Start progress animation
        displayManager.startProgressAnimation();
    
        if (strcmp(network, config.ssid_1) == 0) {
            while (connectToFoundNetworkAttempts-- && !setupOTAWebServer(config.ssid_1, config.password_1, config.label_1, config.timeout_1, wifiOnly))
                delay(repeatDelay);
        } else if (strcmp(network, config.ssid_2) == 0) {
            while (connectToFoundNetworkAttempts-- && !setupOTAWebServer(config.ssid_2, config.password_2, config.label_2, config.timeout_2, wifiOnly))
                delay(repeatDelay);
        } else if (strcmp(network, config.ssid_3) == 0) {
            while (connectToFoundNetworkAttempts-- && !setupOTAWebServer(config.ssid_3, config.password_3, config.label_3, config.timeout_3, wifiOnly))
                delay(repeatDelay);
        }
        
        // Stop progress animation after connection attempts
        displayManager.stopProgressAnimation();
    }

    bool connected = WiFi.status() == WL_CONNECTED;
    
    if (connected) {
        ssid_connected = WiFi.SSID();
        displayManager.updateScrollingStatusLine("SUCCESS! Connected to " + ssid_connected, true, true);
        // Quietly add to display array for later scrolling, without refreshing screen
        displayManager.addDisplayLine("SUCCESS! Connected to " + ssid_connected, false, true);
    } else {
        ssid_connected = ssid_not_connected;
        displayManager.updateScrollingStatusLine("FAILED! Connection unsuccessful", true, true);
        // Quietly add to display array for later scrolling, without refreshing screen
        displayManager.addDisplayLine("FAILED! Connection unsuccessful", false, true);
    }
    
    return connected;
}

bool NetworkManager::setupOTAWebServer(const char* _ssid, const char* _password, const char* label, uint32_t timeout, bool wifiOnly) {
    USB_SERIAL_PRINTF("=== setupOTAWebServer ENTRY: ssid=%s wifiOnly=%i otaActive=%i WiFiStatus=%i ===\n", _ssid, wifiOnly, otaActive, WiFi.status());
    
    if (wifiOnly && WiFi.status() == WL_CONNECTED) {
        USB_SERIAL_PRINTF("setupOTAWebServer: attempt to connect wifiOnly, already connected - otaActive=%i\n",otaActive);
        return true;
    }

    USB_SERIAL_PRINTF("setupOTAWebServer: attempt to connect %s wifiOnly=%i when otaActive=%i\n",_ssid, wifiOnly,otaActive);


    bool forcedCancellation = false;
    bool connected = false;
    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setHostname(config.device_hostname);

    WiFi.begin(_ssid, _password);
    
#ifdef ENABLE_TELEGRAM_BOT_AT_COMPILE_TIME
    if (secured_client && config.enableTelegram)
        secured_client->setCACert(TELEGRAM_CERTIFICATE_ROOT);
#endif

    // Wait for connection for max of timeout/1000 seconds
    const int progressStep = 100;
    int count = timeout / progressStep;
    while (WiFi.status() != WL_CONNECTED && --count > 0) {
        // check for cancellation button - top button.
        if (updateButtonsCallback)
            updateButtonsCallback();

        // Note: Button checking would need to be injected via callback
        // if (p_primaryButton->isPressed()) {
        //     forcedCancellation = true;
        //     break;
        // }

        // Update progress animation
        displayManager.updateProgressAnimation();

        delay(progressStep);
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (wifiOnly == false && !otaActive) {
            USB_SERIAL_PRINTLN("setupOTAWebServer: WiFi connected ok, starting up OTA");  
            USB_SERIAL_PRINTLN("setupOTAWebServer: calling asyncWebServer.on");

            setupWebServerRoutes();
            
            // Initialize WebSocket (like in backup - after routes, before server.begin())
            initWebSocket();
            addWebSocketToServer();

            USB_SERIAL_PRINTLN("setupOTAWebServer: calling AsyncElegantOTA.begin");

            elegantOTA->setID(config.otaDeviceLabel);
            elegantOTA->setUploadBeginCallback(uploadOTABeginCallbackWrapper);
            elegantOTA->setUploadProgressCallback(uploadOTAProgressCallbackWrapper);
            elegantOTA->setUploadSucceededCallback(uploadOTASucceededCallbackWrapper);
            elegantOTA->begin(asyncWebServer);

            if (config.enableWebSerial && !webSerialInitialised) {
                WebSerial.begin(asyncWebServer);
                WebSerial.msgCallback([this](uint8_t *data, size_t len) {
                    this->webSerialReceiveMessage(data, len);
                });
                webSerialInitialised = true;
            }

            USB_SERIAL_PRINTLN("setupOTAWebServer: calling asyncWebServer.begin");

            asyncWebServer->begin();
            USB_SERIAL_PRINTLN("setupOTAWebServer: OTA setup complete");
            otaActive = true;
            delay(2000);
            connected = true;
            
            if (updateButtonsCallback)
                updateButtonsCallback();
        }
    }

    return connected;
}

void NetworkManager::setupWebServerRoutes() {
    asyncWebServer->on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
        request->send(200, "text/plain", "To upload firmware use /update");
    });

    asyncWebServer->on("/reboot", HTTP_GET, [](AsyncWebServerRequest * request) {
        request->send(200, "text/plain", "Rebooting");
        delay(500);
        USB_SERIAL_PRINTLN("Restarting now...");
        esp_restart();
    });

    asyncWebServer->on("/logs", HTTP_GET, [](AsyncWebServerRequest * request) {
        request->send_P(200, "text/html", LOGS_PAGE_HTML);
      });

    // Debug endpoint to test WebSocket
    asyncWebServer->on("/test-ws", HTTP_GET, [this](AsyncWebServerRequest * request) {
        USB_SERIAL_PRINTF("WebSocket test endpoint called - connected clients: %d\n", ws ? ws->count() : 0);
        if (getStatsCallback) {
            String testData = getStatsCallback();
            USB_SERIAL_PRINTF("WebSocket test: sending data length %d\n", testData.length());
            notifyWebSocketClients(testData);
            request->send(200, "text/plain", "WebSocket test sent to " + String(ws ? ws->count() : 0) + " clients");
        } else {
            request->send(500, "text/plain", "getStatsCallback not set");
        }
    });

    // Simple WebSocket test page
    asyncWebServer->on("/ws-test", HTTP_GET, [](AsyncWebServerRequest * request) {
        String html = "<!DOCTYPE html>"
                     "<html>"
                     "<head><title>WebSocket Test</title></head>"
                     "<body>"
                     "<h1>WebSocket Test</h1>"
                     "<div id=\"status\">Disconnected</div>"
                     "<div id=\"messages\"></div>"
                     "<button onclick=\"sendTest()\">Send Test</button>"
                     "<script>"
                     "var ws = new WebSocket('ws://' + window.location.hostname + '/ws');"
                     "var messages = document.getElementById('messages');"
                     "var status = document.getElementById('status');"
                     "ws.onopen = function() {"
                         "status.innerHTML = 'Connected';"
                         "status.style.color = 'green';"
                         "console.log('WebSocket connected');"
                     "};"
                     "ws.onmessage = function(event) {"
                         "console.log('Received:', event.data);"
                         "messages.innerHTML += '<p>Received: ' + event.data.substring(0, 200) + '...</p>';"
                     "};"
                     "ws.onclose = function() {"
                         "status.innerHTML = 'Disconnected';"
                         "status.style.color = 'red';"
                         "console.log('WebSocket disconnected');"
                     "};"
                     "ws.onerror = function(error) {"
                         "status.innerHTML = 'Error';"
                         "status.style.color = 'red';"
                         "console.log('WebSocket error:', error);"
                     "};"
                     "function sendTest() {"
                         "if (ws.readyState === WebSocket.OPEN) {"
                             "ws.send('test message');"
                         "}"
                     "}"
                     "</script>"
                     "</body>"
                     "</html>";
        request->send(200, "text/html", html);
    });

    asyncWebServer->on("/stats", HTTP_GET, [this](AsyncWebServerRequest * request) {
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", config.statsHtml, config.statsHtmlSize); 
        request->send(response);
    });

    asyncWebServer->on("/map", HTTP_GET, [this](AsyncWebServerRequest * request) {
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", config.mapHtml, config.mapHtmlSize); 
        request->send(response);
    });
        
    asyncWebServer->on("/stats", HTTP_POST, [this](AsyncWebServerRequest *request) {
        const AsyncWebParameter* pButton = request->getParam("button", true, false);

        if (pButton) {
            request->send(200, "text/html", "ok");
            if (pButton->value() == String("rebootButton")) {
                esp_restart();
            } else if (pButton->value() == String("clearCountersButton")) {
                // Note: Counter clearing would need to be done via callback
                // uplinkMessageMissingCount = consoleDownlinkMsgCount = privateMQTTUploadCount = 0;
                // badUplinkMessageCount = badLengthUplinkMsgCount = badChkSumUplinkMsgCount = goodUplinkMessageCount = 0;
            } else if (pButton->value() == String("showOnMapButton")) {
                const AsyncWebParameter* pChoice = request->getParam("choice", true, false);
                if (pChoice) {
                    showOnMapRequest = pChoice->value();
                    showOnMapRequestIndex = -1;

                    String searchWaypoint;
                    for (int i = 0; i < WraysburyWaypoints::getWaypointsCount(); i++) {
                        searchWaypoint = WraysburyWaypoints::waypoints[i]._label;
                        if (searchWaypoint.indexOf(showOnMapRequest) != -1) {
                            showOnMapRequestIndex = i;
                            break;
                        }
                    }

                    if (showOnMapRequestIndex == -1)
                        showOnMapRequest = "";
                }
            } else if (pButton->value() == String("setTargetButton")) {
                const AsyncWebParameter* pTarget = request->getParam("target", true, false);
                if (pTarget) {
                    setTargetRequest = pTarget->value();
                    setTargetRequestIndex = -1;

                    String searchWaypoint;
                    for (int i = 0; i < WraysburyWaypoints::getWaypointsCount(); i++) {
                        searchWaypoint = WraysburyWaypoints::waypoints[i]._label;
                        if (searchWaypoint.indexOf(setTargetRequest) != -1) {
                            setTargetRequestIndex = i;
                            break;
                        }
                    }

                    if (setTargetRequestIndex == -1)
                        setTargetRequest = "";
                }
            } else if (pButton->value() == String("gpsSimToggleButton")) {
                forceGPSNoFixForTesting = !forceGPSNoFixForTesting;
                Serial.print("GPS NO FIX simulation toggled to: ");
                Serial.println(forceGPSNoFixForTesting ? "ACTIVE" : "INACTIVE");
            }
        } else {
            request->send(200, "text/plain", "invalid");
        }
    });
}

void NetworkManager::toggleOTAActive() {
    if (otaActive) {
        asyncWebServer->end();
        otaActive = false;
        delay(2000);
    } else {
        if (WiFi.status() == WL_CONNECTED) {
            // Need to re-setup routes and WebSocket when restarting server
            setupWebServerRoutes();
            initWebSocket();
            addWebSocketToServer();
            asyncWebServer->begin();
            otaActive = true;
        }
        delay(200);
    }
}

void NetworkManager::toggleWiFiActive() {
    if (WiFi.status() == WL_CONNECTED) {
        if (otaActive) {
            asyncWebServer->end();
            otaActive = false;
        }

        WiFi.disconnect();
        ssid_connected = ssid_not_connected;
        delay(2000);
    } else {
        const bool wifiOnly = true;
        const int scanAttempts = 3;
        connectToWiFiAndInitOTA(wifiOnly, scanAttempts);
        delay(2000);
    }
}

void NetworkManager::initWebSocket() {
    USB_SERIAL_PRINTF("NetworkManager: Initializing WebSocket at /ws\n");
    ws->onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        this->onWebSocketEvent(server, client, type, arg, data, len);
    });
    USB_SERIAL_PRINTF("NetworkManager: WebSocket event handler set\n");
}

void NetworkManager::addWebSocketToServer() {
    if (asyncWebServer && ws) {
        asyncWebServer->addHandler(ws);
        USB_SERIAL_PRINTF("NetworkManager: WebSocket handler added to server\n");
    }
}

void NetworkManager::onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            USB_SERIAL_PRINTF("WebSocket: Client connected from IP %s\n", client->remoteIP().toString().c_str());
            if (getStatsCallback) {
                USB_SERIAL_PRINTF("WebSocket: Sending initial stats to new client\n");
                notifyWebSocketClients(getStatsCallback()); // re-established
            } else {
                USB_SERIAL_PRINTF("WebSocket: WARNING - getStatsCallback is not set!\n");
            }
            break;
        case WS_EVT_DISCONNECT:
            USB_SERIAL_PRINTF("WebSocket: Client disconnected - remaining clients: %d\n", ws ? ws->count() : 0);
            break;
        case WS_EVT_DATA:
            USB_SERIAL_PRINTF("WebSocket: Received data from client\n");
            handleWebSocketMessage(arg, data, len);
            break;
        case WS_EVT_PONG:
            USB_SERIAL_PRINTF("WebSocket: Received PONG\n");
            break;
        case WS_EVT_ERROR:
            USB_SERIAL_PRINTF("WebSocket: Error occurred\n");
            break;
    }
}

void NetworkManager::handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    USB_SERIAL_PRINTF("WebSocket: Message received - len=%d, final=%d, index=%d, info->len=%d, opcode=%d\n", 
                      len, info->final, info->index, info->len, info->opcode);
    
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        USB_SERIAL_PRINTF("WebSocket: Processing message: %s\n", (char*)data);
        
        if (getStatsCallback) {
            USB_SERIAL_PRINTF("WebSocket: Calling getStats callback\n");
            String stats = getStatsCallback();
            USB_SERIAL_PRINTF("WebSocket: Stats result length: %d\n", stats.length());
            notifyWebSocketClients(stats);
        } else {
            USB_SERIAL_PRINTF("WebSocket: ERROR - getStatsCallback is NULL!\n");
        }
    } else {
        USB_SERIAL_PRINTF("WebSocket: Message conditions not met - ignoring\n");
    }
}

void NetworkManager::notifyWebSocketClients(const String& sensorReadings) {
    if (ws) {
        USB_SERIAL_PRINTF("WebSocket: Sending data to %d clients (length: %d)\n", ws->count(), sensorReadings.length());
        ws->textAll(sensorReadings);
    } else {
        USB_SERIAL_PRINTF("WebSocket: ERROR - ws is null!\n");
    }
}

void NetworkManager::setGetStatsCallback(std::function<String()> callback) {
    getStatsCallback = callback; 
    USB_SERIAL_PRINTF("NetworkManager: getStatsCallback has been set\n");
    
    // Test the callback immediately to ensure it works
    if (getStatsCallback) {
        String testStats = getStatsCallback();
        USB_SERIAL_PRINTF("NetworkManager: Test callback result length: %d\n", testStats.length());
        if (testStats.length() > 0) {
            USB_SERIAL_PRINTF("NetworkManager: Test callback result preview: %.100s\n", testStats.c_str());
        }
    }
}

void NetworkManager::sendStatsWebSocketNotification() {
    USB_SERIAL_PRINTF("WebSocket: sendStatsWebSocketNotification() called - client count: %d\n", getWebSocketClientCount());
    if (getStatsCallback) {
        USB_SERIAL_PRINTF("WebSocket: Calling getStats and notifying clients\n");
        String stats = getStatsCallback();
        USB_SERIAL_PRINTF("WebSocket: Generated stats length: %d\n", stats.length());
        notifyWebSocketClients(stats);
    } else {
        USB_SERIAL_PRINTF("WebSocket: ERROR - getStatsCallback is not set!\n");
    }
}

void NetworkManager::webSerialReceiveMessage(uint8_t *data, size_t len) {
    WebSerial.println(">>> Web Command Received");
    String command = "";
    for(int i = 0; i < len; i++) {
        command += char(data[i]);
    }
    command.trim();

    WebSerial.printf(">>> Processing: '%s'\n", command.c_str());

    // Handle legacy commands
    if (command == "ON") {
        // Note: LED control would need to be done via callback
        // statusLEDOn();
    } else if (command == "OFF") {
        // statusLEDOff();
    } else if (command == "serial-off") {
        // writeLogToSerial = false;
        WebSerial.closeAll();
        return;
    }
    
    // Handle single-character commands (existing serial commands)
    else if (command.length() == 1) {
        char singleChar = command.charAt(0);
        WebSerial.printf(">>> Single char command: '%c'\n", singleChar);
        if (webSerialCommandCallback) {
            webSerialCommandCallback(singleChar);
        }
    }
    
    // Handle multi-character commands (extended flash test commands)
    else if (command == "POST") {
        WebSerial.println(">>> Running Power-On Self Test...");
        if (webSerialExtendedCommandCallback) {
            webSerialExtendedCommandCallback("POST");
        }
    } else if (command == "DEEP") {
        WebSerial.println(">>> Running Deep Sector Validation...");
        if (webSerialExtendedCommandCallback) {
            webSerialExtendedCommandCallback("DEEP");
        }
    } else if (command == "STRESS") {
        WebSerial.println(">>> Running Stress Test (100 records)...");
        if (webSerialExtendedCommandCallback) {
            webSerialExtendedCommandCallback("STRESS");
        }
    } else if (command == "RECOVERY") {
        WebSerial.println(">>> Running Power-Loss Recovery Test...");
        if (webSerialExtendedCommandCallback) {
            webSerialExtendedCommandCallback("RECOVERY");
        }
    } else {
        WebSerial.printf(">>> Unknown command: '%s'\n", command.c_str());
    }
}

void NetworkManager::checkConnectivity() {        
    // Check connectivity if pipeline is backed up OR if forced for display testing
    const uint16_t pipelineBackedUpLength = 10;
    bool shouldCheckConnectivity = forceConnectivityCheckForDisplay || 
        (telemetryPipeline->getPipelineLength() > pipelineBackedUpLength && 
         telemetryPipeline->isPipelineDraining() == false &&
         millis() > lastCheckForInternetConnectivityAt + checkInternetConnectivityDutyCycle);
    
    forceConnectivityCheckForDisplay = false;

    if (shouldCheckConnectivity) {
        lastCheckForInternetConnectivityAt = millis();

        if (WiFi.status() == WL_CONNECTED) {
            // Store internet connectivity status for display
            lastInternetConnectivityStatus = isInternetAccessible();
            
            if (lastInternetConnectivityStatus) {
                // WiFi ok, internet ping success
            } else {
                // WiFi ok, ping fail, out of coverage
            }

            if (isScubaMosquittoBrokerAvailable()) {
                // Scuba MQTT Broker ping success
            } else {
                // WiFi ok, ping google ok, MQTT Broker fail
            }
        } else {
            // No WiFi - internet, DNS, and IP are all not accessible
            lastInternetConnectivityStatus = false;
            lastDNSConnectivityStatus = false;
            lastIPConnectivityStatus = false;
            // Do a manual wifi reconnect attempt - synchronous
            WiFi.reconnect();
        }
    }
}


bool NetworkManager::isInternetAccessible() {
    lastCheckForInternetConnectivityAt = millis();
    
    dns_clear_cache();
    // First try DNS resolution - ping google.com 
    lastDNSConnectivityStatus = Ping.ping(config.ping_name_target, maxPingAttempts);
    
    if (lastDNSConnectivityStatus) {
        lastIPConnectivityStatus = true;  // If DNS works, IP connectivity is also working
        return true;  // DNS and internet both working
    }
    
    // DNS failed, try direct IP ping as fallback
    // This helps distinguish DNS issues from full internet outage
    lastIPConnectivityStatus = Ping.ping(config.ping_ip_target, maxPingAttempts);

    return lastIPConnectivityStatus;
}

bool NetworkManager::isScubaMosquittoBrokerAvailable() {
    return privateMQTT->isConnected();
}

MQTTConnectionResult NetworkManager::publishMQTTTestMessageOnDutyCycle(const char* topic, uint32_t testPublishDutyCycle) {
    MQTTConnectionResult result = MQTTConnectionResult::UNDEFINED_ERROR;
    static uint32_t lastTestMessagePublishedAt = millis();
    if (millis() - lastTestMessagePublishedAt > testPublishDutyCycle) {
        char message[128];
        snprintf(message, sizeof(message), "[%lu] This is a test message from Lemon_V2 (%s)", 
                 millis(), privateMQTT->getEncryptionStatus());
        result = privateMQTT->publish(topic, message);
        lastTestMessagePublishedAt = millis();
    }
    return result;
}

char* NetworkManager::getMQTTPayloadBuffer() {
    return privateMQTT->getPayloadBuffer();
}

void NetworkManager::prepareNetworkForOTA() {
    privateMQTT->disconnect();
    
    if (ws) {
        ws->closeAll();
    }
    
    if (config.enableWebSerial) {
        WebSerial.closeAll();
    }
}

void NetworkManager::uploadOTABeginCallback() {
    // Enable OTA mode to suppress all normal display updates
    displayManager.setOTAMode(true);
    
    // Clear display first
    displayManager.clearDisplay();
    
    // Set large bold font and draw centered text
    displayManager.display.setFont(u8g2_font_ncenB14_tr);
    
    const char* line1 = "OTA Update";
    const char* line2 = "In Progress";
    
    int line1Width = displayManager.display.getUTF8Width(line1);
    int line2Width = displayManager.display.getUTF8Width(line2);
    
    int x1 = (256 - line1Width) / 2;
    int x2 = (256 - line2Width) / 2;
    
    displayManager.display.setDrawColor(1);  // White (draw)
    displayManager.display.drawUTF8(x1, 20, line1);  // First line at y=20
    displayManager.display.drawUTF8(x2, 38, line2);  // Second line at y=38 (more spacing)
    
    // Draw progress bar bounding box (moved down to y=50)
    int barWidth = 180;
    int barHeight = 8;
    int barX = (256 - barWidth) / 2;
    int barY = 50;
    displayManager.display.drawFrame(barX, barY, barWidth, barHeight);
    
    displayManager.display.sendBuffer();
    
    prepareNetworkForOTA();

    if (prepareEntireSystemForOTA)
        prepareEntireSystemForOTA();
}

void NetworkManager::uploadOTAProgressCallback(size_t progress, size_t total) {
    static int lastFillWidth = -1;
    
    // Skip if no total size available
    if (total == 0) {
        USB_SERIAL_PRINTF("OTA Progress: skipping, total=0\n");
        return;
    }
    
    USB_SERIAL_PRINTF("OTA Progress: %zu/%zu bytes\n", progress, total);
    
    // Progress bar dimensions - moved down to y=50, taller for better visibility
    int barWidth = 180;
    int barHeight = 8;
    int barX = (256 - barWidth) / 2;
    int barY = 50;
    
    // Calculate fill width based on actual progress ratio
    int fillWidth = ((barWidth - 2) * progress) / total;
    
    // Only update if we have at least 1 more pixel of progress bar to show
    if (fillWidth <= lastFillWidth) {
        return;
    }
    
    USB_SERIAL_PRINTF("OTA Progress: fillWidth: %d->%d\n", lastFillWidth, fillWidth);
    
    // Only clear and redraw the progress bar area, not entire screen  
    displayManager.display.setDrawColor(0);  // Black (erase)
    displayManager.display.drawBox(barX + 1, barY + 1, barWidth - 2, barHeight - 2);  // Clear progress area only
    
    displayManager.display.setDrawColor(1);  // White (draw)
    
    // Fill progress bar based on actual progress
    if (fillWidth > 0) {
        displayManager.display.drawBox(barX + 1, barY + 1, fillWidth, barHeight - 2);
    }
    
    // Use updateDisplayArea to only refresh the progress bar area
    int tileX = barX / 8;
    int tileY = barY / 8;  
    int tileWidth = (barWidth / 8) + 2;
    int tileHeight = 2;  // Cover progress bar only
    
    displayManager.display.updateDisplayArea(tileX, tileY, tileWidth, tileHeight);
    
    lastFillWidth = fillWidth;
}

void NetworkManager::uploadOTASucceededCallback() {
    USB_SERIAL_PRINTF("OTA upload succeeded, scheduling restart in 3 seconds\n");
    
    // Show 100% completion - use same dimensions as progress callback
    int barWidth = 180;
    int barHeight = 8;
    int barX = (256 - barWidth) / 2;
    int barY = 50;
    
    // Fill progress bar to 100%
    displayManager.display.setDrawColor(0);  // Black (erase)
    displayManager.display.drawBox(barX + 1, barY + 1, barWidth - 2, barHeight - 2);  // Clear
    displayManager.display.setDrawColor(1);  // White (draw)
    displayManager.display.drawBox(barX + 1, barY + 1, barWidth - 2, barHeight - 2);  // Fill 100%
    
    // Update display
    int tileX = barX / 8;
    int tileY = barY / 8;  
    int tileWidth = (barWidth / 8) + 2;
    int tileHeight = 2;
    displayManager.display.updateDisplayArea(tileX, tileY, tileWidth, tileHeight);
    
    restartAfterGoodOTAUpdateAt = millis() + 3000;
    restartForGoodOTAScheduled = true;
}

// WiFi event handlers
void NetworkManager::wifiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    strcpy(IPBuffer, wait_ip_label);
}

void NetworkManager::wifiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
    strcpy(IPBuffer, WiFi.localIP().toString().c_str());
    strcpy(IPLocalGateway, WiFi.gatewayIP().toString().c_str());
    strcpy(WiFiSSID, WiFi.SSID().c_str());

    bool isDevNet = isDevNetworkCallback ? isDevNetworkCallback() : false;
    privateMQTT->setUsingDevNetwork(isDevNet);
}

void NetworkManager::wifiLostIP(WiFiEvent_t event, WiFiEventInfo_t info) {
    strcpy(IPBuffer, lost_ip_label);
    strcpy(IPLocalGateway, "");
    strcpy(WiFiSSID, WiFi.SSID().c_str());

    privateMQTT->setUsingDevNetwork(false);
}

void NetworkManager::wifiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    strcpy(IPBuffer, no_wifi_label);
    strcpy(IPLocalGateway, "");
    strcpy(WiFiSSID, "");

    privateMQTT->setUsingDevNetwork(false);
}

// Static callback wrappers
void NetworkManager::wifiStationConnectedWrapper(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (instance) instance->wifiStationConnected(event, info);
}

void NetworkManager::wifiGotIPWrapper(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (instance) instance->wifiGotIP(event, info);
}

void NetworkManager::wifiLostIPWrapper(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (instance) instance->wifiLostIP(event, info);
}

void NetworkManager::wifiStationDisconnectedWrapper(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (instance) instance->wifiStationDisconnected(event, info);
}

// OTA callback wrappers
void NetworkManager::uploadOTABeginCallbackWrapper(AsyncElegantOtaClass* originator) {
    if (instance) instance->uploadOTABeginCallback();
}

void NetworkManager::uploadOTAProgressCallbackWrapper(AsyncElegantOtaClass* originator, size_t progress, size_t total) {
    if (instance) instance->uploadOTAProgressCallback(progress, total);
}

void NetworkManager::uploadOTASucceededCallbackWrapper(AsyncElegantOtaClass* originator) {
    if (instance) instance->uploadOTASucceededCallback();
}