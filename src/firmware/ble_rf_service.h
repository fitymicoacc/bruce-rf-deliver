#pragma once

#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <cstdint>
#include <cstring>

// BLE GATT UUIDs (match desktop Electron app)
#define BLE_RF_SERVICE_UUID      "12345678-1234-5678-1234-56789abcdef0"
#define BLE_RF_CMD_CHAR_UUID     "12345678-1234-5678-1234-56789abcdef1"
#define BLE_RF_SIGNAL_CHAR_UUID  "12345678-1234-5678-1234-56789abcdef2"
#define BLE_RF_STATUS_CHAR_UUID  "12345678-1234-5678-1234-56789abcdef3"

// Signal notification headers
#define SIGNAL_HEADER       0x80
#define RAW_TIMINGS_HEADER  0x81

// Packet sizes
#define SIGNAL_PACKET_SIZE  17  // header(1) + proto(1) + key(8) + bits(1) + pulse(2) + freq(4)
#define STATUS_PACKET_SIZE  2   // state(1) + error(1)

// Scan limits
#define MAX_SCAN_FREQS   32
#define MAX_SCAN_RANGES  8

// Device states
enum DeviceState : uint8_t {
    DEV_IDLE         = 0x00,
    DEV_LISTENING    = 0x01,
    DEV_TRANSMITTING = 0x02,
    DEV_SCANNING     = 0x03,
    DEV_ERROR        = 0xFF
};

// Error codes
enum ErrorCode : uint8_t {
    ERR_NONE         = 0x00,
    ERR_CC1101_FAIL  = 0x01,
    ERR_INVALID_CMD  = 0x02,
    ERR_TX_FAIL      = 0x03
};

// Command IDs (desktop → device)
enum CommandId : uint8_t {
    CMD_START_LISTEN = 0x01,  // + float32 freq
    CMD_STOP         = 0x02,
    CMD_PLAY         = 0x03,  // + proto(1) + key(8) + bits(1) + pulse(2) + freq(4)
    CMD_PING         = 0x04,
    CMD_SCAN_RANGES  = 0x05,  // not implemented (CC1101 has fixed ISM bands)
    CMD_SCAN_LIST    = 0x06   // + dwell(2) + count(1) + freqs(N*4)
};

// Decoded signal structure
struct DecodedSignal {
    uint8_t  protocol;
    uint64_t key;
    uint8_t  bits;
    uint16_t pulseLength;
    float    freq;
};

// Frequency range for SCAN_RANGES
struct FreqRange {
    float start;
    float end;
    float step;
};

// Scan parameters (used by both SCAN_LIST and SCAN_RANGES)
struct ScanParams {
    bool     rangeMode;
    // SCAN_LIST
    float    freqs[MAX_SCAN_FREQS];
    uint8_t  freqCount;
    // SCAN_RANGES
    FreqRange ranges[MAX_SCAN_RANGES];
    uint8_t   rangeCount;
    // Common
    uint16_t dwellMs;
};

// Parsed command from BLE
struct BleCommand {
    CommandId     id;
    float         freq;     // for START_LISTEN
    DecodedSignal signal;   // for PLAY
    ScanParams    scan;     // for SCAN_LIST / SCAN_RANGES
};

// BLE RF Service — uses separate callback classes (Bruce pattern)
class BleRfService {
public:
    bool init();
    void deinit();
    bool isConnected() const { return connected; }

    bool hasCommand() const { return _hasCmd; }
    BleCommand getCommand();

    void notifySignal(const DecodedSignal& sig);
    void notifyRawTimings(float freq, const uint16_t* durations, uint8_t count, uint8_t startLevel);
    void notifyStatus(DeviceState state, ErrorCode error = ERR_NONE);

    // Public state — accessed by callback classes
    volatile bool connected = false;
    volatile bool _hasCmd = false;
    BleCommand    _pendingCmd;

    NimBLEServer*         pServer = nullptr;
    NimBLECharacteristic* pCmdChar = nullptr;
    NimBLECharacteristic* pSignalChar = nullptr;
    NimBLECharacteristic* pStatusChar = nullptr;
};

extern BleRfService bleRfService;
