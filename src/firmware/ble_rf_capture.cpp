#include "ble_rf_capture.h"
#include "ble_rf_service.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/utils.h"
#include "modules/rf/rf_utils.h"
#include "modules/rf/rf_send.h"

#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <globals.h>

// ===== Scan state =====

struct ScanState {
    ScanParams params;
    uint8_t    idx;          // current range index (range mode) or freq index (list mode)
    float      currentFreq;  // current frequency within range
    uint32_t   lastSwitch;
    bool       active;
};

// ===== File-scope state =====

static RCSwitch rcswitch;
static DeviceState currentState = DEV_IDLE;
static float currentFreq = 0;
static ScanState scanState = {};

// Display state
static String lastCapProto = "";
static String lastCapCode = "";
static float  lastCapFreq = 0;
static int    captureCount = 0;

// Dedup: suppress duplicate decodes within time window
static uint64_t lastRxValue = 0;
static uint8_t  lastRxBits = 0;
static uint32_t lastRxTime = 0;
static const uint32_t DEDUP_WINDOW_MS = 1000;

// Background task coordination
static volatile bool rfForegroundLock = false;
static TaskHandle_t  bgTaskHandle = NULL;

// Forward declarations
static void draw_ble_rf_status(bool force = false);
static void startListening(float freq);
static void stopListening();
static void doPlay(const DecodedSignal& sig);
static void startScanList(const ScanParams& params);
static void stopScan();
static void pollScan();
static void pollRx();

// ===== GDO0 pin helper =====

static int getGdo0Pin() {
    return (bruceConfigPins.rfModule == CC1101_SPI_MODULE)
        ? (int)bruceConfigPins.CC1101_bus.io0
        : (int)bruceConfigPins.rfRx;
}

// ===== Configure CC1101 for OOK RX =====
// Override defaults after initRfModule for consumer remote decoding.
// Must be called in IDLE state (SWRS061D Section 19.1).

static void configureOokRx() {
    ELECHOUSE_cc1101.setSidle();

    // Data rate: 4.8 kBaud (DN022 Table 1 for OOK)
    // Bruce default 50 kBaud is too high for ~350μs period signals
    ELECHOUSE_cc1101.setDRate(4.8);

    // AGC tuning for OOK (TI E2E recommendations):
    //   AGCCTRL2=0x04: MAGN_TARGET=33dB (default 42dB overamplifies OOK silence)
    //   AGCCTRL0=0x92: WAIT=8 samples (default 32 too slow for OOK)
    ELECHOUSE_cc1101.SpiWriteReg(0x1B, 0x04);  // AGCCTRL2
    ELECHOUSE_cc1101.SpiWriteReg(0x1D, 0x92);  // AGCCTRL0

    ELECHOUSE_cc1101.SetRx();

    // Wait for PLL calibration + AGC settle (SWRS061D Table 24)
    vTaskDelay(pdMS_TO_TICKS(50));
}

// ===== START_LISTEN: single frequency =====

static void startListening(float freq) {
    if (currentState == DEV_LISTENING || currentState == DEV_SCANNING) {
        detachInterrupt(digitalPinToInterrupt(getGdo0Pin()));
        deinitRfModule();
    }

    if (!initRfModule("rx", freq)) {
        currentState = DEV_ERROR;
        bleRfService.notifyStatus(DEV_ERROR, ERR_CC1101_FAIL);
        return;
    }

    configureOokRx();
    rcswitch.enableReceive(getGdo0Pin());
    rcswitch.resetAvailable();

    currentFreq = freq;
    currentState = DEV_LISTENING;
    bleRfService.notifyStatus(DEV_LISTENING);

    Serial.printf("[RF] Listening on %.2f MHz\n", freq);
}

// ===== STOP =====

static void stopListening() {
    if (currentState == DEV_LISTENING || currentState == DEV_SCANNING) {
        // detachInterrupt is safer than disableReceive on T-Embed
        detachInterrupt(digitalPinToInterrupt(getGdo0Pin()));
        deinitRfModule();
    }
    stopScan();
    currentState = DEV_IDLE;
    bleRfService.notifyStatus(DEV_IDLE);
    Serial.println("[RF] Stopped");
}

// ===== SCAN_LIST: frequency hopping =====

// ===== READ_ANY: scan subghz_frequency_list with RSSI detection =====
// Pattern from bruce-rf/src/modules/rf/rf_scan.cpp fast_scan()

static const int RSSI_THRESHOLD = -65;  // dBm

static void doReadAny() {
    if (currentState == DEV_LISTENING || currentState == DEV_SCANNING) {
        detachInterrupt(digitalPinToInterrupt(getGdo0Pin()));
        deinitRfModule();
    }
    stopScan();

    currentState = DEV_SCANNING;
    bleRfService.notifyStatus(DEV_SCANNING);

    int scanRange = bruceConfigPins.rfScanRange;
    if (scanRange < 0 || scanRange > 3) scanRange = 3;  // default: all ranges
    int startIdx = range_limits[scanRange][0];
    int endIdx   = range_limits[scanRange][1];

    Serial.printf("[RF] READ_ANY: range %d-%d (%d freqs), RSSI > %d\n",
        startIdx, endIdx, endIdx - startIdx + 1, RSSI_THRESHOLD);

    // Init RF once on the first frequency (Bruce fast_scan pattern).
    // Then hop with setMHZ() — only frequency registers change,
    // dRate/AGC/modulation survive the hop.
    float firstFreq = subghz_frequency_list[startIdx];
    if (!initRfModule("rx", firstFreq)) {
        currentState = DEV_ERROR;
        bleRfService.notifyStatus(DEV_ERROR, ERR_CC1101_FAIL);
        return;
    }

    float bestFreq = 0;
    int bestRssi = -999;

    for (int idx = startIdx; idx <= endIdx; idx++) {
        float freq = subghz_frequency_list[idx];

        // Hop: setMHZ changes freq registers + antenna switch on T-Embed.
        // setClb() inside corrupts FSCTRL1 — restore after.
        setMHZ(freq);
        ELECHOUSE_cc1101.SpiWriteReg(0x0B, 0x06);  // FSCTRL1: fix setClb corruption

        // SPI sync for TFT + settle time (from Bruce fast_scan)
        if (rfForegroundLock) tft.drawPixel(0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(5));

        int rssi = ELECHOUSE_cc1101.getRssi();

        if (rssi > bestRssi) {
            bestRssi = rssi;
            bestFreq = freq;
        }

        if (rssi > RSSI_THRESHOLD) {
            Serial.printf("[RF] READ_ANY: signal on %.3f MHz (RSSI=%d)\n", freq, rssi);
            deinitRfModule();
            // Lock to this frequency and start full OOK listening
            currentFreq = freq;
            if (rfForegroundLock) draw_ble_rf_status(true);
            startListening(freq);
            return;
        }

        currentFreq = freq;
        if (rfForegroundLock && idx % 5 == 0) draw_ble_rf_status(true);
    }

    deinitRfModule();

    // No signal above threshold — lock to strongest
    if (bestFreq > 0 && bestRssi > -90) {
        Serial.printf("[RF] READ_ANY: no strong signal, best=%.3f MHz (RSSI=%d)\n",
            bestFreq, bestRssi);
        startListening(bestFreq);
    } else {
        Serial.println("[RF] READ_ANY: no signal found");
        currentState = DEV_IDLE;
        bleRfService.notifyStatus(DEV_IDLE);
    }
}

// ===== Scan: shared init/hop/poll for both SCAN_LIST and SCAN_RANGES =====

static void stopScan() {
    scanState.active = false;
}

// Get the first frequency for the current scan config
static float scanFirstFreq(const ScanState& s) {
    if (s.params.rangeMode) {
        return s.params.ranges[0].start;
    } else {
        return s.params.freqs[0];
    }
}

// Advance to next frequency, return it. Wraps around.
static float scanNextFreq(ScanState& s) {
    if (s.params.rangeMode) {
        // Step within current range
        const FreqRange& r = s.params.ranges[s.idx];
        s.currentFreq += r.step;
        if (s.currentFreq > r.end + 0.001f) {
            // Move to next range
            s.idx++;
            if (s.idx >= s.params.rangeCount) s.idx = 0;
            s.currentFreq = s.params.ranges[s.idx].start;
        }
    } else {
        s.idx++;
        if (s.idx >= s.params.freqCount) s.idx = 0;
        s.currentFreq = s.params.freqs[s.idx];
    }
    return s.currentFreq;
}

static void hopToFreq(float freq) {
    detachInterrupt(digitalPinToInterrupt(getGdo0Pin()));
    ELECHOUSE_cc1101.setSidle();
    setMHZ(freq);
    // setMHZ→setClb corrupts FSCTRL1/AGCCTRL2 — restore OOK config
    ELECHOUSE_cc1101.SpiWriteReg(0x0B, 0x06);  // FSCTRL1
    ELECHOUSE_cc1101.SpiWriteReg(0x1B, 0x04);  // AGCCTRL2
    ELECHOUSE_cc1101.SpiWriteReg(0x1D, 0x92);  // AGCCTRL0
    ELECHOUSE_cc1101.SetRx();
    rcswitch.enableReceive(getGdo0Pin());
    rcswitch.resetAvailable();
    currentFreq = freq;
}

static bool initScan(const ScanParams& params) {
    if (currentState == DEV_LISTENING || currentState == DEV_SCANNING) {
        detachInterrupt(digitalPinToInterrupt(getGdo0Pin()));
        deinitRfModule();
    }
    stopScan();

    scanState.params = params;
    scanState.idx = 0;
    scanState.active = true;
    if (scanState.params.dwellMs < 50) scanState.params.dwellMs = 50;

    float freq = scanFirstFreq(scanState);
    scanState.currentFreq = freq;

    if (!initRfModule("rx", freq)) {
        currentState = DEV_ERROR;
        bleRfService.notifyStatus(DEV_ERROR, ERR_CC1101_FAIL);
        return false;
    }
    configureOokRx();
    rcswitch.enableReceive(getGdo0Pin());
    rcswitch.resetAvailable();
    currentFreq = freq;

    scanState.lastSwitch = millis();
    currentState = DEV_SCANNING;
    bleRfService.notifyStatus(DEV_SCANNING);
    return true;
}

static void startScanList(const ScanParams& params) {
    if (params.freqCount == 0) {
        doReadAny();
        return;
    }
    if (initScan(params)) {
        Serial.printf("[RF] Scan list: %d freqs, dwell=%dms\n",
            params.freqCount, params.dwellMs);
    }
}

static void startScanRanges(const ScanParams& params) {
    if (params.rangeCount == 0) {
        doReadAny();
        return;
    }
    if (initScan(params)) {
        int totalFreqs = 0;
        for (uint8_t i = 0; i < params.rangeCount; i++) {
            totalFreqs += (int)((params.ranges[i].end - params.ranges[i].start) / params.ranges[i].step) + 1;
        }
        Serial.printf("[RF] Scan ranges: %d ranges, ~%d freqs, dwell=%dms\n",
            params.rangeCount, totalFreqs, params.dwellMs);
    }
}

static void pollScan() {
    if (!scanState.active) return;

    uint32_t now = millis();
    if (now - scanState.lastSwitch < scanState.params.dwellMs) return;

    // Carrier sense hold: if RCSwitch is mid-decode, don't hop (up to 500ms)
    if (rcswitch.available() && (now - scanState.lastSwitch < 500)) return;

    scanState.lastSwitch = now;
    float nextFreq = scanNextFreq(scanState);
    hopToFreq(nextFreq);
}

// ===== RX: extract raw timings and send via BLE =====
// Format matches cc1101-test/rf_controller.cpp:pollRaw()

static void pollRx() {
    if (!rcswitch.available()) return;

    // Read ALL RCSwitch data BEFORE resetAvailable (reset clears values)
    unsigned int* raw = rcswitch.getReceivedRawdata();
    int bits = rcswitch.getReceivedBitlength();
    uint16_t T = rcswitch.getReceivedDelay();
    uint64_t value = rcswitch.getReceivedValue();
    int proto = rcswitch.getReceivedProtocol();

    // Build durations array for desktop
    uint16_t durations[256];
    uint8_t count = 0;

    // Desktop decoder expects pairs starting from index 0.
    // RCSwitch raw: [0]=sync_LOW, [1..2N]=data pairs.
    // Prepend fabricated sync HIGH (1T) before sync LOW.
    durations[count++] = T;  // sync HIGH (1T)
    uint16_t syncLow = (raw[0] > 65535) ? 65535 : (uint16_t)raw[0];
    durations[count++] = syncLow;

    // Data pairs
    int dataEnd = bits * 2;
    for (int i = 1; i <= dataEnd && count < 254; i++) {
        uint16_t d = (raw[i] > 65535) ? 65535 : (uint16_t)raw[i];
        if (d == 0) break;
        durations[count++] = d;
    }

    // Now safe to reset
    rcswitch.resetAvailable();

    if (count < 6) return;  // too short

    // Dedup: P8/P9 (Conrad) and similar mirror protocols decode the same
    // signal as two different protocols with different codes. Suppress
    // if same bits count was decoded within the time window.
    if (value != 0) {
        uint32_t now = millis();
        if (bits == lastRxBits && (now - lastRxTime) < DEDUP_WINDOW_MS) {
            // Same bit-length signal within dedup window — skip duplicate
            Serial.printf("[RF] RX dedup: P%d key=0x%llX (suppressed, same %dbit within %dms)\n",
                proto, (unsigned long long)value, bits, now - lastRxTime);
            return;
        }
        lastRxValue = value;
        lastRxBits = (uint8_t)bits;
        lastRxTime = now;
    }

    // Update display state (only when foreground capture-mode is active)
    if (rfForegroundLock && value != 0) {
        captureCount++;
        char hexKey[20];
        snprintf(hexKey, sizeof(hexKey), "%llX", (unsigned long long)value);
        lastCapProto = "P" + String(proto) + " " + String(bits) + "bit";
        lastCapCode = String(hexKey);
        lastCapFreq = currentFreq;
    }

    if (value != 0 && proto > 0) {
        // RCSwitch decoded — send as decoded signal (0x80)
        DecodedSignal sig = {};
        sig.protocol = (uint8_t)proto;
        sig.key = value;
        sig.bits = (uint8_t)bits;
        sig.pulseLength = T;
        sig.freq = currentFreq;
        bleRfService.notifySignal(sig);
        Serial.printf("[RF] RX decoded: P%d key=0x%llX %dbit T=%d freq=%.2f\n",
            proto, (unsigned long long)value, bits, T, currentFreq);
    } else {
        // Unknown protocol — send raw timings (0x81) for desktop decoder
        bleRfService.notifyRawTimings(currentFreq, durations, count, 1);
        Serial.printf("[RF] RX raw: %d pulses (T=%d), freq=%.2f\n", count, T, currentFreq);
    }
}

// ===== PLAY: transmit signal via RCSwitch =====

static void doPlay(const DecodedSignal& sig) {
    DeviceState prevState = currentState;

    currentState = DEV_TRANSMITTING;
    bleRfService.notifyStatus(DEV_TRANSMITTING);

    // Detach RCSwitch ISR to prevent timing jitter during TX
    detachInterrupt(digitalPinToInterrupt(getGdo0Pin()));

    // Use protocol and key as-is from desktop.
    // Bruce RCSwitch uses standard protocol numbering (1-23+).
    uint8_t txProto = sig.protocol;
    uint64_t txKey = sig.key;

    Serial.printf("[RF] TX: proto=%d key=0x%llX bits=%d pulse=%d freq=%.2f\n",
        txProto, (unsigned long long)txKey, sig.bits, sig.pulseLength, sig.freq);

    // initRfModule("tx") sets CC1101 to TX mode (SPI, antenna switch, PA power)
    // RCSwitch_send then does enableTransmit → send → disableTransmit → deinitRfModule
    if (!initRfModule("tx", sig.freq)) {
        currentState = DEV_ERROR;
        bleRfService.notifyStatus(DEV_ERROR, ERR_TX_FAIL);
        return;
    }
    RCSwitch_send(txKey, sig.bits, sig.pulseLength, txProto, 10);

    // Restore RX if was listening or scanning
    if (prevState == DEV_LISTENING) {
        startListening(currentFreq);
    } else if (prevState == DEV_SCANNING && scanState.active) {
        // Re-init on current scan frequency
        if (initRfModule("rx", currentFreq)) {
            configureOokRx();
            rcswitch.enableReceive(getGdo0Pin());
            rcswitch.resetAvailable();
            currentState = DEV_SCANNING;
            bleRfService.notifyStatus(DEV_SCANNING);
        }
    } else {
        currentState = DEV_IDLE;
        bleRfService.notifyStatus(DEV_IDLE);
    }
}

// ===== Command dispatcher =====

static void processCommand(const BleCommand& cmd) {
    switch (cmd.id) {
        case CMD_START_LISTEN:
            startListening(cmd.freq);
            break;

        case CMD_STOP:
            stopListening();
            break;

        case CMD_PLAY:
            doPlay(cmd.signal);
            break;

        case CMD_PING:
            bleRfService.notifyStatus(currentState);
            Serial.println("[BLE] PONG");
            break;

        case CMD_SCAN_LIST:
            startScanList(cmd.scan);
            break;

        case CMD_SCAN_RANGES:
            startScanRanges(cmd.scan);
            break;
    }
}

// ===== Background task =====

void ble_rf_background_tick() {
    if (rfForegroundLock) return;

    if (bleRfService.hasCommand()) {
        BleCommand cmd = bleRfService.getCommand();
        processCommand(cmd);
    }
    if (currentState == DEV_SCANNING) pollScan();
    if (currentState == DEV_LISTENING || currentState == DEV_SCANNING) pollRx();
}

static void bleRfBackgroundTask(void* param) {
    (void)param;
    for (;;) {
        ble_rf_background_tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void ble_rf_background_start() {
    if (bgTaskHandle != NULL) return;
    xTaskCreate(bleRfBackgroundTask, "BleRfBg", 4096, NULL, 1, &bgTaskHandle);
    Serial.println("[BLE_RF] Background task started");
}

void ble_rf_background_pause() {
    if (rfForegroundLock) return;
    rfForegroundLock = true;
    vTaskDelay(pdMS_TO_TICKS(200));
    if (currentState == DEV_LISTENING || currentState == DEV_SCANNING) {
        detachInterrupt(digitalPinToInterrupt(getGdo0Pin()));
        deinitRfModule();
    }
    currentState = DEV_IDLE;
    bleRfService.notifyStatus(DEV_IDLE);
}

void ble_rf_background_resume() {
    rfForegroundLock = false;
}

// ===== Display =====

static void draw_ble_rf_status(bool force) {
    static DeviceState lastState = (DeviceState)0xFE;
    static bool lastConnected = true;
    static unsigned long lastAnimMs = 0;
    static int animFrame = 0;
    static int lastCaptureCount = -1;

    bool connected = bleRfService.isConnected();
    bool stateChanged = (connected != lastConnected || currentState != lastState
                         || captureCount != lastCaptureCount);

    bool animTick = (!connected || currentState == DEV_SCANNING || currentState == DEV_LISTENING)
                    && (millis() - lastAnimMs > 500);

    if (!force && !stateChanged && !animTick) return;
    if (animTick) { animFrame = (animFrame + 1) % 4; lastAnimMs = millis(); }

    lastState = currentState;
    lastConnected = connected;
    lastCaptureCount = captureCount;

    // Reclaim SPI from CC1101 for TFT (T-Embed shared bus)
    tft.drawPixel(0, 0, 0);

    uint16_t bg = bruceConfig.bgColor;
    uint16_t fg = bruceConfig.priColor;

    tft.fillRect(10, 26, tftWidth - 20, tftHeight - 36, bg);

    /* Title */
    tft.setTextSize(FM);
    tft.setTextColor(TFT_CYAN, bg);
    tft.setCursor(20, 30);
    tft.print("RF Capture");

    /* BLE status */
    tft.setCursor(20, 52);
    tft.setTextColor(fg, bg);
    tft.print("BLE: ");
    if (connected) {
        tft.setTextColor(TFT_GREEN, bg);
        tft.print("Connected");
    } else {
        tft.setTextColor(TFT_RED, bg);
        tft.print("Waiting");
        for (int d = 0; d < animFrame; d++) tft.print(".");
        tft.print("   ");
    }

    /* State + frequency */
    tft.setCursor(20, 72);
    switch (currentState) {
        case DEV_IDLE:
            tft.setTextColor(TFT_DARKGREY, bg);
            tft.print("IDLE");
            break;
        case DEV_LISTENING:
            tft.setTextColor(TFT_GREEN, bg);
            tft.printf("LISTEN %.2f", currentFreq);
            for (int d = 0; d < animFrame; d++) tft.print(".");
            tft.print("  ");
            break;
        case DEV_SCANNING:
            tft.setTextColor(TFT_YELLOW, bg);
            tft.printf("SCAN %.2f", currentFreq);
            for (int d = 0; d < animFrame; d++) tft.print(".");
            tft.print("  ");
            break;
        case DEV_TRANSMITTING:
            tft.setTextColor(TFT_MAGENTA, bg);
            tft.print("TRANSMITTING");
            break;
        case DEV_ERROR:
            tft.setTextColor(TFT_RED, bg);
            tft.print("ERROR");
            break;
    }

    /* Separator */
    tft.drawFastHLine(20, 92, tftWidth - 40, TFT_DARKGREY);

    /* Last captured signal */
    if (lastCapProto.length() > 0) {

        tft.setCursor(20, 97);
        tft.setTextSize(FP);
        tft.setTextColor(TFT_GREEN, bg);
        tft.printf("#%d ", captureCount);
        tft.setTextColor(TFT_WHITE, bg);
        tft.print(lastCapProto);
        tft.setTextColor(TFT_DARKGREY, bg);
        tft.printf(" %.1fMHz", lastCapFreq);

        tft.setCursor(20, 115);
        tft.setTextColor(TFT_CYAN, bg);
        tft.print("0x");
        tft.print(lastCapCode);
    }

    /* Footer */
    tft.setTextSize(FP);
    tft.setTextColor(getColorVariation(fg), bg);
    tft.setCursor(20, tftHeight - 20);
    tft.print("[ESC] exit");
    tft.setCursor(tftWidth - 80, tftHeight - 20);
    tft.printf("Sig: %d", captureCount);
}

// ===== Main entry point =====

void ble_rf_capture_mode() {
    Serial.println("[BLE_RF] Entering BLE RF Capture mode");

    ble_rf_background_pause();

    tft.fillScreen(bruceConfig.bgColor);
    drawMainBorder();

    // bleRfService is initialized from setup() via ble_rf_autostart_begin().
    // The call below is a no-op on subsequent entries (guarded inside
    // BleRfService::init by pServer != nullptr) — it just re-starts
    // advertising if it was stopped.
    if (!bleRfService.init()) {
        displayError("BLE init failed");
        delay(2000);
        ble_rf_background_resume();
        return;
    }
    Serial.println("[BLE_RF] BLE capture active");

    // Reset state
    currentState = DEV_IDLE;
    currentFreq = 0;
    captureCount = 0;
    lastCapProto = "";
    lastCapCode = "";
    lastCapFreq = 0;
    scanState.active = false;

    draw_ble_rf_status(true);

    // Non-blocking main loop
    while (!check(EscPress)) {
        previousMillis = millis();

        // 1. Process BLE commands
        if (bleRfService.hasCommand()) {
            BleCommand cmd = bleRfService.getCommand();
            processCommand(cmd);
        }

        // 2. Frequency hopping during scan
        if (currentState == DEV_SCANNING) {
            pollScan();
        }

        // 3. Poll RCSwitch for signals
        if (currentState == DEV_LISTENING || currentState == DEV_SCANNING) {
            pollRx();
        }

        // 4. Update display
        draw_ble_rf_status();

        delay(1);  // yield for BLE stack
    }

    if (currentState == DEV_LISTENING || currentState == DEV_SCANNING) {
        detachInterrupt(digitalPinToInterrupt(getGdo0Pin()));
        deinitRfModule();
    }
    currentState = DEV_IDLE;
    bleRfService.notifyStatus(DEV_IDLE);

    ble_rf_background_resume();
}
