#include "ble_rf_mini.h"

#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_RF";

static ble_rf_cmd_cb_t s_on_cmd = NULL;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_connected = false;

static uint16_t s_signal_val_handle;
static uint16_t s_status_val_handle;

static uint8_t s_status_buf[2] = { STATE_IDLE, ERR_NONE };

/* Full 128-bit UUIDs matching Bruce-RF: 12345678-1234-5678-1234-56789abcdefX
 * BLE_UUID128_INIT takes bytes in little-endian (reversed) order. */
static const ble_uuid128_t svc_uuid =
    BLE_UUID128_INIT(0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t cmd_uuid =
    BLE_UUID128_INIT(0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t signal_uuid =
    BLE_UUID128_INIT(0xf2, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t status_uuid =
    BLE_UUID128_INIT(0xf3, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static float read_f32_le(const uint8_t *p) {
    float v;
    memcpy(&v, p, 4);
    return v;
}
static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint64_t read_u64_le(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)p[i]) << (i * 8);
    return v;
}

static int cmd_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;

    struct os_mbuf *om = ctxt->om;
    uint16_t len = OS_MBUF_PKTLEN(om);
    uint8_t buf[256];
    if (len > sizeof(buf)) len = sizeof(buf);
    os_mbuf_copydata(om, 0, len, buf);

    if (len < 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    ble_rf_cmd_t cmd = {0};
    cmd.cmd = buf[0];

    switch (cmd.cmd) {
    case CMD_START_LISTEN:
        if (len >= 5) cmd.freq = read_f32_le(&buf[1]);
        else cmd.freq = 433.92f;
        break;
    case CMD_STOP:
    case CMD_PING:
        break;
    case CMD_PLAY:
        if (len >= 17) {
            cmd.protocol = buf[1];
            cmd.key = read_u64_le(&buf[2]);
            cmd.bits = buf[10];
            cmd.pulse_length = read_u16_le(&buf[11]);
            cmd.freq = read_f32_le(&buf[13]);
        }
        break;
    case CMD_SCAN_LIST:
        if (len >= 4) {
            cmd.dwell_ms = read_u16_le(&buf[1]);
            cmd.scan_count = buf[3];
            if (cmd.scan_count > MAX_SCAN_FREQS) cmd.scan_count = MAX_SCAN_FREQS;
            for (int i = 0; i < cmd.scan_count && (4 + (i+1)*4) <= len; i++)
                cmd.scan_freqs[i] = read_f32_le(&buf[4 + i * 4]);
        }
        break;
    case CMD_SCAN_RANGES:
        if (len >= 4) {
            cmd.dwell_ms = read_u16_le(&buf[1]);
            cmd.scan_count = buf[3];
            if (cmd.scan_count > MAX_SCAN_RANGES) cmd.scan_count = MAX_SCAN_RANGES;
            for (int i = 0; i < cmd.scan_count && (4 + (i+1)*12) <= len; i++) {
                cmd.scan_ranges[i].start = read_f32_le(&buf[4 + i * 12]);
                cmd.scan_ranges[i].end   = read_f32_le(&buf[4 + i * 12 + 4]);
                cmd.scan_ranges[i].step  = read_f32_le(&buf[4 + i * 12 + 8]);
            }
        }
        break;
    default:
        ESP_LOGW(TAG, "Unknown cmd 0x%02X", cmd.cmd);
        return 0;
    }

    ESP_LOGI(TAG, "CMD: 0x%02X freq=%.2f", cmd.cmd, cmd.freq);
    if (s_on_cmd) s_on_cmd(&cmd);
    return 0;
}

static int signal_read_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    return 0;
}

static int status_read_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    os_mbuf_append(ctxt->om, s_status_buf, 2);
    return 0;
}

static struct ble_gatt_chr_def gatt_chars[] = {
    {
        .uuid = &cmd_uuid.u,
        .access_cb = cmd_write_cb,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &signal_uuid.u,
        .access_cb = signal_read_cb,
        .val_handle = &s_signal_val_handle,
        .flags = BLE_GATT_CHR_F_NOTIFY,
    },
    {
        .uuid = &status_uuid.u,
        .access_cb = status_read_cb,
        .val_handle = &s_status_val_handle,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
    },
    { 0 },
};

static struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = gatt_chars,
    },
    { 0 },
};

static int gap_event_cb(struct ble_gap_event *event, void *arg);

static void start_advertise(void) {
    const char *name = ble_svc_gap_device_name();
    if (!name) name = "Bruce-RF";

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
    }
}

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_connected = true;
            ESP_LOGI(TAG, "Connected (handle=%d)", s_conn_handle);
        } else {
            start_advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_connected = false;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ESP_LOGI(TAG, "Disconnected");
        start_advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe: attr=%d, notify=%d",
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify);
        break;
    default:
        break;
    }
    return 0;
}

static void on_sync(void) {
    uint8_t addr_type;
    int rc = ble_hs_id_infer_auto(0, &addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }
    start_advertise();
    ESP_LOGI(TAG, "BLE advertising started (addr_type=%d)", addr_type);
}

static void nimble_host_task(void *param) {
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_rf_mini_init(const char *device_name, ble_rf_cmd_cb_t on_cmd) {
    s_on_cmd = on_cmd;

    nvs_flash_erase();
    esp_err_t ret = nvs_flash_init();

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", ret);
        return;
    }

    ble_svc_gap_device_name_set(device_name);
    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_hs_cfg.sync_cb = on_sync;

    nimble_port_freertos_init(nimble_host_task);
    ESP_LOGI(TAG, "BLE initialized as '%s'", device_name);
}

bool ble_rf_mini_connected(void) {
    return s_connected;
}

void ble_rf_mini_notify_signal(uint8_t proto, uint64_t key, uint8_t bits,
                               uint16_t pulse, float freq)
{
    if (!s_connected) return;
    uint8_t pkt[17];
    pkt[0] = SIGNAL_HDR;
    pkt[1] = proto;
    memcpy(&pkt[2], &key, 8);
    pkt[10] = bits;
    memcpy(&pkt[11], &pulse, 2);
    memcpy(&pkt[13], &freq, 4);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(pkt, 17);
    ble_gatts_notify_custom(s_conn_handle, s_signal_val_handle, om);
}

void ble_rf_mini_notify_raw(float freq, const uint16_t *durations,
                            uint8_t count, uint8_t start_level)
{
    if (!s_connected) return;
    uint8_t pkt[7 + 256 * 2];
    pkt[0] = RAW_TIMINGS_HDR;
    memcpy(&pkt[1], &freq, 4);
    pkt[5] = count;
    pkt[6] = start_level;
    for (int i = 0; i < count && i < 256; i++) {
        memcpy(&pkt[7 + i * 2], &durations[i], 2);
    }
    int len = 7 + count * 2;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(pkt, len);
    ble_gatts_notify_custom(s_conn_handle, s_signal_val_handle, om);
}

void ble_rf_mini_notify_status(uint8_t state, uint8_t error) {
    s_status_buf[0] = state;
    s_status_buf[1] = error;
    if (!s_connected) return;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(s_status_buf, 2);
    ble_gatts_notify_custom(s_conn_handle, s_status_val_handle, om);
}
