#pragma once

#include <PicoMQTT.h>
#include <AsyncMqttClient.h>
#include <WiFi.h>

enum class MQTTConnectionResult {
    SUCCESS = 0,
    WIFI_NOT_CONNECTED,
    CLIENT_CONNECT_ERROR,
    SEND_ERROR,
    NOT_ENABLED,
    UNDEFINED_ERROR
};
struct MQTTConfig {
    const char* local_host;
    int local_port;
    const char* remote_host;
    int remote_port;
    const char* client_id;
    const char* username;
    const char* password;
    const char* local_gateway;
    const char* dev_ssid;
    bool enable_tls;
};

class MercatorMQTT {
private:
    // Configuration
    MQTTConfig config;
    
    // PicoMQTT clients (for non-TLS)
    PicoMQTT::Client localClient;
    PicoMQTT::Client remoteClient;
    
    // AsyncMqttClient clients (for TLS)
    AsyncMqttClient localAsyncClient;
    AsyncMqttClient remoteAsyncClient;
    
    uint32_t uploadMinDutyMs;
    uint32_t lastUploadAt;
    const int16_t payloadSize;
    char* payloadBuffer;
    
    bool usingDevNetwork;
    bool enableConnect;
    bool enableUpload;
    bool useTLS;
    
    // Callback storage for AsyncMqttClient
    std::function<void()> localConnectedCallback;
    std::function<void(AsyncMqttClientDisconnectReason)> localDisconnectedCallback;
    std::function<void()> remoteConnectedCallback;
    std::function<void(AsyncMqttClientDisconnectReason)> remoteDisconnectedCallback;

    PicoMQTT::Client* getActivePicoClient();
    PicoMQTT::Client* getActivePicoClient() const;
    AsyncMqttClient* getActiveAsyncClient();
    bool isDevNetwork() const;
    
public:
    MercatorMQTT(const MQTTConfig& config, uint32_t minDutyMs = 0, int16_t bufferSize = 2560);
    ~MercatorMQTT();

    void setConnectionCallbacks(std::function<void()> localConnected,
                                std::function<void(AsyncMqttClientDisconnectReason reason)> localAsyncMqttDisconnected,
                                std::function<void()> remoteConnected,
                                std::function<void(AsyncMqttClientDisconnectReason reason)> remoteAsyncMqttDisconnected,
                                std::function<void()> localPicoMqttConnected,
                                std::function<void()> remotePicoMqttConnected);

    void begin();
    void loop();
    void disconnect();
    
    bool isConnected() const;
    bool canUpload() const;
    
    MQTTConnectionResult publish(const char* topic, const char* payload, int qos = 1);
    
    void setEnabled(bool connectEnabled, bool uploadEnabled);
    void updateNetworkStatus(const char* gateway, const char* ssid);
    void setUsingDevNetwork(bool isDevNetwork);
    
    uint32_t getLastUploadTime() const { return lastUploadAt; }
    char* getPayloadBuffer() { return payloadBuffer; }
    int16_t getPayloadSize() const { return payloadSize; }
    bool isTLSEnabled() const { return useTLS; }
    const char* getEncryptionStatus() const { return isTLSEnabled() ? "TLS" : "Not Encrypted"; }
    const char* getCurrentHostname() const { return usingDevNetwork ? config.local_host : config.remote_host; }

    static const char* resultToText(MQTTConnectionResult result)
    {
        switch (result) {
            case MQTTConnectionResult::SUCCESS:            return "SUCCESS";
            case MQTTConnectionResult::WIFI_NOT_CONNECTED: return "WIFI_NOT_CONNECTED";
            case MQTTConnectionResult::CLIENT_CONNECT_ERROR: return "CLIENT_CONNECT_ERROR";
            case MQTTConnectionResult::SEND_ERROR:         return "SEND_ERROR";
            case MQTTConnectionResult::NOT_ENABLED:        return "NOT_ENABLED";
            case MQTTConnectionResult::UNDEFINED_ERROR:    return "UNDEFINED_ERROR";
            default:                                       return "UNKNOWN";
        }
    }

    static const char* getDisconnectReason(AsyncMqttClientDisconnectReason reason);
};