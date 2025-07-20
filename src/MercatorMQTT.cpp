#include "MercatorMQTT.h"
#include <cstring>

MercatorMQTT::MercatorMQTT(const MQTTConfig& config, uint32_t minDutyMs, int16_t bufferSize)
    : localClient(config.local_host, config.local_port, config.client_id, config.username, config.password)
    , remoteClient(config.remote_host, config.remote_port, config.client_id, config.username, config.password)
    , uploadMinDutyMs(minDutyMs)
    , lastUploadAt(0)
    , payloadSize(bufferSize)
    , payloadBuffer(new char[bufferSize])
    , usingDevNetwork(false)
    , enableConnect(true)
    , enableUpload(true)
{
}

MercatorMQTT::~MercatorMQTT() {
    delete[] payloadBuffer;
}

void MercatorMQTT::setConnectionCallbacks(std::function<void()> localConnected,
                                         std::function<void()> localDisconnected,
                                         std::function<void()> remoteConnected,
                                         std::function<void()> remoteDisconnected) {
    localClient.connected_callback = localConnected;
    localClient.disconnected_callback = localDisconnected;
    remoteClient.connected_callback = remoteConnected;
    remoteClient.disconnected_callback = remoteDisconnected;
}

void MercatorMQTT::begin() {
    if (enableUpload) {
        localClient.begin();
        remoteClient.begin();
    }
}

void MercatorMQTT::loop() {
    if (enableUpload) {
        getActiveClient()->loop();
    }
}

void MercatorMQTT::disconnect() {
    localClient.disconnect();
    remoteClient.disconnect();
}

bool MercatorMQTT::isConnected() const {
    if (!enableConnect) return false;
    return getActiveClient()->connected();
}

bool MercatorMQTT::canUpload() const {
    if (!enableUpload || !enableConnect) return false;
    if (WiFi.status() != WL_CONNECTED) return false;
    if (millis() < lastUploadAt + uploadMinDutyMs) return false;
    return isConnected();
}

PicoMQTT::Client* MercatorMQTT::getActiveClient() {
    return usingDevNetwork ? &localClient : &remoteClient;
}

PicoMQTT::Client* MercatorMQTT::getActiveClient() const {
    return usingDevNetwork ? const_cast<PicoMQTT::Client*>(&localClient) : const_cast<PicoMQTT::Client*>(&remoteClient);
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
    
    PicoMQTT::Client* client = getActiveClient();
    if (!client->connected()) {
        return MQTTConnectionResult::CLIENT_CONNECT_ERROR;
    }
    
    lastUploadAt = millis();
    bool result = client->publish(topic, payload, qos);
    
    if (result) {
        return MQTTConnectionResult::SUCCESS;
    } else {
        return MQTTConnectionResult::SEND_ERROR;
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