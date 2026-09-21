// Stage 9: Bluetooth handsfree (HFP client) test console. See doc/validate_board_plan.md, stage 9.
//
// Mirrors gcore_pots_bt/main/bt_task.c (same controller settings, class of device, HFP client and HCI voice data path),
// but everything the dial and the GUI would do is a console command instead: pairing (SSP passkey entry, with the
// passkey typed at the console), forget pairing, dial, answer, hang up, DTMF.
//
// Call audio: the SCO voice data arrives as 16-bit mono PCM through two callbacks. A bridge task moves it between the
// callbacks and the codec: phone -> earpiece (I2S out) and microphone (I2S in) -> phone. Set the earpiece output first
// with `audio out spk` and a low `vol`. The codec is restarted at 8 kHz (CVSD) or 16 kHz (mSBC) when call audio starts.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "esp_console.h"
#include "esp_timer.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_hf_client_api.h"
#include "nvs_flash.h"
#include "cmds.h"

#define DEVICE_NAME    "weeBell-test"
#define BR_FRAMES      64           // frames per bridge iteration (4 ms at 16 kHz, 8 ms at 8 kHz)
#define SB_BYTES       8192
#define RX_PRIME_MS    30           // audio buffered before earpiece playback starts (the jitter buffer)
#define RX_MAX_MS      100          // more than this is dropped (clock drift between phone and codec)
#define TX_PREFILL_MS  20           // silence put ahead of the microphone data so the phone's requests never find it empty
#define TX_MAX_MS      60           // microphone data older than this is dropped
#define AUTO_MS        10000        // reconnect attempt interval

typedef enum { EV_AUDIO_UP, EV_AUDIO_DOWN } ev_type_t;
typedef struct {
    ev_type_t type;
    int arg;
    int64_t t_us;       // when the stack reported the event (for the start-up time printout)
} ev_t;

// Device ID (DID) record. Apple's guidelines (section 2.2.1) ask for one, with a Bluetooth SIG vendor ID for a company
// product. This is a hobby device with no SIG company ID, so it uses the USB-IF source (2) with the pid.codes test
// vendor/product (0x1209 / 0x0001) which are free for development. Change these if the device gets its own IDs.
#define DID_VENDOR_ID_SOURCE   2
#define DID_VENDOR_ID          0x1209
#define DID_PRODUCT_ID         0x0001
#define DID_VERSION            0x0100        // 1.0.0

// ESP-IDF 4.4.4 has no public API for a Device ID record (esp_sdp arrived in IDF 5). The stack's own function is used
// directly; the struct mirrors tSDP_DI_RECORD (sdp_api.h) with SDP_MAX_ATTR_LEN = 400 (bt_target.h).
typedef struct {
    uint16_t vendor;
    uint16_t vendor_id_source;
    uint16_t product;
    uint16_t version;
    uint8_t primary_record;
    char client_executable_url[400];
    char service_description[400];
    char documentation_url[400];
} did_record_t;
extern uint8_t BTA_DmSetLocalDiRecord(did_record_t *p_device_info, uint32_t *p_handle);

static int bt_stats(void);
static bool bt_up;
static QueueHandle_t evq;
static StreamBufferHandle_t rx_sb, tx_sb;          // 16-bit mono samples: phone -> earpiece, microphone -> phone
static volatile bool br_run, br_alive;

static esp_bd_addr_t peer;
static bool have_peer;
static esp_bd_addr_t ssp_addr;
static int conn_state;                              // esp_hf_client_connection_state_t
static bool slc;
static int audio_state;                             // 0 disconnected, 1 connecting, 2 CVSD 8 kHz, 3 mSBC 16 kHz
static int call_ind, callsetup_ind, callheld_ind;
static bool pair_active;
static int64_t pair_until_us;
static bool auto_connect = true;
static int64_t next_auto_us;
// NoInputNoOutput = Just Works: the only method that worked with an iPhone (KeyboardOnly passkey entry: the phone never
// showed a code and pairing timed out). `bt iocap in|io|out` selects the others for experiments.
static esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
static int cod_major = 0x04;                        // audio / video
// Default minor 2 (hands-free). iOS asks the user to pick a device type (Car Stereo, Headphone, Hearing Aid, Speaker,
// Other) after pairing. Tried in stage 9: minor 6 (loudspeaker) did not stop the prompt, so no class we send avoids it; it
// is a single tap that iOS remembers for the bonded device. Do not claim a class the device is not.
static int cod_minor = 0x02;                        // hands-free (audio/video minor classes: 1 headset, 2 hands-free,
                                                    // 5 microphone, 6 loudspeaker, 7 headphones, 8 portable audio,
                                                    // 9 car audio, 11 hi-fi audio)
static int cod_service = 0x300;                     // 11-bit service class field: 0x100 audio, 0x200 telephony,
                                                    // 0x020 rendering (0x120 + minor 1 gives the common headset 0x240404)

static volatile uint32_t st_rx_bytes, st_tx_bytes, st_rx_under, st_rx_drop, st_tx_under, st_tx_drop, st_rx_trim;
static volatile uint32_t st_in_size, st_out_size, st_in_pkts, st_out_pkts;
static volatile int rx_prime_samples, rx_max_samples, tx_max_samples;   // set per call from the sample rate
// Digital gain on each direction (linear). The call audio is often far below full scale, so it can be raised here, with
// clipping at full scale. `bt gain rx|tx <dB>`.
// The microphone default is -12 dB: at 0 dB (peaks near full scale) the far end heard clipping with the mouth 2 cm from the
// module; -12 dB fixed it (stage 9). The design's microphone volume digit 6 is about -12.8 dB.
static volatile float rx_gain = 1.0f, tx_gain = 0.2512f;
// Levels: peak and sum of squares of what arrives from the phone (before gain) and of what is sent to it (after gain).
static volatile int st_rx_peak, st_tx_peak;
static volatile uint64_t st_rx_sumsq, st_tx_sumsq, st_rx_n, st_tx_n;

static int16_t apply_gain(float v, float g)
{
    v *= g;
    return v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : (int16_t)v;
}

// Equalization is not done here any more: the microphone profile is applied where the codec is read and the earpiece profile
// where it is written (eq.c, `eqp`), so it also covers this bridge. Only the gains below are applied in this file.

static const char *conn_str[] = {"disconnected", "connecting", "connected", "slc_connected", "disconnecting"};
static const char *audio_str[] = {"disconnected", "connecting", "connected (CVSD, 8 kHz)", "connected (mSBC, 16 kHz)"};
static const char *setup_str[] = {"none", "incoming", "outgoing dialing", "outgoing alerting"};
static const char *iocap_str[] = {"out (DisplayOnly)", "io (DisplayYesNo)", "in (KeyboardOnly)", "none (NoInputNoOutput)"};

static void pa(const uint8_t *a)
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
}

static bool parse_addr(const char *s, uint8_t *out)
{
    unsigned v[6];
    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        out[i] = (uint8_t)v[i];
    }
    return true;
}

static int bonded(esp_bd_addr_t *list, int max)
{
    int n = esp_bt_gap_get_bond_device_num();
    if (n > max) {
        n = max;
    }
    if (n > 0 && esp_bt_gap_get_bond_device_list(&n, list) == ESP_OK) {
        return n;
    }
    return 0;
}

// Put a whole message in a stream buffer or none of it, so the samples never get out of step.
static bool sb_put(StreamBufferHandle_t sb, const void *data, size_t bytes)
{
    if (xStreamBufferSpacesAvailable(sb) < bytes) {
        return false;
    }
    return xStreamBufferSend(sb, data, bytes, 0) == bytes;
}

static void sb_discard(StreamBufferHandle_t sb, size_t keep_bytes)
{
    uint8_t junk[256];
    size_t avail = xStreamBufferBytesAvailable(sb);
    while (avail > keep_bytes) {
        size_t want = avail - keep_bytes;
        size_t r = xStreamBufferReceive(sb, junk, want < sizeof(junk) ? want : sizeof(junk), 0);
        if (r == 0) {
            break;
        }
        avail -= r;
    }
}

// ---- Voice data callbacks (called on the Bluetooth task) ----
static void hf_incoming(const uint8_t *buf, uint32_t sz)
{
    st_in_size = sz;
    st_in_pkts++;
    st_rx_bytes += sz;
    const int16_t *pcm = (const int16_t *)buf;
    int peak = st_rx_peak;
    uint64_t sumsq = st_rx_sumsq;
    for (uint32_t i = 0; i < sz / 2; i++) {
        int a = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (a > peak) {
            peak = a;
        }
        sumsq += (uint64_t)((int)pcm[i] * (int)pcm[i]);
    }
    st_rx_peak = peak;
    st_rx_sumsq = sumsq;
    st_rx_n += sz / 2;
    if (!sb_put(rx_sb, buf, sz)) {
        st_rx_drop++;
    }
    esp_hf_client_outgoing_data_ready();
}

static uint32_t hf_outgoing(uint8_t *p, uint32_t sz)
{
    st_out_size = sz;
    st_out_pkts++;
    st_tx_bytes += sz;
    sb_discard(tx_sb, (size_t)tx_max_samples * 2);
    size_t r = xStreamBufferReceive(tx_sb, p, sz, 0);
    if (r < sz) {
        memset(p + r, 0, sz - r);
        st_tx_under++;
    }
    return sz;
}

// ---- Bridge between the voice callbacks and the codec ----
static void bridge_task(void *arg)
{
    static int16_t in[BR_FRAMES * 2], out[BR_FRAMES * 2], mono[BR_FRAMES];
    bool primed = false;
    br_alive = true;
    while (br_run) {
        int got = audio_read_frames(in, BR_FRAMES, 200);         // paced by the codec clock; the microphone
        if (got > 0) {
            int tpeak = st_tx_peak;
            uint64_t tsum = st_tx_sumsq;
            const float g = tx_gain;
            for (int i = 0; i < got; i++) {
                mono[i] = apply_gain((float)in[2 * i], g);
                int a = mono[i] < 0 ? -mono[i] : mono[i];
                if (a > tpeak) {
                    tpeak = a;
                }
                tsum += (uint64_t)((int)mono[i] * (int)mono[i]);
            }
            st_tx_peak = tpeak;
            st_tx_sumsq = tsum;
            st_tx_n += (uint64_t)got;
            if (!sb_put(tx_sb, mono, (size_t)got * 2)) {
                st_tx_drop++;
            }
        }
        size_t before = xStreamBufferBytesAvailable(rx_sb);
        sb_discard(rx_sb, (size_t)rx_max_samples * 2);
        if (xStreamBufferBytesAvailable(rx_sb) < before) {
            st_rx_trim++;                                        // too far behind: dropped the oldest audio
        }
        size_t avail = xStreamBufferBytesAvailable(rx_sb) / 2;
        if (!primed && avail >= (size_t)rx_prime_samples) {
            primed = true;
        }
        int have = 0;
        if (primed) {
            have = (int)(xStreamBufferReceive(rx_sb, mono, BR_FRAMES * 2, 0) / 2);
            if (have < BR_FRAMES) {
                st_rx_under++;
                primed = false;
            }
        }
        const float rg = rx_gain;
        for (int i = 0; i < BR_FRAMES; i++) {
            int16_t s = i < have ? apply_gain((float)mono[i], rg) : 0;
            out[2 * i] = s;
            out[2 * i + 1] = s;
        }
        audio_write_frames(out, BR_FRAMES);                     // the earpiece
    }
    br_alive = false;
    vTaskDelete(NULL);
}

static void bridge_start(int rate)
{
    xStreamBufferReset(rx_sb);
    xStreamBufferReset(tx_sb);
    rx_prime_samples = rate * RX_PRIME_MS / 1000;
    rx_max_samples = rate * RX_MAX_MS / 1000;
    tx_max_samples = rate * TX_MAX_MS / 1000;
    st_rx_bytes = st_tx_bytes = st_rx_under = st_rx_drop = st_tx_under = st_tx_drop = st_rx_trim = 0;
    st_in_pkts = st_out_pkts = 0;
    st_rx_peak = st_tx_peak = 0;
    st_rx_sumsq = st_tx_sumsq = st_rx_n = st_tx_n = 0;
    static int16_t zeros[1024];
    int pre = rate * TX_PREFILL_MS / 1000;
    xStreamBufferSend(tx_sb, zeros, (size_t)(pre > 1024 ? 1024 : pre) * 2, 0);
    br_run = true;
    xTaskCreatePinnedToCore(bridge_task, "bt_bridge", 4096, NULL, 6, NULL, 1);
    while (!br_alive) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void bridge_stop(void)
{
    br_run = false;
    while (br_alive) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Runs the slow work that must not happen inside a Bluetooth callback (restarting the codec) and the timers.
static void ctl_task(void *arg)
{
    ev_t ev;
    for (;;) {
        if (xQueueReceive(evq, &ev, pdMS_TO_TICKS(500))) {
            if (ev.type == EV_AUDIO_UP) {
                if (br_alive) {
                    bridge_stop();
                    audio_source_resume();
                }
                if (audio_restart(ev.arg) == 0) {
                    audio_source_pause();
                    bridge_start(ev.arg);
                    printf("call audio up: codec at %d Hz, bridge running %d ms after the phone opened the audio link "
                           "(Apple asks for audio within 40 ms; the codec restart is the slow part)\n",
                           ev.arg, (int)((esp_timer_get_time() - ev.t_us) / 1000));
                } else {
                    printf("call audio: codec restart failed\n");
                }
            } else if (ev.type == EV_AUDIO_DOWN) {
                if (br_alive) {
                    bridge_stop();
                    audio_source_resume();
                    printf("call audio down: bridge stopped. Call statistics:\n");
                    bt_stats();
                }
            }
        }
        int64_t now = esp_timer_get_time();
        if (pair_active && now > pair_until_us) {
            pair_active = false;
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
            printf("pairing window closed (no longer discoverable)\n");
        }
        if (auto_connect && conn_state == 0 && !pair_active && now > next_auto_us) {
            esp_bd_addr_t list[4];
            int n = bonded(list, 4);
            next_auto_us = now + (int64_t)AUTO_MS * 1000;
            if (n > 0) {
                printf("auto-connect to ");
                pa(list[0]);
                printf("\n");
                esp_hf_client_connect(list[0]);
            }
        }
    }
}

// ---- GAP callback: pairing ----
static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            printf("PAIRING OK: %s ", param->auth_cmpl.device_name);
            pa(param->auth_cmpl.bda);
            printf("\n");
            memcpy(peer, param->auth_cmpl.bda, 6);
            have_peer = true;
            next_auto_us = esp_timer_get_time() + 8000000;     // the phone usually connects by itself right after
        } else {
            printf("PAIRING FAILED: status %d\n", param->auth_cmpl.stat);
        }
        break;
#if CONFIG_BT_SSP_ENABLED
    case ESP_BT_GAP_CFM_REQ_EVT:
        memcpy(ssp_addr, param->cfm_req.bda, 6);
        printf("PAIRING: numeric comparison. The phone should show %06u. Type 'bt yes' if it matches, else 'bt no'.\n",
               (unsigned)param->cfm_req.num_val);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        printf("PAIRING: type %06u on the phone.\n", (unsigned)param->key_notif.passkey);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        memcpy(ssp_addr, param->key_req.bda, 6);
        printf("PAIRING: the phone shows a 6-digit passkey. Type it here: bt passkey <6 digits>  (or 'bt no' to refuse)\n");
        break;
#endif
    case ESP_BT_GAP_PIN_REQ_EVT: {
        esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
        printf("PAIRING: legacy PIN requested, answering 0000\n");
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin);
        break;
    }
    case ESP_BT_GAP_REMOVE_BOND_DEV_COMPLETE_EVT:
        printf("bond removed (status %d)\n", param->remove_bond_dev_cmpl.status);
        break;
    case ESP_BT_GAP_CONFIG_EIR_DATA_EVT:
        printf("EIR configured: status %d, %d data types\n", param->config_eir_data.stat, param->config_eir_data.eir_type_num);
        break;
    default:
        break;
    }
}

// ---- HFP client callback ----
static void hf_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param)
{
    switch (event) {
    case ESP_HF_CLIENT_CONNECTION_STATE_EVT:
        conn_state = param->conn_stat.state;
        slc = param->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED;
        if (param->conn_stat.state != ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED) {
            memcpy(peer, param->conn_stat.remote_bda, 6);
            have_peer = true;
        }
        printf("HFP connection: %s", conn_str[param->conn_stat.state]);
        if (slc) {
            printf(" (peer features 0x%x, call-hold features 0x%x)", (unsigned)param->conn_stat.peer_feat,
                   (unsigned)param->conn_stat.chld_feat);
        }
        printf("\n");
        break;
    case ESP_HF_CLIENT_AUDIO_STATE_EVT: {
        audio_state = param->audio_stat.state;
        printf("HFP call audio: %s\n", audio_str[param->audio_stat.state]);
        ev_t ev = {.type = EV_AUDIO_DOWN, .arg = 0, .t_us = esp_timer_get_time()};
        if (audio_state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED || audio_state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC) {
            esp_hf_client_register_data_callback(hf_incoming, hf_outgoing);
            ev.type = EV_AUDIO_UP;
            ev.arg = audio_state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC ? 16000 : 8000;
            xQueueSend(evq, &ev, 0);
        } else if (audio_state == ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED) {
            xQueueSend(evq, &ev, 0);
        }
        break;
    }
    case ESP_HF_CLIENT_CIND_CALL_EVT:
        call_ind = param->call.status;
        printf("call indicator: %s\n", call_ind ? "call in progress" : "no call");
        break;
    case ESP_HF_CLIENT_CIND_CALL_SETUP_EVT:
        callsetup_ind = param->call_setup.status;
        printf("call setup: %s\n", setup_str[callsetup_ind]);
        break;
    case ESP_HF_CLIENT_CIND_CALL_HELD_EVT:
        callheld_ind = param->call_held.status;
        printf("call held indicator: %d\n", callheld_ind);
        break;
    case ESP_HF_CLIENT_RING_IND_EVT:
        printf("RING\n");
        break;
    case ESP_HF_CLIENT_CLIP_EVT:
        printf("caller: %s\n", param->clip.number ? param->clip.number : "(unknown)");
        break;
    case ESP_HF_CLIENT_CCWA_EVT:
        printf("call waiting: %s\n", param->ccwa.number ? param->ccwa.number : "(unknown)");
        break;
    case ESP_HF_CLIENT_CLCC_EVT:
        printf("call %d: %s, state %d, number %s\n", param->clcc.idx, param->clcc.dir ? "incoming" : "outgoing",
               (int)param->clcc.status, param->clcc.number ? param->clcc.number : "(none)");
        break;
    case ESP_HF_CLIENT_VOLUME_CONTROL_EVT:
        printf("phone volume request: %s %d\n", param->volume_control.type ? "microphone" : "speaker",
               param->volume_control.volume);
        break;
    case ESP_HF_CLIENT_CIND_SERVICE_AVAILABILITY_EVT:
        printf("phone network service: %s\n", param->service_availability.status ? "available" : "unavailable");
        break;
    case ESP_HF_CLIENT_CIND_SIGNAL_STRENGTH_EVT:
        printf("phone signal strength: %d\n", param->signal_strength.value);
        break;
    case ESP_HF_CLIENT_CIND_BATTERY_LEVEL_EVT:
        printf("phone battery: %d\n", param->battery_level.value);
        break;
    case ESP_HF_CLIENT_COPS_CURRENT_OPERATOR_EVT:
        printf("phone operator: %s\n", param->cops.name);
        break;
    case ESP_HF_CLIENT_AT_RESPONSE_EVT:
        printf("AT response: code %d, cme %d\n", param->at_response.code, param->at_response.cme);
        break;
    default:
        break;
    }
}

static int bt_start(void)
{
    if (bt_up) {
        printf("Bluetooth is already on\n");
        return 0;
    }
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();                       // NVS holds only the Bluetooth bonds and PHY data
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        printf("nvs init failed: %s\n", esp_err_to_name(ret));
        return 1;
    }
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bt_controller_init(&cfg)) != ESP_OK || (ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK ||
        (ret = esp_bluedroid_init()) != ESP_OK || (ret = esp_bluedroid_enable()) != ESP_OK) {
        printf("Bluetooth stack start failed: %s\n", esp_err_to_name(ret));
        return 1;
    }
    esp_bt_cod_t cod;
    cod.reserved_2 = 0;
    cod.minor = cod_minor;
    cod.major = cod_major;
    cod.reserved_8 = 0;
    cod.service = cod_service;
    esp_bt_gap_set_cod(cod, ESP_BT_INIT_COD);
    esp_bt_dev_set_device_name(DEVICE_NAME);
    // Apple guidelines 2.1.4: the Extended Inquiry Response shall carry the local name and the TX power level.
    // The stack adds the name and the service UUIDs; TX power is off by default.
    esp_bt_eir_data_t eir = {.fec_required = true, .include_txpower = true, .include_uuid = true, .flag = 0};
    esp_bt_gap_config_eir_data(&eir);
    // Apple guidelines 2.2.1: a Device ID record.
    did_record_t did = {.vendor = DID_VENDOR_ID, .vendor_id_source = DID_VENDOR_ID_SOURCE, .product = DID_PRODUCT_ID,
                        .version = DID_VERSION, .primary_record = 1};
    uint32_t did_handle = 0;
    printf("Device ID record: %s (vendor source %d, vendor 0x%04x, product 0x%04x, version 0x%04x)\n",
           BTA_DmSetLocalDiRecord(&did, &did_handle) == 0 ? "added" : "FAILED", DID_VENDOR_ID_SOURCE, DID_VENDOR_ID,
           DID_PRODUCT_ID, DID_VERSION);
    esp_bt_gap_register_callback(gap_cb);
    esp_hf_client_register_callback(hf_cb);
    esp_hf_client_init();
#if CONFIG_BT_SSP_ENABLED
    esp_bt_sp_param_t ptype = ESP_BT_SP_IOCAP_MODE;
    esp_bt_gap_set_security_param(ptype, &iocap, sizeof(uint8_t));
#endif
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 0, pin_code);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);

    rx_sb = xStreamBufferCreate(SB_BYTES, 1);
    tx_sb = xStreamBufferCreate(SB_BYTES, 1);
    evq = xQueueCreate(8, sizeof(ev_t));
    xTaskCreate(ctl_task, "bt_ctl", 6144, NULL, 4, NULL);
    next_auto_us = esp_timer_get_time() + 3000000;
    bt_up = true;
    printf("Bluetooth on as '%s', address ", DEVICE_NAME);
    pa(esp_bt_dev_get_address());
    printf(", SSP %s, IO capability %s. Not discoverable: type 'bt pair' to pair.\n",
#if CONFIG_BT_SSP_ENABLED
           "on",
#else
           "OFF (legacy PIN 0000)",
#endif
           iocap_str[iocap]);
    return 0;
}

static int need_up(void)
{
    if (!bt_up) {
        printf("type 'bt on' first\n");
        return 0;
    }
    return 1;
}

static int need_slc(void)
{
    if (!need_up()) {
        return 0;
    }
    if (!slc) {
        printf("no HFP service-level connection yet: 'bt connect' (or wait for the phone / auto-connect)\n");
        return 0;
    }
    return 1;
}

static int bt_status(void)
{
    printf("Bluetooth: %s\n", bt_up ? "on" : "off (type 'bt on')");
    if (!bt_up) {
        return 0;
    }
    printf("  own address ");
    pa(esp_bt_dev_get_address());
    printf("\n  IO capability: %s; %s; auto-connect %s\n", iocap_str[iocap], pair_active ? "DISCOVERABLE now" : "not discoverable",
           auto_connect ? "on" : "off");
    printf("  HFP connection: %s; call audio: %s\n", conn_str[conn_state], audio_str[audio_state]);
    printf("  call: %s; call setup: %s; bridge: %s\n", call_ind ? "in progress" : "none", setup_str[callsetup_ind],
           br_alive ? "running" : "stopped");
    esp_bd_addr_t list[8];
    int n = bonded(list, 8);
    printf("  bonded devices: %d\n", n);
    for (int i = 0; i < n; i++) {
        printf("    ");
        pa(list[i]);
        printf("\n");
    }
    return 0;
}

static int bt_stats(void)
{
    printf("call audio: %u packets (%u bytes each) from the phone, %u packets (%u bytes each) to the phone\n",
           (unsigned)st_in_pkts, (unsigned)st_in_size, (unsigned)st_out_pkts, (unsigned)st_out_size);
    printf("earpiece side: %u gaps (buffer ran dry), %u times trimmed (too far behind), %u packets lost when full\n",
           (unsigned)st_rx_under, (unsigned)st_rx_trim, (unsigned)st_rx_drop);
    printf("microphone side: %u gaps (phone asked and none was ready), %u blocks lost when full\n", (unsigned)st_tx_under,
           (unsigned)st_tx_drop);
    printf("(each gap is a dropout you can hear: a few in a whole call is fine, several a second is not)\n");
    // Levels in dBFS (0 = full scale). Speech normally peaks around -6 to -15 dBFS with an RMS of -25 to -35 dBFS;
    // much lower than that from the phone is why the earpiece sounds coarse and quiet.
    double rx_rms = st_rx_n ? sqrt((double)st_rx_sumsq / (double)st_rx_n) : 0.0;
    double tx_rms = st_tx_n ? sqrt((double)st_tx_sumsq / (double)st_tx_n) : 0.0;
    printf("received from the phone (before gain): peak %.1f dBFS, average %.1f dBFS\n",
           st_rx_peak > 0 ? 20.0 * log10(st_rx_peak / 32768.0) : -99.9, rx_rms > 0 ? 20.0 * log10(rx_rms / 32768.0) : -99.9);
    printf("sent to the phone (after gain): peak %.1f dBFS, average %.1f dBFS (includes silence)\n",
           st_tx_peak > 0 ? 20.0 * log10(st_tx_peak / 32768.0) : -99.9, tx_rms > 0 ? 20.0 * log10(tx_rms / 32768.0) : -99.9);
    printf("digital gain now: earpiece %+.1f dB, microphone %+.1f dB\n", 20.0 * log10(rx_gain), 20.0 * log10(tx_gain));
    return 0;
}

static int cmd_bt(int argc, char **argv)
{
    const char *s = argc >= 2 ? argv[1] : "";
    if (!strcmp(s, "on")) {
        return bt_start();
    } else if (!strcmp(s, "status")) {
        return bt_status();
    } else if (!strcmp(s, "stats")) {
        return bt_stats();
    } else if (!strcmp(s, "iocap") && argc >= 3) {
        if (!strcmp(argv[2], "in")) {
            iocap = ESP_BT_IO_CAP_IN;
        } else if (!strcmp(argv[2], "io")) {
            iocap = ESP_BT_IO_CAP_IO;
        } else if (!strcmp(argv[2], "out")) {
            iocap = ESP_BT_IO_CAP_OUT;
        } else if (!strcmp(argv[2], "none")) {
            iocap = ESP_BT_IO_CAP_NONE;
        } else {
            printf("usage: bt iocap in|io|out|none\n");
            return 1;
        }
#if CONFIG_BT_SSP_ENABLED
        if (bt_up) {
            esp_bt_sp_param_t ptype = ESP_BT_SP_IOCAP_MODE;
            esp_bt_gap_set_security_param(ptype, &iocap, sizeof(uint8_t));
        }
#endif
        printf("IO capability: %s\n", iocap_str[iocap]);
        return 0;
    } else if (!strcmp(s, "cod")) {
        // bt cod [minor [major [service, hex]]]: the class of device the phone sees while pairing
        if (argc >= 3) {
            cod_minor = atoi(argv[2]) & 0x3f;
            if (argc >= 4) {
                cod_major = atoi(argv[3]) & 0x1f;
            }
            if (argc >= 5) {
                cod_service = (int)strtol(argv[4], NULL, 16) & 0x7ff;
            }
            if (bt_up) {
                esp_bt_cod_t cod;
                cod.reserved_2 = 0;
                cod.minor = cod_minor;
                cod.major = cod_major;
                cod.reserved_8 = 0;
                cod.service = cod_service;
                esp_bt_gap_set_cod(cod, ESP_BT_INIT_COD);
            }
        }
        uint32_t code = ((uint32_t)cod_service << 13) | ((uint32_t)cod_major << 8) | ((uint32_t)cod_minor << 2);
        printf("class of device 0x%06x: major %d, minor %d, service field 0x%03x (audio/video minors: 1 headset, 2 hands-free, "
               "5 microphone, 6 loudspeaker, 7 headphones, 8 portable audio, 9 car audio, 11 hi-fi audio; service bits "
               "0x100 audio, 0x200 telephony, 0x020 rendering)%s\n",
               (unsigned)code, cod_major, cod_minor, cod_service,
               argc >= 3 ? "; forget the device on the phone and re-pair to see the effect" : "");
        return 0;
    } else if (!strcmp(s, "pair")) {
        if (!need_up()) {
            return 1;
        }
        int secs = argc >= 3 ? atoi(argv[2]) : 180;      // phones can take a long time to find it
        if (secs < 5 || secs > 900) {
            secs = 180;
        }
        pair_active = true;
        pair_until_us = esp_timer_get_time() + (int64_t)secs * 1000000;
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        printf("discoverable for %d s as '%s'. On the phone: Settings > Bluetooth, pick it. IO capability %s.\n", secs,
               DEVICE_NAME, iocap_str[iocap]);
        return 0;
    } else if (!strcmp(s, "nopair")) {
        if (!need_up()) {
            return 1;
        }
        pair_active = false;
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
        printf("not discoverable\n");
        return 0;
    } else if (!strcmp(s, "passkey") && argc >= 3) {
#if CONFIG_BT_SSP_ENABLED
        if (!need_up()) {
            return 1;
        }
        if (strlen(argv[2]) != 6 || strspn(argv[2], "0123456789") != 6) {
            printf("the passkey is exactly 6 digits\n");
            return 1;
        }
        esp_bt_gap_ssp_passkey_reply(ssp_addr, true, (uint32_t)atoi(argv[2]));
        printf("passkey sent\n");
        return 0;
#else
        printf("SSP is disabled in this build\n");
        return 1;
#endif
    } else if (!strcmp(s, "yes") || !strcmp(s, "no")) {
#if CONFIG_BT_SSP_ENABLED
        if (!need_up()) {
            return 1;
        }
        if (!strcmp(s, "yes")) {
            esp_bt_gap_ssp_confirm_reply(ssp_addr, true);
        } else {
            esp_bt_gap_ssp_confirm_reply(ssp_addr, false);
            esp_bt_gap_ssp_passkey_reply(ssp_addr, false, 0);
        }
        printf("reply sent: %s\n", s);
        return 0;
#else
        printf("SSP is disabled in this build\n");
        return 1;
#endif
    } else if (!strcmp(s, "list")) {
        return bt_status();
    } else if (!strcmp(s, "forget")) {
        if (!need_up()) {
            return 1;
        }
        esp_bd_addr_t list[8];
        int n = bonded(list, 8);
        if (slc && have_peer) {
            esp_hf_client_disconnect(peer);
        }
        for (int i = 0; i < n; i++) {
            esp_bt_gap_remove_bond_device(list[i]);
        }
        printf("removed %d bond(s). Also remove '%s' from the phone's Bluetooth list before pairing again.\n", n, DEVICE_NAME);
        return 0;
    } else if (!strcmp(s, "connect")) {
        if (!need_up()) {
            return 1;
        }
        esp_bd_addr_t a;
        if (argc >= 3) {
            if (!parse_addr(argv[2], a)) {
                printf("address as aa:bb:cc:dd:ee:ff\n");
                return 1;
            }
        } else {
            esp_bd_addr_t list[4];
            if (bonded(list, 4) < 1) {
                printf("no bonded device: pair first ('bt pair')\n");
                return 1;
            }
            memcpy(a, list[0], 6);
        }
        printf("connecting to ");
        pa(a);
        printf("\n");
        esp_hf_client_connect(a);
        return 0;
    } else if (!strcmp(s, "disconnect")) {
        if (!need_up() || !have_peer) {
            return 1;
        }
        esp_hf_client_disconnect(peer);
        return 0;
    } else if (!strcmp(s, "auto") && argc >= 3) {
        auto_connect = !strcmp(argv[2], "on");
        printf("auto-connect %s\n", auto_connect ? "on" : "off");
        return 0;
    } else if (!strcmp(s, "dial") && argc >= 3) {
        if (!need_slc()) {
            return 1;
        }
        printf("dialing %s\n", argv[2]);
        esp_hf_client_dial(argv[2]);
        return 0;
    } else if (!strcmp(s, "answer")) {
        if (!need_slc()) {
            return 1;
        }
        esp_hf_client_answer_call();
        return 0;
    } else if (!strcmp(s, "hangup")) {
        if (!need_slc()) {
            return 1;
        }
        esp_hf_client_reject_call();
        return 0;
    } else if (!strcmp(s, "dtmf") && argc >= 3) {
        if (!need_slc()) {
            return 1;
        }
        esp_hf_client_send_dtmf(argv[2][0]);
        return 0;
    } else if (!strcmp(s, "calls")) {
        if (!need_slc()) {
            return 1;
        }
        esp_hf_client_query_current_calls();
        return 0;
    } else if (!strcmp(s, "audio") && argc >= 3) {
        if (!need_slc()) {
            return 1;
        }
        if (!strcmp(argv[2], "on")) {
            esp_hf_client_connect_audio(peer);
        } else {
            esp_hf_client_disconnect_audio(peer);
        }
        return 0;
    } else if (!strcmp(s, "vol") && argc >= 4) {
        if (!need_slc()) {
            return 1;
        }
        int v = atoi(argv[3]);
        if (v < 0 || v > 15) {
            printf("volume 0 to 15\n");
            return 1;
        }
        esp_hf_client_volume_update(!strcmp(argv[2], "mic") ? ESP_HF_VOLUME_CONTROL_TARGET_MIC : ESP_HF_VOLUME_CONTROL_TARGET_SPK, v);
        printf("told the phone %s volume %d\n", argv[2], v);
        return 0;
    } else if (!strcmp(s, "gain") && argc >= 4) {
        // bt gain rx|tx <dB>: digital gain on the call audio (rx = phone to earpiece, tx = microphone to phone)
        float db = (float)atof(argv[3]);
        if (db < -30.0f || db > 30.0f) {
            printf("gain -30 to +30 dB\n");
            return 1;
        }
        float lin = powf(10.0f, db / 20.0f);
        if (!strcmp(argv[2], "rx")) {
            rx_gain = lin;
        } else if (!strcmp(argv[2], "tx")) {
            tx_gain = lin;
        } else {
            printf("usage: bt gain rx|tx <dB>\n");
            return 1;
        }
        printf("digital gain %s %+.1f dB (clips at full scale)\n", argv[2], db);
        return 0;
    } else if (!strcmp(s, "nrec")) {
        if (!need_slc()) {
            return 1;
        }
        esp_hf_client_send_nrec();
        printf("asked the phone to turn off its own noise reduction and echo cancellation (the main firmware does this today)\n");
        return 0;
    }
    printf("usage: bt on | status | stats | iocap in|io|out|none | cod [minor [major [service hex]]] | pair [s] | nopair | passkey <6 digits> | yes | no | forget |\n"
           "          connect [addr] | disconnect | auto on|off | dial <n> | answer | hangup | dtmf <c> | calls | audio on|off |\n"
           "          vol spk|mic <0-15> | gain rx|tx <dB> | nrec      (equalizer profiles: see the 'eqp' command)\n");
    return 1;
}

void register_bt_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "bt",
        .help = "Bluetooth handsfree: bt on | status | stats | iocap | pair | passkey | yes | no | forget | connect | dial | answer | hangup | dtmf | audio | vol (type 'bt' for the list)",
        .func = cmd_bt,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
