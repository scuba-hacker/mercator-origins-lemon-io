#pragma once

#include <PicoMQTT.h>
#include <AsyncMqttClient.h>
#include <WiFi.h>
#include <atomic>

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

    // QoS 1 acknowledgment ownership for the TLS path. The awaited key binds
    // a packet ID to the client that issued it. Timed-out IDs are quarantined
    // for that client, so a delayed callback can never satisfy a later attempt
    // after the 16-bit ID is reused. Atomics bridge the async TCP task and main.
    enum class AckSource : uint8_t { NONE = 0, LOCAL = 1, REMOTE = 2 };
    std::atomic<uint32_t> awaitedAckKey;
    std::atomic<uint32_t> satisfiedAckKey;
    uint8_t quarantinedPacketIds[2][8192];
    static const uint32_t PUBACK_TIMEOUT_MS = 3000;

    static uint32_t ackKey(AckSource source, uint16_t packetId) {
        return (static_cast<uint32_t>(source) << 16) | packetId;
    }
    void resetAckAttempt() { awaitedAckKey.store(0); satisfiedAckKey.store(0); }
    void armAckAttempt(AckSource source, uint16_t packetId) {
        satisfiedAckKey.store(0);
        awaitedAckKey.store(ackKey(source, packetId));
    }
    void noteAck(AckSource source, uint16_t packetId) {
        uint32_t key = ackKey(source, packetId);
        if (awaitedAckKey.load() == key) {
            satisfiedAckKey.store(key);
        }
    }
    bool ackAttemptSatisfied() const {
        uint32_t awaited = awaitedAckKey.load();
        return awaited != 0 && satisfiedAckKey.load() == awaited;
    }
    bool packetIdQuarantined(AckSource source, uint16_t packetId) const;
    void quarantinePacketId(AckSource source, uint16_t packetId);
    void clearPacketIdQuarantine(AckSource source, uint16_t packetId);
    
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
    MercatorMQTT(const MQTTConfig& config, uint32_t minDutyMs = 0, int16_t bufferSize = 3072);
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
    // canUpload() without the duty-cycle throttle: true whenever the broker is
    // reachable and uploads are enabled. Used for storage routing decisions
    // (flash vs PSRAM), where a momentary throttle must not count as offline.
    bool isUplinkUsable() const;
    
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

#ifdef TESTING_MODE
    /** @brief Deterministic checks of the acknowledgment transaction:
     *  retained/wrapped IDs, mismatched acks, acks with no attempt armed,
     *  and the fresh-match success path. Prints PASS/FAIL per case. */
    bool runAckStateTests();
#endif
};
