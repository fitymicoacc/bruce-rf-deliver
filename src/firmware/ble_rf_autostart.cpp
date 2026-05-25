#include "ble_rf_autostart.h"
#include "ble_rf_service.h"
#include "ble_rf_capture.h"

static bool autostarted = false;

void ble_rf_autostart_begin() {
    if (autostarted) return;
    autostarted = true;

    bleRfService.init();
    ble_rf_background_start();
}
