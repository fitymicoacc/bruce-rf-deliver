#pragma once

// Initialize BLE RF service at boot so the device advertises "Bruce-RF"
// without requiring the user to enter the RF → BLE RF Capture menu first.
//
// Call once from setup() after core peripherals are ready. Safe to call
// together with other BLE users in the firmware (NimBLE is reused).
//
// Full capture operation (RX/TX, display) still runs inside
// ble_rf_capture_mode(). The dispatcher that programmatically enters
// capture mode on incoming START_LISTEN / SCAN_* commands lives in a
// follow-up revision.
void ble_rf_autostart_begin();
