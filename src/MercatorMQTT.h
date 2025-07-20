#pragma once

#include <PicoMQTT.h>
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
};

class MercatorMQTT {
private:
    PicoMQTT::Client localClient;
    PicoMQTT::Client remoteClient;
    
    uint32_t uploadMinDutyMs;
    uint32_t lastUploadAt;
    const int16_t payloadSize;
    char* payloadBuffer;
    
    bool usingDevNetwork;
    bool enableConnect;
    bool enableUpload;
    
    PicoMQTT::Client* getActiveClient();
    PicoMQTT::Client* getActiveClient() const;
    bool isDevNetwork() const;
    
public:
    MercatorMQTT(const MQTTConfig& config, uint32_t minDutyMs = 50, int16_t bufferSize = 2560);
    ~MercatorMQTT();
    
    void setConnectionCallbacks(std::function<void()> localConnected,
                               std::function<void()> localDisconnected,
                               std::function<void()> remoteConnected,
                               std::function<void()> remoteDisconnected);
    
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
};