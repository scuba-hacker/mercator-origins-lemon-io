#include "MercatorMQTT.h"
#include "SerialConfig.h"
#include <cstring>

MercatorMQTT::MercatorMQTT(const MQTTConfig& config, uint32_t minDutyMs, int16_t bufferSize)
    : config(config)
    , localClient(config.local_host, config.local_port, config.client_id, config.username, config.password)
    , remoteClient(config.remote_host, config.remote_port, config.client_id, config.username, config.password)
    , uploadMinDutyMs(minDutyMs)
    , lastUploadAt(0)
    , payloadSize(bufferSize)
    , payloadBuffer(new char[bufferSize])
    , awaitedAckKey(0)
    , satisfiedAckKey(0)
    , usingDevNetwork(false)
    , enableConnect(true)
    , enableUpload(true)
    , useTLS(config.enable_tls)
{
    memset(quarantinedPacketIds, 0, sizeof(quarantinedPacketIds));
    if (useTLS) {
        // Configure AsyncMqttClient for TLS (port 8883 enables TLS automatically)
        localAsyncClient.setServer(config.local_host, config.local_port);
        localAsyncClient.setClientId(config.client_id);
        localAsyncClient.setCredentials(config.username, config.password);

        remoteAsyncClient.setServer(config.remote_host, config.remote_port);
        remoteAsyncClient.setClientId(config.client_id);
        remoteAsyncClient.setCredentials(config.username, config.password);

        // PUBACK tracking: publish() reports SUCCESS for QoS 1 only once the
        // broker has acknowledged the exact packet id of the ARMED attempt
        // (delete-on-ack upstream). See the transaction notes in the header.
        localAsyncClient.onPublish([this](uint16_t packetId) {
            noteAck(AckSource::LOCAL, packetId);
        });
        remoteAsyncClient.onPublish([this](uint16_t packetId) {
            noteAck(AckSource::REMOTE, packetId);
        });
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

bool MercatorMQTT::packetIdQuarantined(AckSource source, uint16_t packetId) const {
    if (source == AckSource::NONE || packetId == 0) {
        return false;
    }
    uint8_t source_index = static_cast<uint8_t>(source) - 1;
    return (quarantinedPacketIds[source_index][packetId >> 3] &
            static_cast<uint8_t>(1u << (packetId & 7))) != 0;
}

void MercatorMQTT::quarantinePacketId(AckSource source, uint16_t packetId) {
    if (source == AckSource::NONE || packetId == 0) {
        return;
    }
    uint8_t source_index = static_cast<uint8_t>(source) - 1;
    quarantinedPacketIds[source_index][packetId >> 3] |=
        static_cast<uint8_t>(1u << (packetId & 7));
}

void MercatorMQTT::clearPacketIdQuarantine(AckSource source, uint16_t packetId) {
    if (source == AckSource::NONE || packetId == 0) {
        return;
    }
    uint8_t source_index = static_cast<uint8_t>(source) - 1;
    quarantinedPacketIds[source_index][packetId >> 3] &=
        static_cast<uint8_t>(~(1u << (packetId & 7)));
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
        AckSource ack_source = usingDevNetwork ? AckSource::LOCAL : AckSource::REMOTE;
        if (!client->connected()) {
            // Try to connect if not connected
            client->connect();
            return MQTTConnectionResult::CLIENT_CONNECT_ERROR;
        }
        
        // Invalidate any previous acknowledgment state BEFORE queueing: a
        // retained ack (including one whose 16-bit packet ID has wrapped
        // around to match) must never be able to satisfy this new attempt.
        resetAckAttempt();

        uint16_t packetId = client->publish(topic, qos, false, payload);
        if (packetId == 0) {
            return MQTTConnectionResult::SEND_ERROR;
        }

        if (qos == 0) {
            // No acknowledgement exists at QoS 0 - local enqueue is all there is.
            lastUploadAt = millis();
            return MQTTConnectionResult::SUCCESS;
        }

        if (packetIdQuarantined(ack_source, packetId)) {
            USB_SERIAL_PRINTF("MercatorMQTT::publish() - packet %u is quarantined after an earlier failed attempt\n",
                              packetId);
            resetAckAttempt();
            client->disconnect(true);
            client->clearQueue();
            return MQTTConnectionResult::SEND_ERROR;
        }

        // QoS 1: a non-zero packet id only means "queued locally". The caller
        // deletes the record on SUCCESS, so arm the attempt and wait for a
        // FRESH broker PUBACK matching this exact packet id (delivered on the
        // async TCP task via onPublish). An ack arriving in the microseconds
        // before arming is missed and leads to a timeout+retry (a harmless
        // duplicate), never to a false success.
        armAckAttempt(ack_source, packetId);
        uint32_t waitStart = millis();
        while (!ackAttemptSatisfied()) {
            if (millis() - waitStart >= PUBACK_TIMEOUT_MS || !client->connected()) {
                USB_SERIAL_PRINTF("MercatorMQTT::publish() - no PUBACK for packet %u (%s)\n",
                                  packetId,
                                  client->connected() ? "timeout" : "disconnected");
                quarantinePacketId(ack_source, packetId);
                resetAckAttempt();  // invalidate BEFORE the transport reset
                if (client->connected()) {
                    // AsyncMqttClient holds the unacked QoS 1 packet at the
                    // head of its queue; retrying on the same session would
                    // stack a new packet behind the blocked head on every
                    // retry until memory runs out. The disconnect MUST be
                    // forced: a graceful disconnect(false) only queues a
                    // DISCONNECT packet BEHIND the blocked publish (never
                    // sent) and wedges the client in DISCONNECTING, where
                    // connect() refuses to run. disconnect(true) closes the
                    // TCP session immediately. The explicit clearQueue()
                    // below removes the blocked publish before reconnect.
                    client->disconnect(true);
                }
                if (!client->clearQueue()) {
                    USB_SERIAL_PRINTLN("MercatorMQTT::publish() - warning: AsyncMqttClient queue could not be cleared");
                }
                return MQTTConnectionResult::SEND_ERROR;
            }
            delay(1);  // yield; PUBACK arrives on the async TCP task
        }

        resetAckAttempt();
        lastUploadAt = millis();
        return MQTTConnectionResult::SUCCESS;
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

#ifdef TESTING_MODE
bool MercatorMQTT::runAckStateTests() {
    bool all_passed = true;
    auto check = [&](const char* name, bool passed) {
        USB_SERIAL_PRINTF("ACK-TEST %-52s %s\n", name, passed ? "PASS" : "FAIL");
        if (!passed) all_passed = false;
    };

    // Case A: retained state cannot satisfy a new transaction with the same ID.
    resetAckAttempt();
    armAckAttempt(AckSource::LOCAL, 100);
    noteAck(AckSource::LOCAL, 100);
    check("previous attempt satisfied by its fresh ack", ackAttemptSatisfied());
    resetAckAttempt();
    armAckAttempt(AckSource::LOCAL, 100);
    check("wrapped id NOT satisfied by retained state", !ackAttemptSatisfied());

    // Case B: mismatched IDs and callbacks from the other client are rejected.
    resetAckAttempt();
    armAckAttempt(AckSource::LOCAL, 7);
    noteAck(AckSource::LOCAL, 9);
    check("mismatched ack rejected", !ackAttemptSatisfied());
    noteAck(AckSource::REMOTE, 7);
    check("matching id from wrong client rejected", !ackAttemptSatisfied());

    // Case C: an ack with no attempt armed is ignored, and does not
    // pre-satisfy the next attempt (the missed-ack race resolves to a
    // timeout/retry, never a false success).
    resetAckAttempt();
    noteAck(AckSource::LOCAL, 5);
    armAckAttempt(AckSource::LOCAL, 5);
    check("ack before arming ignored (leads to retry)", !ackAttemptSatisfied());

    // Case D: the fresh matching ack for the armed attempt succeeds.
    noteAck(AckSource::LOCAL, 5);
    check("fresh matching ack accepted", ackAttemptSatisfied());

    // Case E: invalidation on timeout clears the transaction and quarantines
    // the ID. A delayed same-ID PUBACK cannot own a future attempt because the
    // publish path refuses that ID before arming it.
    const uint16_t delayed_id = 77;
    resetAckAttempt();
    armAckAttempt(AckSource::LOCAL, delayed_id);
    quarantinePacketId(AckSource::LOCAL, delayed_id);
    resetAckAttempt();
    noteAck(AckSource::LOCAL, delayed_id);
    check("invalidated attempt reports unsatisfied", !ackAttemptSatisfied());
    check("timed-out id quarantined against delayed same-id ack",
          packetIdQuarantined(AckSource::LOCAL, delayed_id));
    check("quarantine remains client-scoped",
          !packetIdQuarantined(AckSource::REMOTE, delayed_id));
    clearPacketIdQuarantine(AckSource::LOCAL, delayed_id);

    resetAckAttempt();
    USB_SERIAL_PRINTF("=== MQTT ack transaction tests: %s ===\n", all_passed ? "PASSED" : "FAILED");
    return all_passed;
}
#endif // TESTING_MODE

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
