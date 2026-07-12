#include "MercatorMQTT.h"
#include <cstring>

MercatorMQTT::MercatorMQTT(const MQTTConfig& config, uint32_t minDutyMs, int16_t bufferSize)
    : config(config)
    , localClient(config.local_host, config.local_port, config.client_id, config.username, config.password)
    , remoteClient(config.remote_host, config.remote_port, config.client_id, config.username, config.password)
    , uploadMinDutyMs(minDutyMs)
    , lastUploadAt(0)
    , payloadSize(bufferSize)
    , payloadBuffer(new char[bufferSize])
    , usingDevNetwork(false)
    , enableConnect(true)
    , enableUpload(true)
    , useTLS(config.enable_tls)
{
    if (useTLS) {
        // Configure AsyncMqttClient for TLS (port 8883 enables TLS automatically)
        localAsyncClient.setServer(config.local_host, config.local_port);
        localAsyncClient.setClientId(config.client_id);
        localAsyncClient.setCredentials(config.username, config.password);
        
        remoteAsyncClient.setServer(config.remote_host, config.remote_port);
        remoteAsyncClient.setClientId(config.client_id);
        remoteAsyncClient.setCredentials(config.username, config.password);
    }
}

MercatorMQTT::~MercatorMQTT() {
    delete[] payloadBuffer;
}

void MercatorMQTT::setConnectionCallbacks(std::function<void()> localConnected,
                                         std::function<void(AsyncMqttClientDisconnectReason reason)> localAsyncMqttDisconnected,
                                         std::function<void()> remoteConnected,
                                         std::function<void(AsyncMqttClientDisconnectReason reason)> remoteAsyncMqttDisconnected,
                                         std::function<void()> localPicoMqttConnected,
                                         std::function<void()> remotePicoMqttConnected                                        
                                        ) {
    if (useTLS) {
        // Store callbacks for AsyncMqttClient
        localConnectedCallback = localConnected;
        localDisconnectedCallback = localAsyncMqttDisconnected;
        remoteConnectedCallback = remoteConnected;
        remoteDisconnectedCallback = remoteAsyncMqttDisconnected;
        
        localAsyncClient.onConnect([this](bool sessionPresent) { if (localConnectedCallback) localConnectedCallback(); });
        localAsyncClient.onDisconnect([this](AsyncMqttClientDisconnectReason reason) { if (localDisconnectedCallback) localDisconnectedCallback(reason); });
        remoteAsyncClient.onConnect([this](bool sessionPresent) { if (remoteConnectedCallback) remoteConnectedCallback(); });
        remoteAsyncClient.onDisconnect([this](AsyncMqttClientDisconnectReason reason) { if (remoteDisconnectedCallback) remoteDisconnectedCallback(reason); });
    } else {
        localClient.connected_callback = localConnected;
        localClient.disconnected_callback = localPicoMqttConnected;
        remoteClient.connected_callback = remoteConnected;
        remoteClient.disconnected_callback = remotePicoMqttConnected;
    }
}

void MercatorMQTT::begin() {
    if (enableUpload) {
        if (useTLS) {
            // AsyncMqttClient connects automatically when needed
        } else {
            localClient.begin();
            remoteClient.begin();
        }
    }
}

void MercatorMQTT::loop() {
    if (enableUpload) {
        if (!useTLS) {
            getActivePicoClient()->loop();
        }
        // AsyncMqttClient handles its own loop internally
    }
}

void MercatorMQTT::disconnect() {
    if (useTLS) {
        localAsyncClient.disconnect();
        remoteAsyncClient.disconnect();
    } else {
        localClient.disconnect();
        remoteClient.disconnect();
    }
}

bool MercatorMQTT::isConnected() const {
    if (!enableConnect) return false;
    if (useTLS) {
        AsyncMqttClient* client = const_cast<AsyncMqttClient*>(usingDevNetwork ? &localAsyncClient : &remoteAsyncClient);
        return client->connected();
    } else {
        return getActivePicoClient()->connected();
    }
}

bool MercatorMQTT::canUpload() const {
    if (millis() < lastUploadAt + uploadMinDutyMs) return false;
    return isUplinkUsable();
}

bool MercatorMQTT::isUplinkUsable() const {
    if (!enableUpload || !enableConnect) return false;
    if (WiFi.status() != WL_CONNECTED) return false;
    return isConnected();
}

PicoMQTT::Client* MercatorMQTT::getActivePicoClient() {
    return usingDevNetwork ? &localClient : &remoteClient;
}

PicoMQTT::Client* MercatorMQTT::getActivePicoClient() const {
    return usingDevNetwork ? const_cast<PicoMQTT::Client*>(&localClient) : const_cast<PicoMQTT::Client*>(&remoteClient);
}

AsyncMqttClient* MercatorMQTT::getActiveAsyncClient() {
    return usingDevNetwork ? &localAsyncClient : &remoteAsyncClient;
}

bool MercatorMQTT::isDevNetwork() const {
    return usingDevNetwork;
}

MQTTConnectionResult MercatorMQTT::publish(const char* topic, const char* payload, int qos) {
    if (!enableUpload) {
        return MQTTConnectionResult::NOT_ENABLED;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        return MQTTConnectionResult::WIFI_NOT_CONNECTED;
    }
    
    if (useTLS) {
        AsyncMqttClient* client = getActiveAsyncClient();
        if (!client->connected()) {
            // Try to connect if not connected
            client->connect();
            return MQTTConnectionResult::CLIENT_CONNECT_ERROR;
        }
        
        uint16_t packetId = client->publish(topic, qos, false, payload);
        if (packetId != 0) {
            lastUploadAt = millis();  // Only update timestamp on successful upload
            return MQTTConnectionResult::SUCCESS;
        } else {
            return MQTTConnectionResult::SEND_ERROR;
        }
    } else {
        PicoMQTT::Client* client = getActivePicoClient();
        if (!client->connected()) {
            return MQTTConnectionResult::CLIENT_CONNECT_ERROR;
        }
        
        bool result = client->publish(topic, payload, qos);
        if (result) {
            lastUploadAt = millis();  // Only update timestamp on successful upload
            return MQTTConnectionResult::SUCCESS;
        } else {
            return MQTTConnectionResult::SEND_ERROR;
        }
    }
}

void MercatorMQTT::setEnabled(bool connectEnabled, bool uploadEnabled) {
    enableConnect = connectEnabled;
    enableUpload = uploadEnabled;
}

void MercatorMQTT::updateNetworkStatus(const char* gateway, const char* ssid) {
    // Network status is updated from main.cpp which has access to the config
    // The main.cpp will determine if it's dev network and call setUsingDevNetwork
}

void MercatorMQTT::setUsingDevNetwork(bool isDevNetwork) {
    usingDevNetwork = isDevNetwork;
}

const char* MercatorMQTT::getDisconnectReason(AsyncMqttClientDisconnectReason reason)
{
    switch (reason) {
        case AsyncMqttClientDisconnectReason::TCP_DISCONNECTED:
            return "TCP Disconnected";
        case AsyncMqttClientDisconnectReason::MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
            return "MQTT Unacceptable Protocol Version";
        case AsyncMqttClientDisconnectReason::MQTT_IDENTIFIER_REJECTED:
            return "MQTT Identifier Rejected";
        case AsyncMqttClientDisconnectReason::MQTT_SERVER_UNAVAILABLE:
            return "MQTT Server Unavailable";
        case AsyncMqttClientDisconnectReason::MQTT_MALFORMED_CREDENTIALS:
            return "MQTT Malformed Credentials";
        case AsyncMqttClientDisconnectReason::MQTT_NOT_AUTHORIZED:
            return "MQTT Not Authorized";
        case AsyncMqttClientDisconnectReason::ESP8266_NOT_ENOUGH_SPACE:
            return "ESP8266 Not Enough Space";
        case AsyncMqttClientDisconnectReason::TLS_BAD_FINGERPRINT:
            return "TLS Bad Fingerprint";
        default:
            return "Unknown Disconnect Reason";
    }
}