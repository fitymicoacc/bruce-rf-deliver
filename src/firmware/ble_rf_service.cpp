#include "ble_rf_service.h"
#include <globals.h>

BleRfService bleRfService;

// ===== Separate callback classes (Bruce pattern from ble_common.cpp) =====

class RfServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        bleRfService.connected = true;
        Serial.println("[BLE] Client connected");
    }
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        bleRfService.connected = false;
        Serial.printf("[BLE] Client disconnected (reason=%d)\n", reason);
        NimBLEDevice::startAdvertising();
    }
};

class RfCmdCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
        NimBLEAttValue val = pChar->getValue();
        const uint8_t* data = val.data();
        size_t len = val.length();
        if (len < 1) return;

        uint8_t cmdByte = data[0];

        switch (cmdByte) {
            case CMD_START_LISTEN:
                if (len >= 5) {
                    bleRfService._pendingCmd.id = CMD_START_LISTEN;
                    memcpy(&bleRfService._pendingCmd.freq, &data[1], sizeof(float));
                    bleRfService._hasCmd = true;
                    Serial.printf("[BLE] CMD: START_LISTEN freq=%.2f\n", bleRfService._pendingCmd.freq);
                }
                break;

            case CMD_STOP:
                bleRfService._pendingCmd.id = CMD_STOP;
                bleRfService._hasCmd = true;
                Serial.println("[BLE] CMD: STOP");
                break;

            case CMD_PLAY:
                if (len >= 17) {
                    bleRfService._pendingCmd.id = CMD_PLAY;
                    size_t off = 1;
                    bleRfService._pendingCmd.signal.protocol = data[off]; off += 1;
                    memcpy(&bleRfService._pendingCmd.signal.key, &data[off], sizeof(uint64_t)); off += 8;
                    bleRfService._pendingCmd.signal.bits = data[off]; off += 1;
                    memcpy(&bleRfService._pendingCmd.signal.pulseLength, &data[off], sizeof(uint16_t)); off += 2;
                    memcpy(&bleRfService._pendingCmd.signal.freq, &data[off], sizeof(float));
                    bleRfService._hasCmd = true;
                    Serial.printf("[BLE] CMD: PLAY proto=%d key=0x%llX bits=%d pulse=%d freq=%.2f\n",
                        bleRfService._pendingCmd.signal.protocol,
                        (unsigned long long)bleRfService._pendingCmd.signal.key,
                        bleRfService._pendingCmd.signal.bits,
                        bleRfService._pendingCmd.signal.pulseLength,
                        bleRfService._pendingCmd.signal.freq);
                }
                break;

            case CMD_PING:
                bleRfService._pendingCmd.id = CMD_PING;
                bleRfService._hasCmd = true;
                Serial.println("[BLE] CMD: PING");
                break;

            case CMD_SCAN_RANGES:
                if (len >= 4) {
                    bleRfService._pendingCmd.id = CMD_SCAN_RANGES;
                    bleRfService._pendingCmd.scan.rangeMode = true;
                    size_t off = 1;
                    memcpy(&bleRfService._pendingCmd.scan.dwellMs, &data[off], sizeof(uint16_t)); off += 2;
                    bleRfService._pendingCmd.scan.rangeCount = data[off]; off += 1;
                    if (bleRfService._pendingCmd.scan.rangeCount > MAX_SCAN_RANGES)
                        bleRfService._pendingCmd.scan.rangeCount = MAX_SCAN_RANGES;
                    size_t needed = off + bleRfService._pendingCmd.scan.rangeCount * 12;
                    if (len >= needed) {
                        for (uint8_t i = 0; i < bleRfService._pendingCmd.scan.rangeCount; i++) {
                            memcpy(&bleRfService._pendingCmd.scan.ranges[i].start, &data[off], sizeof(float)); off += 4;
                            memcpy(&bleRfService._pendingCmd.scan.ranges[i].end, &data[off], sizeof(float)); off += 4;
                            memcpy(&bleRfService._pendingCmd.scan.ranges[i].step, &data[off], sizeof(float)); off += 4;
                        }
                        bleRfService._hasCmd = true;
                        Serial.printf("[BLE] CMD: SCAN_RANGES count=%d dwell=%d\n",
                            bleRfService._pendingCmd.scan.rangeCount,
                            bleRfService._pendingCmd.scan.dwellMs);
                    }
                }
                break;

            case CMD_SCAN_LIST:
                if (len >= 4) {
                    bleRfService._pendingCmd.id = CMD_SCAN_LIST;
                    bleRfService._pendingCmd.scan.rangeMode = false;
                    size_t off = 1;
                    memcpy(&bleRfService._pendingCmd.scan.dwellMs, &data[off], sizeof(uint16_t)); off += 2;
                    bleRfService._pendingCmd.scan.freqCount = data[off]; off += 1;
                    if (bleRfService._pendingCmd.scan.freqCount > MAX_SCAN_FREQS)
                        bleRfService._pendingCmd.scan.freqCount = MAX_SCAN_FREQS;
                    size_t needed = off + bleRfService._pendingCmd.scan.freqCount * 4;
                    if (len >= needed) {
                        for (uint8_t i = 0; i < bleRfService._pendingCmd.scan.freqCount; i++) {
                            memcpy(&bleRfService._pendingCmd.scan.freqs[i], &data[off], sizeof(float));
                            off += 4;
                        }
                        bleRfService._hasCmd = true;
                        Serial.printf("[BLE] CMD: SCAN_LIST count=%d dwell=%d\n",
                            bleRfService._pendingCmd.scan.freqCount,
                            bleRfService._pendingCmd.scan.dwellMs);
                    }
                }
                break;

            default:
                Serial.printf("[BLE] Unknown command: 0x%02X\n", cmdByte);
                break;
        }
    }
};

// ===== Public API =====

bool BleRfService::init() {
    if (pServer != nullptr) {
        // Re-entering capture mode: BLE stack already initialized.
        // GATT services are still registered — just restart advertising.
        // This avoids BlueZ GATT cache staleness on Linux after deinit/reinit.
        connected = false;
        _hasCmd = false;

        NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
        pAdv->start();

        BLEConnected = true;

        Serial.println("[BLE] Re-advertising as Bruce-RF (stack preserved)");
        return true;
    }

    // First-time initialization
    NimBLEDevice::init("Bruce-RF");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setMTU(517);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new RfServerCallbacks());

    NimBLEService* pService = pServer->createService(BLE_RF_SERVICE_UUID);

    pCmdChar = pService->createCharacteristic(
        BLE_RF_CMD_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pCmdChar->setCallbacks(new RfCmdCallbacks());

    pSignalChar = pService->createCharacteristic(
        BLE_RF_SIGNAL_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );

    pStatusChar = pService->createCharacteristic(
        BLE_RF_STATUS_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ
    );

    // Initial status
    uint8_t initStatus[STATUS_PACKET_SIZE] = { DEV_IDLE, ERR_NONE };
    pStatusChar->setValue(initStatus, STATUS_PACKET_SIZE);

    pService->start();

    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(BLE_RF_SERVICE_UUID);
    pAdv->setName("Bruce-RF");
    pAdv->start();

    connected = false;
    _hasCmd = false;

    BLEConnected = true;

    Serial.println("[BLE] Advertising as Bruce-RF");
    return true;
}

void BleRfService::deinit() {
    // Don't tear down BLE stack — preserve GATT database for reconnect.
    // Just disconnect clients and stop advertising.
    // This prevents BlueZ GATT cache staleness and avoids deinit() crashes.

    if (pServer) {
        // Disconnect any connected client so desktop gets gattserverdisconnected
        if (connected) {
            uint16_t count = pServer->getConnectedCount();
            for (int i = count - 1; i >= 0; i--) {
                uint16_t connHandle = pServer->getPeerInfo(i).getConnHandle();
                pServer->disconnect(connHandle);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        pServer->getAdvertising()->stop();
    }

    connected = false;
    _hasCmd = false;
    BLEConnected = false;

    // pServer, pCmdChar etc. are NOT nulled — preserved for re-entry
    Serial.println("[BLE] Stopped advertising (stack preserved)");
}

BleCommand BleRfService::getCommand() {
    _hasCmd = false;
    return _pendingCmd;
}

void BleRfService::notifySignal(const DecodedSignal& sig) {
    if (!connected || !pSignalChar) return;

    uint8_t pkt[SIGNAL_PACKET_SIZE];
    size_t off = 0;
    pkt[off++] = SIGNAL_HEADER;
    pkt[off++] = sig.protocol;
    memcpy(&pkt[off], &sig.key, sizeof(uint64_t)); off += 8;
    pkt[off++] = sig.bits;
    memcpy(&pkt[off], &sig.pulseLength, sizeof(uint16_t)); off += 2;
    memcpy(&pkt[off], &sig.freq, sizeof(float));

    pSignalChar->notify(pkt, SIGNAL_PACKET_SIZE);
    Serial.printf("[BLE] Notify signal: proto=%d key=0x%llX bits=%d\n",
        sig.protocol, (unsigned long long)sig.key, sig.bits);
}

void BleRfService::notifyRawTimings(float freq, const uint16_t* durations,
                                     uint8_t count, uint8_t startLevel) {
    if (!connected || !pSignalChar) return;

    size_t pktLen = 7 + count * 2;
    uint8_t pkt[520];
    if (pktLen > sizeof(pkt)) pktLen = sizeof(pkt);

    size_t off = 0;
    pkt[off++] = RAW_TIMINGS_HEADER;
    memcpy(&pkt[off], &freq, sizeof(float)); off += 4;
    pkt[off++] = count;
    pkt[off++] = startLevel;

    uint8_t actual = (pktLen - 7) / 2;
    for (uint8_t i = 0; i < actual; i++) {
        memcpy(&pkt[off], &durations[i], sizeof(uint16_t)); off += 2;
    }

    pSignalChar->notify(pkt, off);
    Serial.printf("[BLE] Notify raw: %d pulses, freq=%.2f\n", actual, freq);
}

void BleRfService::notifyStatus(DeviceState state, ErrorCode error) {
    if (!pStatusChar) return;

    uint8_t pkt[STATUS_PACKET_SIZE] = { (uint8_t)state, (uint8_t)error };
    pStatusChar->setValue(pkt, STATUS_PACKET_SIZE);

    if (connected) {
        pStatusChar->notify(pkt, STATUS_PACKET_SIZE);
    }
}
