#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app/messenger.h"
#include "app/generic.h"
#include "audio.h"
#include "driver/system.h"
#include "driver/bk4819.h"
#include "driver/bk4819-regs.h"
#include "driver/eeprom.h"
#include "ui/inputbox.h"
#include "functions.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "helper/battery.h"
#include "ui/ui.h"

#define MSG_SENT_CAPACITY 8u
#define MSG_INBOX_CAPACITY 8u
#define MSG_SETTINGS_COUNT 9u

#define MSG_FSK_WORDS       50u
#define MSG_PKT_WIRE_LEN    94u
#define MSG_PKT_MAGIC0      'G'
#define MSG_PKT_MAGIC1      'G'
#define MSG_PKT_MAGIC2      'M'
#define MSG_PKT_MAGIC3      '2'
#define MSG_PKT_VERSION     2u
#define MSG_TYPE_TEXT       1u
#define MSG_TYPE_ACK        2u
#define MSG_TYPE_PING       3u
#define MSG_TYPE_PONG       4u
#define MSG_PKT_TO_ALL      "ALL"
#define MSG_PKT_CALLSIGN_FIELD_LEN 8u /* UV-K1 0.6.5 RF field width; UI edit remains 6 chars */
#define MSG_RF_REG5D_LEN_100_BYTES    0x6300u
#define MSG_RF_REG59_RX_CLEAR        0x4068u
#define MSG_RF_REG59_RX_ENABLE       0x3068u
#define MSG_RF_REG59_TX_CLEAR_LONG_PRE 0x80F8u
#define MSG_RF_REG59_TX_READY_LONG_PRE 0x00F8u
#define MSG_RF_REG59_TX_START_LONG_PRE 0x28F8u

#define MSG_CFG_EEPROM_ADDR 0x1FA0u
#define MSG_CFG_MAGIC0 'G'
#define MSG_CFG_MAGIC1 'M'
#define MSG_CFG_MAGIC2 'G'
#define MSG_CFG_MAGIC3 '5'
#define MSG_DRAFT_EEPROM_ADDR 0x1C00u
#define MSG_DRAFT_SLOT_SIZE 40u
#define MSG_DRAFT_MARKER 0xD5u

typedef struct {
    char text[MSG_TEXT_LEN + 1u];
    MSG_Status_t status;
    uint16_t id;
    uint16_t age_seconds;
    bool unread;
    char from[MSG_PKT_CALLSIGN_FIELD_LEN + 1u];
} MSG_Item_t;

typedef struct {
    char call[MSG_PKT_CALLSIGN_FIELD_LEN + 1u];
    uint16_t age_seconds;
    int16_t rssi_dbm;
    uint8_t type;
} MSG_HeardItem_t;

#define MSG_HEARD_CAPACITY 5u
#define MSG_HEARD_PAGE_ROWS 3u

static MSG_Screen_t gMsgScreen = MSG_SCREEN_HOME;
static uint8_t gMsgCursor;
static uint8_t gMsgScroll;
static uint8_t gMsgHomeCursor;
static uint8_t gMsgSettingsCursor;
static uint8_t gMsgReadSource;
static uint8_t gMsgReadIndex;
static uint8_t gMsgRangeStatus; /* RF16b: 0=HEARD, 1=WAIT/listen */
static uint8_t gMsgRangeScroll;
static uint16_t gMsgRangeWait10ms;
static bool gMsgRangeFound;
static MSG_HeardItem_t gHeard[MSG_HEARD_CAPACITY];
static uint8_t gHeardCount;
static uint8_t gMsgAgeSubTicks;
static uint16_t gMsgLedBlinkTicks;
static uint8_t gMsgLedBlinkPhase;
static bool gMsgLedOwned;
static bool gMsgUnreadIcon;
static uint8_t gMsgBeepPending;

static MSG_Item_t gInbox[MSG_INBOX_CAPACITY];
static uint8_t gInboxCount;
static MSG_Item_t gSent[MSG_SENT_CAPACITY];
static uint8_t gSentCount;

static char gDrafts[MSG_DRAFT_CAPACITY][MSG_TEXT_LEN + 1u] = {
    "OK", "CALL ME", "WHERE ARE YOU", "ON MY WAY", "SAFE"
};

static char gMsgComposeBuf[MSG_TEXT_LEN + 1u];
static uint8_t gMsgComposeLen;
static bool gMsgComposeIsDraftEdit;
static uint8_t gMsgComposeDraftIndex;

static char gCallsign[MSG_CALLSIGN_LEN + 1u] = "UVK5";
static char gCallsignEdit[MSG_CALLSIGN_LEN + 1u];
static uint8_t gCallsignLen;
static void redraw(void);
static uint8_t current_count(void);


static uint8_t gMsgT9Mode = 1u; /* UV-K1 compose starts with upper-case B mode */
static KEY_Code_t gMsgT9LastKey = KEY_INVALID;
static uint8_t gMsgT9TapIndex;
static uint8_t gMsgT9Timeout10ms;
static uint8_t gCallsignT9Mode = 1u;
static KEY_Code_t gCallsignT9LastKey = KEY_INVALID;
static uint8_t gCallsignT9TapIndex;
static uint8_t gCallsignT9Timeout10ms;

bool gMessengerEnabled = true;
bool gMessengerRangeRsp = true;
bool gMessengerCallsignTx = true;
bool gMessengerAck = true;
uint8_t gMessengerHop = 0u;
bool gMessengerBeep = true;
uint8_t gMessengerLed = 1u;
bool gMessengerDebug = false;

static uint16_t gMsgTxBuf[MSG_FSK_WORDS];
static uint16_t gMsgNextId = 1u;

/* RF10 safety scheduler: keep RF7b TX path, do not enable FSK RX/stock RX hooks.
   This only fixes UI sent status timing (? -> retry -> x) without touching voice RX. */
#define MSG_ACK_WAIT_10MS      400u /* UV-K1 0.6.5: 4.0s */
#define MSG_ACK_DELAY_MIN_10MS  80u  /* UV-K1: delayed ACK, 800ms minimum */
#define MSG_ACK_DELAY_JIT_10MS 220u  /* +0..2.2s jitter => 0.8..3.0s */
#define MSG_RANGE_PONG_MIN_10MS 300u /* UV-K1: 3s minimum PONG delay */
#define MSG_RANGE_PONG_JIT_10MS 500u /* +0..5s jitter => 3..8s */
#define MSG_REPEAT_GAP_MS       100u

static bool gMsgAckPending;
static bool gMsgAckRetried;
static uint16_t gMsgAckWait10ms;
static uint16_t gMsgWaitAckId;
static char gMsgAckText[MSG_TEXT_LEN + 1u];

static bool gPendingAckActive;
static uint16_t gPendingAckDelay10ms;
static uint16_t gPendingAckId;
static char gPendingAckTo[MSG_PKT_CALLSIGN_FIELD_LEN + 1u];
static uint8_t gPendingAckVfo = 0xFFu;

static bool gPendingPongActive;
static uint16_t gPendingPongDelay10ms;
static uint16_t gPendingPongId;
static char gPendingPongTo[MSG_PKT_CALLSIGN_FIELD_LEN + 1u];
static uint8_t gPendingPongVfo = 0xFFu;

#define MSG_SEEN_CACHE 8u
typedef struct { char from[MSG_PKT_CALLSIGN_FIELD_LEN + 1u]; uint16_t id; uint8_t type; } MSG_Seen_t;
static MSG_Seen_t gSeen[MSG_SEEN_CACHE];
static uint8_t gSeenNext;

/* RF13: NUNU 20.5-style BK4819 receive layer, with GOGUFW frame/parser. */
typedef enum { MSG_RX_READY = 0, MSG_RX_RECEIVING } MSG_RxStatus_t;
static MSG_RxStatus_t gMsgRxStatus = MSG_RX_READY;
static uint8_t gMsgRxBytes[MSG_FSK_WORDS * 2u];
static uint8_t gMsgRxIndex;

static void MSG_StartAckWait(uint16_t id, const char *text);
static void MSG_MarkPendingFailed(void);
static void add_inbox(const char *text, uint16_t id, const char *from);
static void add_heard(const char *from, int16_t rssi_dbm, uint16_t voltage_cv, uint8_t type);
static void request_messenger_redraw(void);
static void MSG_RFSendRepeat(uint8_t type, const char *text, uint16_t id, const char *to, uint8_t count, uint16_t gap_ms);
static void MSG_RFSendRepeatOnVfo(uint8_t type, const char *text, uint16_t id, const char *to, uint8_t count, uint16_t gap_ms, uint8_t tx_vfo);
static void MSG_QueueAck(uint16_t id, const char *to);
static void MSG_QueuePong(uint16_t id, const char *to);
static uint16_t MSG_Jitter(uint16_t seed, uint16_t span);
static int16_t MSG_CurrentRSSIdBm(void);

static void MSG_SaveDrafts(void)
{
    uint8_t slot[MSG_DRAFT_SLOT_SIZE];
    for (uint8_t i = 0u; i < MSG_DRAFT_CAPACITY; i++) {
        memset(slot, 0xFF, sizeof(slot));
        memset(slot, 0, MSG_TEXT_LEN + 1u);
        strncpy((char *)slot, gDrafts[i], MSG_TEXT_LEN);
        slot[MSG_TEXT_LEN] = 0;
        slot[37] = MSG_DRAFT_MARKER;
        slot[38] = i;
        slot[39] = (uint8_t)(MSG_CFG_MAGIC0 ^ MSG_CFG_MAGIC1 ^ MSG_CFG_MAGIC2 ^ MSG_CFG_MAGIC3 ^ i);
        for (uint8_t off = 0u; off < MSG_DRAFT_SLOT_SIZE; off += 8u) {
            EEPROM_WriteBuffer((uint16_t)(MSG_DRAFT_EEPROM_ADDR + ((uint16_t)i * MSG_DRAFT_SLOT_SIZE) + off), &slot[off]);
        }
    }
}

static void MSG_LoadDrafts(void)
{
    uint8_t slot[MSG_DRAFT_SLOT_SIZE];
    bool valid = false;
    for (uint8_t i = 0u; i < MSG_DRAFT_CAPACITY; i++) {
        EEPROM_ReadBuffer((uint16_t)(MSG_DRAFT_EEPROM_ADDR + ((uint16_t)i * MSG_DRAFT_SLOT_SIZE)), slot, sizeof(slot));
        if (slot[37] == MSG_DRAFT_MARKER && slot[38] == i &&
            slot[39] == (uint8_t)(MSG_CFG_MAGIC0 ^ MSG_CFG_MAGIC1 ^ MSG_CFG_MAGIC2 ^ MSG_CFG_MAGIC3 ^ i)) {
            memset(gDrafts[i], 0, sizeof(gDrafts[i]));
            for (uint8_t j = 0u; j < MSG_TEXT_LEN && slot[j] >= 32u && slot[j] <= 126u; j++) {
                gDrafts[i][j] = (char)slot[j];
            }
            valid = true;
        }
    }
    if (!valid) MSG_SaveDrafts();
}

static void MSG_SaveConfig(void)
{
    uint8_t block0[8];
    uint8_t block1[8];
    memset(block0, 0xFF, sizeof(block0));
    memset(block1, 0xFF, sizeof(block1));
    block0[0] = MSG_CFG_MAGIC0;
    block0[1] = MSG_CFG_MAGIC1;
    block0[2] = MSG_CFG_MAGIC2;
    block0[3] = MSG_CFG_MAGIC3;
    block0[4] = (gMessengerEnabled ? 0x01u : 0u)
              | (gMessengerRangeRsp ? 0x02u : 0u)
              | (gMessengerCallsignTx ? 0x04u : 0u)
              | (gMessengerAck ? 0x08u : 0u)
              | (gMessengerBeep ? 0x10u : 0u)
              | (gMessengerDebug ? 0x20u : 0u);
    block0[5] = (gMessengerLed <= 2u) ? gMessengerLed : 1u;
    block0[6] = (gMessengerHop <= 5u) ? gMessengerHop : 0u;
    block0[7] = 0xA5u;
    for (uint8_t i = 0u; i < MSG_CALLSIGN_LEN && gCallsign[i]; i++) block1[i] = (uint8_t)gCallsign[i];
    block1[MSG_CALLSIGN_LEN] = 0;
    EEPROM_WriteBuffer(MSG_CFG_EEPROM_ADDR, block0);
    EEPROM_WriteBuffer(MSG_CFG_EEPROM_ADDR + 8u, block1);
}

void MSG_LoadConfig(void)
{
    uint8_t block0[8];
    uint8_t block1[8];
    EEPROM_ReadBuffer(MSG_CFG_EEPROM_ADDR, block0, sizeof(block0));
    if (block0[0] != MSG_CFG_MAGIC0 || block0[1] != MSG_CFG_MAGIC1 ||
        block0[2] != MSG_CFG_MAGIC2 || block0[3] != MSG_CFG_MAGIC3 || block0[7] != 0xA5u) {
        MSG_SaveConfig();
        MSG_LoadDrafts();
        return;
    }
    gMessengerEnabled    = (block0[4] & 0x01u) != 0u;
    gMessengerRangeRsp   = (block0[4] & 0x02u) != 0u;
    gMessengerCallsignTx = (block0[4] & 0x04u) != 0u;
    gMessengerAck        = (block0[4] & 0x08u) != 0u;
    gMessengerBeep       = (block0[4] & 0x10u) != 0u;
    gMessengerDebug      = (block0[4] & 0x20u) != 0u;
    gMessengerLed        = (block0[5] <= 2u) ? block0[5] : 1u;
    gMessengerHop        = (block0[6] <= 5u) ? block0[6] : 0u;

    EEPROM_ReadBuffer(MSG_CFG_EEPROM_ADDR + 8u, block1, sizeof(block1));
    memset(gCallsign, 0, sizeof(gCallsign));
    uint8_t n = 0u;
    while (n < MSG_CALLSIGN_LEN && block1[n] >= 32u && block1[n] <= 126u) {
        gCallsign[n] = (char)block1[n];
        n++;
    }
    if (n == 0u) {
        strcpy(gCallsign, "UVK5");
        MSG_SaveConfig();
    }
    MSG_LoadDrafts();

    /* Seed boot MsgID away from 1 to avoid UV-K1 duplicate filter collisions
       after repeated UV-K5 reboots/tests. Retry still uses the same MsgID. */
    {
        uint16_t seed = (uint16_t)(gBatteryVoltageAverage ? gBatteryVoltageAverage : gBatteryCurrentVoltage);
        seed ^= BK4819_ReadRegister(BK4819_REG_67);
        seed ^= (uint16_t)((uint16_t)gCallsign[0] << 8);
        seed ^= 0xA700u;
        if (seed < 0x1000u) seed = (uint16_t)(seed + 0x4000u);
        if (seed == 0u) seed = 1u;
        gMsgNextId = seed;
    }
}

void MSG_SaveConfigNow(void)
{
    MSG_SaveConfig();
}

static void MSG_NotifyIncoming(void)
{
    gMsgUnreadIcon = true;
    gMsgLedBlinkTicks = 1u;
    gMsgLedBlinkPhase = 0u;
    gUpdateStatus = true;
    if (gMessengerBeep) {
        gMsgBeepPending = 1u;
    }
}

static void MSG_LedSet(bool on)
{
    if (gMessengerLed == 0u) on = false;
    const bool red = (gMessengerLed == 2u);
    if (on) {
        gMsgLedOwned = true;
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
        BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, red);
    } else if (gMsgLedOwned) {
        gMsgLedOwned = false;
        if (!g_SquelchLost) BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
        BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    }
}

static uint16_t MSG_Jitter(uint16_t seed, uint16_t span)
{
    if (span == 0u) return 0u;
    uint16_t x = (uint16_t)(seed ^ gMsgNextId ^ (uint16_t)gMsgAgeSubTicks ^ 0x5A3Cu);
    x ^= (uint16_t)(x << 7);
    x ^= (uint16_t)(x >> 9);
    x ^= (uint16_t)(x << 8);
    return (uint16_t)(x % span);
}

static int16_t MSG_CurrentRSSIdBm(void)
{
    uint16_t r = (uint16_t)(BK4819_ReadRegister(BK4819_REG_67) & 0x01FFu);
    /* Approximate display value only; matches the compact Range/HEARD UI purpose. */
    return (int16_t)((int16_t)(r / 2u) - 160);
}

static void put_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t msg_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    while (len--) {
        crc ^= (uint16_t)(*data++) << 8;
        for (uint8_t i = 0u; i < 8u; i++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void copy_field(uint8_t *dst, uint8_t len, const char *src)
{
    memset(dst, 0, len);
    if (!src) return;
    for (uint8_t i = 0u; i < len && src[i]; i++) dst[i] = (uint8_t)src[i];
}

static uint8_t build_native_packet(uint8_t *out, uint8_t type, const char *text, uint16_t id, const char *to)
{
    memset(out, 0, MSG_PKT_WIRE_LEN);
    out[0] = MSG_PKT_MAGIC0;
    out[1] = MSG_PKT_MAGIC1;
    out[2] = MSG_PKT_MAGIC2;
    out[3] = MSG_PKT_MAGIC3;
    out[4] = MSG_PKT_VERSION;
    out[5] = type;
    out[6] = 0u;
    put_u16_le(&out[7], id);
    out[9] = (gMessengerHop != 0u) ? gMessengerHop : 1u;
    out[10] = out[9];
    copy_field(&out[11], MSG_PKT_CALLSIGN_FIELD_LEN, gCallsign[0] ? gCallsign : "UVK5");
    copy_field(&out[19], MSG_PKT_CALLSIGN_FIELD_LEN, (to && to[0]) ? to : MSG_PKT_TO_ALL);

    if (type == MSG_TYPE_TEXT && text) {
        uint8_t n = 0u;
        while (text[n] && n < MSG_TEXT_LEN) {
            out[28u + n] = (uint8_t)text[n];
            n++;
        }
        out[27] = n;
    } else if (type == MSG_TYPE_ACK) {
        out[27] = 2u;
        out[28] = (uint8_t)(id & 0xFFu);
        out[29] = (uint8_t)(id >> 8);
    } else if (type == MSG_TYPE_PONG) {
        uint16_t cv = gBatteryVoltageAverage ? gBatteryVoltageAverage : gBatteryCurrentVoltage;
        out[27] = 2u;
        out[28] = (uint8_t)(cv & 0xFFu);
        out[29] = (uint8_t)(cv >> 8);
    } else {
        out[27] = 0u;
    }

    uint16_t crc = msg_crc16(out, MSG_PKT_WIRE_LEN - 2u);
    put_u16_le(&out[MSG_PKT_WIRE_LEN - 2u], crc);
    return MSG_PKT_WIRE_LEN;
}

void MSG_RestoreVoiceForPTT(void);

static void MSG_RestoreVoicePath(void)
{
    BK4819_ResetFSK();
    BK4819_WriteRegister(BK4819_REG_59, 0x0068u);
    BK4819_WriteRegister(BK4819_REG_70, 0x0000u);
    BK4819_WriteRegister(BK4819_REG_58, 0x0000u);
    BK4819_ExitTxMute();
    BK4819_SetAF(BK4819_AF_MUTE);
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);
    RADIO_SetupRegisters(false);
}

void MSG_RestoreVoiceForPTT(void)
{
    MSG_EnableRX(false);
    MSG_RestoreVoicePath();
}

static void MSG_ConfigureFSK_GOGUFW(bool rx)
{
    /* RF14: UV-K1 0.6.5 physical FSK parity.
       Earlier RF13 used NUNU FSK700/sync values for RX while TX used the
       GOGUFW/Aircopy 1200 path. That meant UV-K5 could transmit frames that
       UV-K1 partly understood, but UV-K5 was listening for a different modem.
       Use the same Aircopy/GOGUFW modem setup on both TX and RX. */
    BK4819_WriteRegister(BK4819_REG_70, 0x00E0u);
    BK4819_WriteRegister(BK4819_REG_72, 0x3065u);
    BK4819_WriteRegister(BK4819_REG_58, 0x00C1u);
    BK4819_WriteRegister(BK4819_REG_5C, 0x5665u);
    BK4819_WriteRegister(BK4819_REG_5D, MSG_RF_REG5D_LEN_100_BYTES);
    if (rx) {
        BK4819_WriteRegister(BK4819_REG_5E, 0x3204u);
    }
    BK4819_WriteRegister(BK4819_REG_02, 0x0000u);
}

void MSG_EnableRX(bool enable)
{
    if (!gMessengerEnabled) enable = false;
    if (enable) {
        MSG_ConfigureFSK_GOGUFW(true);
        BK4819_WriteRegister(BK4819_REG_59, MSG_RF_REG59_RX_CLEAR);
        BK4819_WriteRegister(BK4819_REG_59, MSG_RF_REG59_RX_ENABLE);
        gMsgRxStatus = MSG_RX_READY;
        gMsgRxIndex = 0u;
    } else {
        gMsgRxStatus = MSG_RX_READY;
        gMsgRxIndex = 0u;
        BK4819_WriteRegister(BK4819_REG_70, 0x0000u);
        BK4819_WriteRegister(BK4819_REG_58, 0x0000u);
    }
}

static void copy_wire_cstr(char *dst, uint8_t dst_len, const uint8_t *src, uint8_t src_len)
{
    uint8_t n = src_len;
    if (n > dst_len) n = dst_len;
    memcpy(dst, src, n);
    dst[n] = 0;
    while (n > 0u && dst[n - 1u] == 0) n--;
    dst[n] = 0;
}

static bool parse_ack_text(const char *text, uint16_t *id)
{
    if (!text || !id) return false;
    if (text[0] != 'A' || text[1] != 'C' || text[2] != 'K' || text[3] != ':') return false;
    uint16_t v = 0u;
    for (uint8_t i = 0u; i < 4u; i++) {
        char c = text[4u + i];
        uint8_t n;
        if (c >= '0' && c <= '9') n = (uint8_t)(c - '0');
        else if (c >= 'A' && c <= 'F') n = (uint8_t)(10 + c - 'A');
        else if (c >= 'a' && c <= 'f') n = (uint8_t)(10 + c - 'a');
        else return false;
        v = (uint16_t)((v << 4) | n);
    }
    *id = v;
    return true;
}


static bool MSG_SeenRemembered(const char *from, uint16_t id, uint8_t type)
{
    for (uint8_t i = 0u; i < MSG_SEEN_CACHE; i++) {
        if (gSeen[i].id == id && gSeen[i].type == type &&
            strncmp(gSeen[i].from, from ? from : "", MSG_PKT_CALLSIGN_FIELD_LEN) == 0) {
            return true;
        }
    }
    return false;
}

static void MSG_RememberSeen(const char *from, uint16_t id, uint8_t type)
{
    gSeen[gSeenNext].id = id;
    gSeen[gSeenNext].type = type;
    memset(gSeen[gSeenNext].from, 0, sizeof(gSeen[gSeenNext].from));
    if (from && from[0]) strncpy(gSeen[gSeenNext].from, from, MSG_PKT_CALLSIGN_FIELD_LEN);
    gSeenNext = (uint8_t)((gSeenNext + 1u) % MSG_SEEN_CACHE);
}

static void ack_sent_id(uint16_t ack_id)
{
    for (uint8_t i = 0u; i < gSentCount; i++) {
        if (gSent[i].id == ack_id) {
            gSent[i].status = MSG_STATUS_ACKED;
            break;
        }
    }
    if (gMsgAckPending && ack_id == gMsgWaitAckId) {
        gMsgAckPending = false;
        gMsgAckRetried = false;
        gMsgAckWait10ms = 0u;
    }
    redraw();
}

static void MSG_HandlePacketBytes(const uint8_t *raw, uint8_t len)
{
    if (len < (MSG_PKT_WIRE_LEN + 6u)) return;

    const uint8_t *pkt = NULL;
    for (uint8_t i = 0u; i + MSG_PKT_WIRE_LEN + 5u < len; i += 2u) {
        uint16_t head = (uint16_t)raw[i] | ((uint16_t)raw[i + 1u] << 8);
        if (head != 0xABCDu) continue;
        uint8_t pkt_off = (uint8_t)(i + 2u);
        uint8_t crc_off = (uint8_t)(pkt_off + MSG_PKT_WIRE_LEN);
        uint8_t tail_off = (uint8_t)(crc_off + 2u);
        if (tail_off + 1u >= len) break;
        uint16_t outer = (uint16_t)raw[crc_off] | ((uint16_t)raw[crc_off + 1u] << 8);
        uint16_t tail = (uint16_t)raw[tail_off] | ((uint16_t)raw[tail_off + 1u] << 8);
        if (tail != 0xDCBAu) continue;
        if (outer != msg_crc16(&raw[pkt_off], MSG_PKT_WIRE_LEN)) continue;
        pkt = &raw[pkt_off];
        break;
    }
    if (!pkt) return;
    if (pkt[0] != MSG_PKT_MAGIC0 || pkt[1] != MSG_PKT_MAGIC1 || pkt[2] != MSG_PKT_MAGIC2 || pkt[3] != MSG_PKT_MAGIC3) return;
    if (pkt[4] != MSG_PKT_VERSION) return;
    if (pkt[5] != MSG_TYPE_TEXT && pkt[5] != MSG_TYPE_ACK && pkt[5] != MSG_TYPE_PING && pkt[5] != MSG_TYPE_PONG) return;
    if (pkt[27] > MSG_TEXT_LEN) return;
    uint16_t inner = (uint16_t)pkt[MSG_PKT_WIRE_LEN - 2u] | ((uint16_t)pkt[MSG_PKT_WIRE_LEN - 1u] << 8);
    if (inner != msg_crc16(pkt, MSG_PKT_WIRE_LEN - 2u)) return;

    const uint8_t type = pkt[5];
    const uint8_t rx_vfo = gEeprom.RX_VFO;
    const uint16_t id = (uint16_t)pkt[7] | ((uint16_t)pkt[8] << 8);
    char from[MSG_PKT_CALLSIGN_FIELD_LEN + 1u];
    copy_wire_cstr(from, MSG_PKT_CALLSIGN_FIELD_LEN, &pkt[11], MSG_PKT_CALLSIGN_FIELD_LEN);

    if (type == MSG_TYPE_ACK) {
        uint16_t ack_id = id;
        if (pkt[27] >= 2u) ack_id = (uint16_t)pkt[28] | ((uint16_t)pkt[29] << 8);
        add_heard(from, MSG_CurrentRSSIdBm(), 0u, MSG_TYPE_ACK);
        ack_sent_id(ack_id);
    } else if (type == MSG_TYPE_TEXT) {
        char txt[MSG_TEXT_LEN + 1u];
        uint8_t n = pkt[27];
        if (n > MSG_TEXT_LEN) n = MSG_TEXT_LEN;
        memcpy(txt, &pkt[28], n);
        txt[n] = 0;
        uint16_t ack_text_id;
        if (parse_ack_text(txt, &ack_text_id)) {
            ack_sent_id(ack_text_id);
            return;
        }
        add_heard(from, MSG_CurrentRSSIdBm(), 0u, MSG_TYPE_TEXT);
        if (!MSG_SeenRemembered(from, id, MSG_TYPE_TEXT)) {
            MSG_RememberSeen(from, id, MSG_TYPE_TEXT);
            add_inbox(txt[0] ? txt : "MSG", id, from);
        }
        if (gMessengerAck) { gPendingAckVfo = rx_vfo; MSG_QueueAck(id, from); }
        redraw();
    } else if (type == MSG_TYPE_PING) {
        add_heard(from, MSG_CurrentRSSIdBm(), 0u, MSG_TYPE_PING);
        if (gMessengerRangeRsp) { gPendingPongVfo = rx_vfo; MSG_QueuePong(id, from); }
        redraw();
    } else if (type == MSG_TYPE_PONG) {
        uint16_t remote_cv = 0u;
        if (pkt[27] >= 2u) remote_cv = (uint16_t)pkt[28] | ((uint16_t)pkt[29] << 8);
        add_heard(from, MSG_CurrentRSSIdBm(), remote_cv, MSG_TYPE_PONG);
        gMsgRangeStatus = 0u;
        gMsgRangeFound = true;
        gMsgRangeWait10ms = 0u;
        redraw();
    }
}

void MSG_StorePacket(uint16_t interrupt_bits)
{
    if (!gMessengerEnabled) return;
    const bool rx_sync = (interrupt_bits & BK4819_REG_02_FSK_RX_SYNC) != 0u;
    const bool rx_fifo_almost_full = (interrupt_bits & BK4819_REG_02_FSK_FIFO_ALMOST_FULL) != 0u;
    const bool rx_finished = (interrupt_bits & BK4819_REG_02_FSK_RX_FINISHED) != 0u;

    if (rx_sync) {
        gMsgRxIndex = 0u;
        memset(gMsgRxBytes, 0, sizeof(gMsgRxBytes));
        gMsgRxStatus = MSG_RX_RECEIVING;
    }

    if (rx_fifo_almost_full && gMsgRxStatus == MSG_RX_RECEIVING) {
        uint16_t count = BK4819_ReadRegister(BK4819_REG_5E) & 0x0007u;
        if (count == 0u) count = 1u;
        for (uint16_t i = 0u; i < count; i++) {
            uint16_t word = BK4819_ReadRegister(BK4819_REG_5F);
            if (gMsgRxIndex < sizeof(gMsgRxBytes)) gMsgRxBytes[gMsgRxIndex++] = (uint8_t)(word & 0xFFu);
            if (gMsgRxIndex < sizeof(gMsgRxBytes)) gMsgRxBytes[gMsgRxIndex++] = (uint8_t)(word >> 8);
        }
    }

    if (rx_finished) {
        while (gMsgRxIndex < sizeof(gMsgRxBytes)) {
            uint16_t word = BK4819_ReadRegister(BK4819_REG_5F);
            if (gMsgRxIndex < sizeof(gMsgRxBytes)) gMsgRxBytes[gMsgRxIndex++] = (uint8_t)(word & 0xFFu);
            if (gMsgRxIndex < sizeof(gMsgRxBytes)) gMsgRxBytes[gMsgRxIndex++] = (uint8_t)(word >> 8);
        }
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
        /* NUNU: clear FIFO, immediately re-enable RX, then process buffer. */
        BK4819_WriteRegister(BK4819_REG_59, MSG_RF_REG59_RX_CLEAR);
        BK4819_WriteRegister(BK4819_REG_59, MSG_RF_REG59_RX_ENABLE);
        gMsgRxStatus = MSG_RX_READY;
        if (gMsgRxIndex > 2u) MSG_HandlePacketBytes(gMsgRxBytes, gMsgRxIndex);
        gMsgRxIndex = 0u;
    }
}

static void MSG_RFSendOnceOnVfo(uint8_t type, const char *text, uint16_t id, const char *to, uint8_t tx_vfo)
{
    uint8_t old_tx_vfo = gEeprom.TX_VFO;
    VFO_Info_t *old_tx_ptr = gTxVfo;
    VFO_Info_t *old_current_ptr = gCurrentVfo;
    if (tx_vfo < 2u) {
        gEeprom.TX_VFO = tx_vfo;
        gTxVfo = &gEeprom.VfoInfo[tx_vfo];
        gCurrentVfo = gTxVfo;
    }
    uint8_t pkt[MSG_PKT_WIRE_LEN];
    build_native_packet(pkt, type, text, id, to);
    memset(gMsgTxBuf, 0, sizeof(gMsgTxBuf));
    /* UV-K1 GOGUFW 0.6.5 frame layout, byte-for-byte:
       word[0]  = 0xABCD lead marker
       word[1..47] = 94-byte native GGM2 packet
       word[48] = outer FSK frame CRC over the 94-byte packet
       word[49] = 0xDCBA tail marker */
    gMsgTxBuf[0] = 0xABCDu;
    for (uint8_t i = 0u; i < (MSG_PKT_WIRE_LEN / 2u); i++) {
        gMsgTxBuf[1u + i] = (uint16_t)pkt[i * 2u] | ((uint16_t)pkt[i * 2u + 1u] << 8);
    }
    gMsgTxBuf[1u + (MSG_PKT_WIRE_LEN / 2u)] = msg_crc16(pkt, MSG_PKT_WIRE_LEN);
    gMsgTxBuf[MSG_FSK_WORDS - 1u] = 0xDCBAu;

    /* Use the radio's normal TX preparation first. This is the part RF5 broke:
       direct PA/LED toggling is not enough; BK4819 must be in the normal TX link. */
    RADIO_SetTxParameters();
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
    BK4819_ExitTxMute();

    BK4819_SetupGOGUFW_FSK();
    BK4819_WriteRegister(BK4819_REG_3F, BK4819_REG_3F_FSK_TX_FINISHED);
    BK4819_WriteRegister(BK4819_REG_5D, MSG_RF_REG5D_LEN_100_BYTES);
    BK4819_WriteRegister(BK4819_REG_59, MSG_RF_REG59_TX_CLEAR_LONG_PRE);
    BK4819_WriteRegister(BK4819_REG_59, MSG_RF_REG59_TX_READY_LONG_PRE);
    for (uint8_t i = 0u; i < MSG_FSK_WORDS; i++) {
        BK4819_WriteRegister(BK4819_REG_5F, gMsgTxBuf[i]);
    }
    SYSTEM_DelayMs(20);
    BK4819_ExitTxMute();
    BK4819_WriteRegister(BK4819_REG_59, MSG_RF_REG59_TX_START_LONG_PRE);

    uint16_t timeout = 360u;
    while (timeout-- && (BK4819_ReadRegister(BK4819_REG_0C) & 1u) == 0u) {
        SYSTEM_DelayMs(5);
    }
    BK4819_WriteRegister(BK4819_REG_02, 0x0000);
    SYSTEM_DelayMs(20);
    MSG_RestoreVoicePath();
    if (tx_vfo < 2u) {
        gEeprom.TX_VFO = old_tx_vfo;
        gTxVfo = old_tx_ptr;
        gCurrentVfo = old_current_ptr;
    }
}

static void MSG_RFSendOnce(uint8_t type, const char *text, uint16_t id, const char *to)
{
    MSG_RFSendOnceOnVfo(type, text, id, to, 0xFFu);
}

static void MSG_RFSendRepeat(uint8_t type, const char *text, uint16_t id, const char *to, uint8_t count, uint16_t gap_ms)
{
    MSG_RFSendRepeatOnVfo(type, text, id, to, count, gap_ms, 0xFFu);
}

static void MSG_RFSendRepeatOnVfo(uint8_t type, const char *text, uint16_t id, const char *to, uint8_t count, uint16_t gap_ms, uint8_t tx_vfo)
{
    if (count == 0u) count = 1u;
    for (uint8_t i = 0u; i < count; i++) {
        MSG_RFSendOnceOnVfo(type, text, id, to, tx_vfo);
        if ((uint8_t)(i + 1u) < count) {
            SYSTEM_DelayMs(gap_ms);
        }
    }
}


static const char *t9_group(uint8_t mode, KEY_Code_t key)
{
    if (mode == 2u) return "";
    const bool upper = (mode == 1u);
    switch (key) {
        case KEY_2: return upper ? "ABC" : "abc";
        case KEY_3: return upper ? "DEF" : "def";
        case KEY_4: return upper ? "GHI" : "ghi";
        case KEY_5: return upper ? "JKL" : "jkl";
        case KEY_6: return upper ? "MNO" : "mno";
        case KEY_7: return upper ? "PQRS" : "pqrs";
        case KEY_8: return upper ? "TUV" : "tuv";
        case KEY_9: return upper ? "WXYZ" : "wxyz";
        default: return "";
    }
}

static void t9_commit_common(KEY_Code_t *last, uint8_t *tap, uint8_t *timeout)
{
    *last = KEY_INVALID;
    *tap = 0u;
    *timeout = 0u;
}

static void t9_append(char *buf, uint8_t *len, uint8_t max, char c)
{
    if (*len < max) {
        buf[*len] = c;
        (*len)++;
        buf[*len] = 0;
    }
}

static void t9_delete(char *buf, uint8_t *len, KEY_Code_t *last, uint8_t *tap, uint8_t *timeout)
{
    t9_commit_common(last, tap, timeout);
    if (*len > 0u) {
        (*len)--;
        buf[*len] = 0;
    }
}

static void t9_handle(char *buf, uint8_t *len, uint8_t max, uint8_t *mode,
                      KEY_Code_t *last, uint8_t *tap, uint8_t *timeout, KEY_Code_t key)
{
    if (key == KEY_0) {
        t9_commit_common(last, tap, timeout);
        t9_append(buf, len, max, (*mode == 2u) ? '0' : ' ');
        return;
    }
    if (key == KEY_F) {
        t9_delete(buf, len, last, tap, timeout);
        return;
    }
    if (key == KEY_STAR) {
        t9_commit_common(last, tap, timeout);
        *mode = (uint8_t)((*mode + 1u) % 3u);
        return;
    }
    if (key < KEY_1 || key > KEY_9) return;

    if (*mode == 2u) {
        t9_commit_common(last, tap, timeout);
        t9_append(buf, len, max, (char)('0' + (key - KEY_0)));
        return;
    }

    const char *grp = (key == KEY_1) ? ".,?!" : t9_group(*mode, key);
    const uint8_t n = (uint8_t)strlen(grp);
    if (n == 0u) return;

    if (*last == key && *len > 0u && *timeout > 0u) {
        *tap = (uint8_t)((*tap + 1u) % n);
        buf[*len - 1u] = grp[*tap];
    } else {
        t9_commit_common(last, tap, timeout);
        *last = key;
        *tap = 0u;
        t9_append(buf, len, max, grp[0]);
    }
    *timeout = 80u;
}

void MSG_Tick10ms(void)
{
    if (gMsgT9Timeout10ms > 0u && --gMsgT9Timeout10ms == 0u) {
        t9_commit_common(&gMsgT9LastKey, &gMsgT9TapIndex, &gMsgT9Timeout10ms);
    }
    if (gCallsignT9Timeout10ms > 0u && --gCallsignT9Timeout10ms == 0u) {
        t9_commit_common(&gCallsignT9LastKey, &gCallsignT9TapIndex, &gCallsignT9Timeout10ms);
    }
    if (gMsgBeepPending && gMessengerBeep && gCurrentFunction != FUNCTION_RECEIVE && gCurrentFunction != FUNCTION_MONITOR) {
        AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP);
        gMsgBeepPending = 0u;
    }
    if (++gMsgAgeSubTicks >= 100u) {
        gMsgAgeSubTicks = 0u;
        for (uint8_t i = 0u; i < gInboxCount; i++) if (gInbox[i].age_seconds < 0xFFFFu) ++gInbox[i].age_seconds;
        for (uint8_t i = 0u; i < gSentCount; i++) if (gSent[i].age_seconds < 0xFFFFu) ++gSent[i].age_seconds;
        for (uint8_t hi = 0u; hi < gHeardCount; hi++) if (gHeard[hi].age_seconds < 0xFFFFu) ++gHeard[hi].age_seconds;
        if (gMsgScreen == MSG_SCREEN_SENT || gMsgScreen == MSG_SCREEN_INBOX || gMsgScreen == MSG_SCREEN_READ || gMsgScreen == MSG_SCREEN_RANGE) redraw();
    }
    if (gPendingAckActive) {
        if (gPendingAckDelay10ms > 0u) --gPendingAckDelay10ms;
        if (gPendingAckDelay10ms == 0u) {
            if (g_SquelchLost) {
                gPendingAckDelay10ms = 10u;
            } else {
                MSG_RFSendRepeatOnVfo(MSG_TYPE_ACK, NULL, gPendingAckId, gPendingAckTo, 1u, MSG_REPEAT_GAP_MS, gPendingAckVfo);
                gPendingAckActive = false;
                gPendingAckVfo = 0xFFu;
            }
        }
    }
    if (gPendingPongActive) {
        if (gPendingPongDelay10ms > 0u) --gPendingPongDelay10ms;
        if (gPendingPongDelay10ms == 0u) {
            if (g_SquelchLost) {
                gPendingPongDelay10ms = 10u;
            } else {
                MSG_RFSendRepeatOnVfo(MSG_TYPE_PONG, NULL, gPendingPongId, gPendingPongTo, 1u, MSG_REPEAT_GAP_MS, gPendingPongVfo);
                gPendingPongActive = false;
                gPendingPongVfo = 0xFFu;
            }
        }
    }
    if (gMsgUnreadIcon && gMessengerLed != 0u) {
        if (gMsgLedBlinkTicks > 0u) --gMsgLedBlinkTicks;
        if (gMsgLedBlinkTicks == 0u) {
            switch (gMsgLedBlinkPhase) {
                case 0u: MSG_LedSet(true);  gMsgLedBlinkTicks = 8u;  gMsgLedBlinkPhase = 1u; break;
                case 1u: MSG_LedSet(false); gMsgLedBlinkTicks = 14u; gMsgLedBlinkPhase = 2u; break;
                case 2u: MSG_LedSet(true);  gMsgLedBlinkTicks = 8u;  gMsgLedBlinkPhase = 3u; break;
                default: MSG_LedSet(false); gMsgLedBlinkTicks = 270u; gMsgLedBlinkPhase = 0u; break;
            }
        }
    } else {
        MSG_LedSet(false);
    }
    if (gMsgAckPending && gMsgAckWait10ms > 0u) {
        if (--gMsgAckWait10ms == 0u) {
            if (!gMsgAckRetried) {
                if (g_SquelchLost) {
                    gMsgAckWait10ms = 10u;
                } else {
                    gMsgAckRetried = true;
                    gMsgAckWait10ms = MSG_ACK_WAIT_10MS;
                    MSG_RFSendOnce(MSG_TYPE_TEXT, gMsgAckText[0] ? gMsgAckText : "EMPTY", gMsgWaitAckId, MSG_PKT_TO_ALL);
                    redraw();
                }
            } else {
                MSG_MarkPendingFailed();
            }
        }
    }
    if (gMsgRangeStatus == 1u && gMsgRangeWait10ms > 0u) {
        if (--gMsgRangeWait10ms == 0u) {
            gMsgRangeStatus = 0u;
            gMsgRangeFound = false;
            redraw();
        }
    }
}

static void request_messenger_redraw(void)
{
    gUpdateDisplay = true;
    if (gScreenToDisplay == DISPLAY_MESSENGER) {
        gRequestDisplayScreen = DISPLAY_MESSENGER;
    }
}

static void redraw(void)
{
    request_messenger_redraw();
}

static void go_home(void)
{
    gMsgScreen = MSG_SCREEN_HOME;
    gMsgCursor = 0u;
    gMsgScroll = 0u;
}

static void open_list(MSG_Screen_t screen)
{
    gMsgScreen = screen;
    gMsgCursor = 0u;
    gMsgScroll = 0u;
}

static void open_list_at(MSG_Screen_t screen, uint8_t idx)
{
    open_list(screen);
    uint8_t count = current_count();
    if (count == 0u) return;
    if (idx >= count) idx = (uint8_t)(count - 1u);
    gMsgCursor = idx;
    if (gMsgCursor >= 6u) gMsgScroll = (uint8_t)(gMsgCursor - 5u);
}

static uint8_t current_count(void)
{
    if (gMsgScreen == MSG_SCREEN_INBOX) return gInboxCount;
    if (gMsgScreen == MSG_SCREEN_SENT) return gSentCount;
    if (gMsgScreen == MSG_SCREEN_DRAFTS) return MSG_DRAFT_CAPACITY;
    return 0u;
}

static void list_move(int8_t dir)
{
    uint8_t count = current_count();
    if (count == 0u) return;
    int16_t next = (int16_t)gMsgCursor + dir;
    if (next < 0) next = (int16_t)count - 1;
    if (next >= count) next = 0;
    gMsgCursor = (uint8_t)next;
    if (gMsgCursor < gMsgScroll) gMsgScroll = gMsgCursor;
    if (gMsgCursor >= (uint8_t)(gMsgScroll + 6u)) gMsgScroll = (uint8_t)(gMsgCursor - 5u);
}

static void open_compose(const char *seed)
{
    memset(gMsgComposeBuf, 0, sizeof(gMsgComposeBuf));
    gMsgComposeLen = 0u;
    gMsgT9Mode = 1u;
    gMsgComposeIsDraftEdit = false;
    t9_commit_common(&gMsgT9LastKey, &gMsgT9TapIndex, &gMsgT9Timeout10ms);
    if (seed != NULL && seed[0] != 0) {
        strncpy(gMsgComposeBuf, seed, MSG_TEXT_LEN);
        gMsgComposeBuf[MSG_TEXT_LEN] = 0;
        gMsgComposeLen = (uint8_t)strlen(gMsgComposeBuf);
    }
    gMsgScreen = MSG_SCREEN_COMPOSE;
}

static void open_draft_edit(uint8_t idx)
{
    if (idx >= MSG_DRAFT_CAPACITY) idx = 0u;
    open_compose(gDrafts[idx]);
    gMsgComposeIsDraftEdit = true;
    gMsgComposeDraftIndex = idx;
}

static void add_sent(uint16_t id, const char *text)
{
    if (gSentCount >= MSG_SENT_CAPACITY) {
        for (uint8_t i = MSG_SENT_CAPACITY - 1u; i > 0u; i--) gSent[i] = gSent[i - 1u];
        gSentCount = MSG_SENT_CAPACITY - 1u;
    } else {
        for (uint8_t i = gSentCount; i > 0u; i--) gSent[i] = gSent[i - 1u];
    }
    strncpy(gSent[0].text, text, MSG_TEXT_LEN);
    gSent[0].text[MSG_TEXT_LEN] = 0;
    gSent[0].status = MSG_STATUS_PENDING;
    gSent[0].id = id;
    gSent[0].age_seconds = 0u;
    gSent[0].unread = false;
    strncpy(gSent[0].from, MSG_PKT_TO_ALL, MSG_PKT_CALLSIGN_FIELD_LEN);
    gSent[0].from[MSG_PKT_CALLSIGN_FIELD_LEN] = 0;
    gSentCount++;
}

static void MSG_MarkPendingFailed(void)
{
    for (uint8_t i = 0u; i < gSentCount; i++) {
        if (gSent[i].status == MSG_STATUS_PENDING && gSent[i].id == gMsgWaitAckId) { gSent[i].status = MSG_STATUS_FAILED; break; }
    }
    gMsgAckPending = false;
    gMsgAckRetried = false;
    gMsgAckWait10ms = 0u;
    redraw();
}

static void MSG_StartAckWait(uint16_t id, const char *text)
{
    memset(gMsgAckText, 0, sizeof(gMsgAckText));
    if (text != NULL) {
        strncpy(gMsgAckText, text, MSG_TEXT_LEN);
        gMsgAckText[MSG_TEXT_LEN] = 0;
    }
    gMsgWaitAckId = id;
    gMsgAckPending = true;
    gMsgAckRetried = false;
    gMsgAckWait10ms = MSG_ACK_WAIT_10MS;
}

static void MSG_QueueAck(uint16_t id, const char *to)
{
    if (!gMessengerAck) return;
    gPendingAckActive = true;
    gPendingAckDelay10ms = (uint16_t)(MSG_ACK_DELAY_MIN_10MS + MSG_Jitter(id, MSG_ACK_DELAY_JIT_10MS + 1u));
    gPendingAckId = id;
    memset(gPendingAckTo, 0, sizeof(gPendingAckTo));
    if (to && to[0]) strncpy(gPendingAckTo, to, MSG_PKT_CALLSIGN_FIELD_LEN);
    else strncpy(gPendingAckTo, MSG_PKT_TO_ALL, MSG_PKT_CALLSIGN_FIELD_LEN);
}

static void MSG_QueuePong(uint16_t id, const char *to)
{
    if (!gMessengerRangeRsp) return;
    gPendingPongActive = true;
    gPendingPongDelay10ms = (uint16_t)(MSG_RANGE_PONG_MIN_10MS + MSG_Jitter(id ^ 0xA55Au, MSG_RANGE_PONG_JIT_10MS + 1u));
    gPendingPongId = id;
    memset(gPendingPongTo, 0, sizeof(gPendingPongTo));
    if (to && to[0]) strncpy(gPendingPongTo, to, MSG_PKT_CALLSIGN_FIELD_LEN);
    else strncpy(gPendingPongTo, MSG_PKT_TO_ALL, MSG_PKT_CALLSIGN_FIELD_LEN);
}

static void add_heard(const char *from, int16_t rssi_dbm, uint16_t voltage_cv, uint8_t type)
{
    (void)voltage_cv;
    char call[MSG_PKT_CALLSIGN_FIELD_LEN + 1u];
    memset(call, 0, sizeof(call));
    if (from && from[0]) {
        strncpy(call, from, MSG_PKT_CALLSIGN_FIELD_LEN);
        call[MSG_PKT_CALLSIGN_FIELD_LEN] = 0;
    } else {
        strcpy(call, "UNKNOWN");
    }

    uint8_t pos = 0xFFu;
    for (uint8_t i = 0u; i < gHeardCount; i++) {
        if (strncmp(gHeard[i].call, call, MSG_PKT_CALLSIGN_FIELD_LEN) == 0) { pos = i; break; }
    }
    if (pos == 0xFFu) {
        if (gHeardCount < MSG_HEARD_CAPACITY) gHeardCount++;
        pos = (uint8_t)(gHeardCount - 1u);
    }
    for (uint8_t i = pos; i > 0u; i--) gHeard[i] = gHeard[i - 1u];
    strncpy(gHeard[0].call, call, MSG_PKT_CALLSIGN_FIELD_LEN);
    gHeard[0].call[MSG_PKT_CALLSIGN_FIELD_LEN] = 0;
    gHeard[0].age_seconds = 0u;
    gHeard[0].rssi_dbm = rssi_dbm;
    gHeard[0].type = type;
    if (gMsgScreen == MSG_SCREEN_RANGE) redraw();
}

static void add_inbox(const char *text, uint16_t id, const char *from)
{
    if (gInboxCount >= MSG_INBOX_CAPACITY) {
        for (uint8_t i = MSG_INBOX_CAPACITY - 1u; i > 0u; i--) gInbox[i] = gInbox[i - 1u];
        gInboxCount = MSG_INBOX_CAPACITY - 1u;
    } else {
        for (uint8_t i = gInboxCount; i > 0u; i--) gInbox[i] = gInbox[i - 1u];
    }
    strncpy(gInbox[0].text, text, MSG_TEXT_LEN);
    gInbox[0].text[MSG_TEXT_LEN] = 0;
    gInbox[0].status = MSG_STATUS_PENDING;
    gInbox[0].id = id;
    gInbox[0].age_seconds = 0u;
    gInbox[0].unread = true;
    if (from && from[0]) {
        strncpy(gInbox[0].from, from, MSG_PKT_CALLSIGN_FIELD_LEN);
        gInbox[0].from[MSG_PKT_CALLSIGN_FIELD_LEN] = 0;
    } else {
        strcpy(gInbox[0].from, "UNKNOWN");
    }
    gInboxCount++;
    MSG_NotifyIncoming();
}

static void open_sent_after_send(void)
{
    gMsgScreen = MSG_SCREEN_SENT;
    gMsgCursor = 0u;
    gMsgScroll = 0u;
}

static void close_messenger_to_main(void)
{
    /* Important on UV-K5: leave the app screen in a clean HOME state.
       Otherwise after Range Check, DISPLAY_MESSENGER may keep processing keys
       while gMsgScreen is still RANGE, so every key appears to reopen Range. */
    gMsgScreen = MSG_SCREEN_HOME;
    gMsgCursor = 0u;
    gMsgScroll = 0u;
    gMsgHomeCursor = 0u;
    gMsgRangeStatus = 0u;
    gMsgRangeWait10ms = 0u;
    gMsgRangeFound = false;
    gWasFKeyPressed = false;
    gRequestDisplayScreen = DISPLAY_MAIN;
    GUI_SelectNextDisplay(DISPLAY_MAIN);
    gUpdateDisplay = true;
    gUpdateStatus = true;
}

static void open_callsign(void)
{
    strncpy(gCallsignEdit, gCallsign, MSG_CALLSIGN_LEN);
    gCallsignEdit[MSG_CALLSIGN_LEN] = 0;
    gCallsignLen = (uint8_t)strlen(gCallsignEdit);
    gCallsignT9Mode = 1u;
    t9_commit_common(&gCallsignT9LastKey, &gCallsignT9TapIndex, &gCallsignT9Timeout10ms);
    gMsgScreen = MSG_SCREEN_CALLSIGN;
}

void MSG_Open(void)
{
    gMsgScreen = MSG_SCREEN_HOME;
    gMsgCursor = 0u;
    gMsgScroll = 0u;
    gMsgHomeCursor = 0u;
    gMsgRangeStatus = 0u;
    gMsgRangeWait10ms = 0u;
    gMsgRangeFound = false;
    gWasFKeyPressed = false;
    gRequestDisplayScreen = DISPLAY_MESSENGER;
    GUI_SelectNextDisplay(DISPLAY_MESSENGER);
    gUpdateDisplay = true;
}

void MSG_RangeOpen(void)
{
    gMsgScreen = MSG_SCREEN_RANGE;
    gMsgCursor = 0u;
    gMsgScroll = 0u;
    gMsgRangeScroll = 0u;
    gMsgRangeStatus = 0u;
    gMsgRangeWait10ms = 0u;
    gMsgRangeFound = false;
    gWasFKeyPressed = false;
    gRequestDisplayScreen = DISPLAY_MESSENGER;
    GUI_SelectNextDisplay(DISPLAY_MESSENGER);
    gUpdateDisplay = true;
}

void MSG_OpenCallsign(void)
{
    open_callsign();
    gInputBoxIndex = 0;
    gWasFKeyPressed = false;
    gRequestDisplayScreen = DISPLAY_MESSENGER;
    GUI_SelectNextDisplay(DISPLAY_MESSENGER);
    gUpdateDisplay = true;
}

bool MSG_HasUnread(void) { return gMsgUnreadIcon; }
MSG_Screen_t MSG_GetScreen(void) { return gMsgScreen; }
uint8_t MSG_GetHomeCursor(void) { return gMsgHomeCursor; }
uint8_t MSG_GetListCursor(void) { return gMsgCursor; }
uint8_t MSG_GetListScroll(void) { return gMsgScroll; }
uint8_t MSG_GetSettingsCursor(void) { return gMsgSettingsCursor; }
uint8_t MSG_GetRangeStatus(void) { return gMsgRangeStatus; }
uint8_t MSG_GetRangeScroll(void) { return gMsgRangeScroll; }
bool MSG_GetRangeFound(void) { return gMsgRangeFound; }
uint8_t MSG_GetHeardCount(void) { return gHeardCount; }
const char *MSG_GetHeardCallsign(uint8_t idx) { return (idx < gHeardCount) ? gHeard[idx].call : ""; }
uint16_t MSG_GetHeardAge(uint8_t idx) { return (idx < gHeardCount) ? gHeard[idx].age_seconds : 0u; }
int16_t MSG_GetHeardRssi(uint8_t idx) { return (idx < gHeardCount) ? gHeard[idx].rssi_dbm : -120; }
uint8_t MSG_GetHeardType(uint8_t idx) { return (idx < gHeardCount) ? gHeard[idx].type : 0u; }
const char *MSG_GetComposeText(void) { return gMsgComposeBuf; }
uint8_t MSG_GetComposeLen(void) { return gMsgComposeLen; }
uint8_t MSG_GetT9Mode(void) { return gMsgT9Mode; }
void MSG_SetCallsignFromMenuEdit(const char *text)
{
    uint8_t j = 0u;
    memset(gCallsign, 0, sizeof(gCallsign));
    for (uint8_t i = 0u; i < MSG_CALLSIGN_LEN && text[i]; i++) {
        char c = text[i];
        if (c == '_' || c == ' ') continue;
        gCallsign[j++] = c;
        if (j >= MSG_CALLSIGN_LEN) break;
    }
    if (j == 0u) strcpy(gCallsign, "UVK5");
    MSG_SaveConfig();
}

const char *MSG_GetCallsign(void) { return gCallsign; }
const char *MSG_GetCallsignEdit(void) { return gCallsignEdit; }
uint8_t MSG_GetCallsignMode(void) { return gCallsignT9Mode; }
uint8_t MSG_GetDraftCount(void) { return MSG_DRAFT_CAPACITY; }
const char *MSG_GetDraft(uint8_t idx) { return (idx < MSG_DRAFT_CAPACITY) ? gDrafts[idx] : ""; }
uint8_t MSG_GetInboxCount(void) { return gInboxCount; }
uint8_t MSG_GetSentCount(void) { return gSentCount; }
const char *MSG_GetListText(uint8_t idx)
{
    if (gMsgScreen == MSG_SCREEN_DRAFTS) return MSG_GetDraft(idx);
    if (gMsgScreen == MSG_SCREEN_SENT && idx < gSentCount) return gSent[idx].text;
    if (gMsgScreen == MSG_SCREEN_INBOX && idx < gInboxCount) return gInbox[idx].text;
    return "";
}
MSG_Status_t MSG_GetSentStatus(uint8_t idx) { return (idx < gSentCount) ? gSent[idx].status : MSG_STATUS_PENDING; }
bool MSG_GetInboxUnread(uint8_t idx) { return (idx < gInboxCount) ? gInbox[idx].unread : false; }

uint16_t MSG_GetListAge(uint8_t idx)
{
    if (gMsgScreen == MSG_SCREEN_SENT && idx < gSentCount) return gSent[idx].age_seconds;
    if (gMsgScreen == MSG_SCREEN_INBOX && idx < gInboxCount) return gInbox[idx].age_seconds;
    return 0u;
}
uint8_t MSG_GetReadSource(void) { return gMsgReadSource; }
uint8_t MSG_GetReadIndex(void) { return gMsgReadIndex; }
const char *MSG_GetReadText(void)
{
    if (gMsgReadSource == MSG_SCREEN_SENT && gMsgReadIndex < gSentCount) return gSent[gMsgReadIndex].text;
    if (gMsgReadSource == MSG_SCREEN_INBOX && gMsgReadIndex < gInboxCount) return gInbox[gMsgReadIndex].text;
    return "";
}

const char *MSG_GetReadFrom(void)
{
    if (gMsgReadSource == MSG_SCREEN_INBOX && gMsgReadIndex < gInboxCount) return gInbox[gMsgReadIndex].from;
    return MSG_PKT_TO_ALL;
}
MSG_Status_t MSG_GetReadStatus(void)
{
    if (gMsgReadSource == MSG_SCREEN_SENT && gMsgReadIndex < gSentCount) return gSent[gMsgReadIndex].status;
    return MSG_STATUS_PENDING;
}

uint16_t MSG_GetReadAge(void)
{
    if (gMsgReadSource == MSG_SCREEN_SENT && gMsgReadIndex < gSentCount) return gSent[gMsgReadIndex].age_seconds;
    if (gMsgReadSource == MSG_SCREEN_INBOX && gMsgReadIndex < gInboxCount) return gInbox[gMsgReadIndex].age_seconds;
    return 0u;
}

void MSG_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (Key == KEY_PTT) {
        if (bKeyPressed) {
            MSG_RestoreVoicePath();
        }
        GENERIC_Key_PTT(bKeyPressed);
        if (!bKeyPressed) {
            MSG_RestoreVoicePath();
        }
        return;
    }
    if (bKeyHeld) return;
    if (bKeyPressed) return;

    switch (gMsgScreen) {
        case MSG_SCREEN_HOME:
            if (Key == KEY_UP || Key == KEY_DOWN) {
                gMsgHomeCursor = (uint8_t)((gMsgHomeCursor + (Key == KEY_UP ? 3u : 1u)) % 4u);
            } else if (Key == KEY_MENU) {
                switch (gMsgHomeCursor) {
                    case 0: open_list(MSG_SCREEN_INBOX); break;
                    case 1: open_compose(""); break;
                    case 2: open_list(MSG_SCREEN_SENT); break;
                    case 3: open_list(MSG_SCREEN_DRAFTS); break;
                    default: break;
                }
            } else if (Key == KEY_EXIT) {
                close_messenger_to_main();
                return;
            }
            break;

        case MSG_SCREEN_INBOX:
        case MSG_SCREEN_SENT:
        case MSG_SCREEN_DRAFTS:
            if (Key == KEY_UP || Key == KEY_DOWN) list_move(Key == KEY_UP ? -1 : 1);
            else if (Key == KEY_MENU) {
                if (gMsgScreen == MSG_SCREEN_DRAFTS) open_draft_edit(gMsgCursor);
                else if (current_count() > 0u) {
                    gMsgReadSource = (uint8_t)gMsgScreen;
                    gMsgReadIndex = gMsgCursor;
                    if (gMsgScreen == MSG_SCREEN_INBOX && gMsgCursor < gInboxCount) {
                        gInbox[gMsgCursor].unread = false;
                        bool any_unread = false;
                        for (uint8_t ui = 0u; ui < gInboxCount; ui++) if (gInbox[ui].unread) any_unread = true;
                        gMsgUnreadIcon = any_unread;
                        if (!any_unread) { gMsgLedBlinkTicks = 0u; MSG_LedSet(false); }
                        gUpdateStatus = true;
                    }
                    gMsgScreen = MSG_SCREEN_READ;
                }
            } else if (Key == KEY_EXIT) go_home();
            else if (Key == KEY_F) {
                if (gMsgScreen == MSG_SCREEN_SENT && gSentCount > 0u) { gSentCount--; if (gMsgCursor >= gSentCount) gMsgCursor = 0u; }
                if (gMsgScreen == MSG_SCREEN_INBOX && gInboxCount > 0u) { gInboxCount--; if (gMsgCursor >= gInboxCount) gMsgCursor = 0u; }
            }
            break;

        case MSG_SCREEN_READ:
            if (Key == KEY_UP || Key == KEY_DOWN) {
                uint8_t count = (gMsgReadSource == MSG_SCREEN_SENT) ? gSentCount : gInboxCount;
                if (count > 0u) {
                    if (Key == KEY_UP) gMsgReadIndex = (gMsgReadIndex == 0u) ? (uint8_t)(count - 1u) : (uint8_t)(gMsgReadIndex - 1u);
                    else gMsgReadIndex = (uint8_t)((gMsgReadIndex + 1u) % count);
                    if (gMsgReadSource == MSG_SCREEN_INBOX && gMsgReadIndex < gInboxCount) {
                        gInbox[gMsgReadIndex].unread = false;
                        bool any_unread = false;
                        for (uint8_t ui = 0u; ui < gInboxCount; ui++) if (gInbox[ui].unread) any_unread = true;
                        gMsgUnreadIcon = any_unread;
                        if (!any_unread) { gMsgLedBlinkTicks = 0u; MSG_LedSet(false); }
                        gUpdateStatus = true;
                    }
                }
            } else if (Key == KEY_MENU) {
                if (gMsgReadSource == MSG_SCREEN_SENT) {
                    const char *rt = MSG_GetReadText()[0] ? MSG_GetReadText() : "EMPTY";
                    uint16_t id = gMsgNextId++;
                    MSG_RFSendOnce(MSG_TYPE_TEXT, rt, id, MSG_PKT_TO_ALL);
                    add_sent(id, rt);
                    MSG_StartAckWait(id, rt);
                    open_sent_after_send();
                } else {
                    open_compose("REPLY ");
                }
            } else if (Key == KEY_F) {
                if (gMsgReadSource == MSG_SCREEN_SENT && gSentCount > 0u) gSentCount--;
                if (gMsgReadSource == MSG_SCREEN_INBOX && gInboxCount > 0u) gInboxCount--;
                open_list_at((MSG_Screen_t)gMsgReadSource, gMsgReadIndex);
            } else if (Key == KEY_EXIT) {
                open_list_at((MSG_Screen_t)gMsgReadSource, gMsgReadIndex);
            }
            break;

        case MSG_SCREEN_COMPOSE:
            if (Key == KEY_MENU) {
                t9_commit_common(&gMsgT9LastKey, &gMsgT9TapIndex, &gMsgT9Timeout10ms);
                if (gMsgComposeIsDraftEdit) {
                    strncpy(gDrafts[gMsgComposeDraftIndex], gMsgComposeBuf, MSG_TEXT_LEN);
                    gDrafts[gMsgComposeDraftIndex][MSG_TEXT_LEN] = 0;
                    MSG_SaveDrafts();
                }
                const char *tx = gMsgComposeBuf[0] ? gMsgComposeBuf : "EMPTY";
                uint16_t id = gMsgNextId++;
                MSG_RFSendOnce(MSG_TYPE_TEXT, tx, id, MSG_PKT_TO_ALL);
                add_sent(id, tx);
                MSG_StartAckWait(id, tx);
                open_sent_after_send();
            } else if (Key == KEY_EXIT) {
                t9_commit_common(&gMsgT9LastKey, &gMsgT9TapIndex, &gMsgT9Timeout10ms);
                go_home();
            } else {
                t9_handle(gMsgComposeBuf, &gMsgComposeLen, MSG_TEXT_LEN, &gMsgT9Mode,
                          &gMsgT9LastKey, &gMsgT9TapIndex, &gMsgT9Timeout10ms, Key);
            }
            break;

        case MSG_SCREEN_RANGE:
            if (Key == KEY_EXIT) {
                close_messenger_to_main(); return;
            } else if (Key == KEY_MENU) {
                if (gMsgRangeStatus != 1u) { MSG_RFSendRepeat(MSG_TYPE_PING, NULL, gMsgNextId++, MSG_PKT_TO_ALL, 2u, 700u); gMsgRangeStatus = 1u; gMsgRangeWait10ms = 1200u; }
            } else if (Key == KEY_UP || Key == KEY_DOWN) {
                uint8_t pages = (uint8_t)((gHeardCount + MSG_HEARD_PAGE_ROWS - 1u) / MSG_HEARD_PAGE_ROWS);
                if (pages == 0u) pages = 1u;
                if (Key == KEY_UP) gMsgRangeScroll = (gMsgRangeScroll == 0u) ? (uint8_t)(pages - 1u) : (uint8_t)(gMsgRangeScroll - 1u);
                else gMsgRangeScroll = (uint8_t)((gMsgRangeScroll + 1u) % pages);
            }
            break;

        case MSG_SCREEN_CALLSIGN:
            if (Key == KEY_MENU) {
                t9_commit_common(&gCallsignT9LastKey, &gCallsignT9TapIndex, &gCallsignT9Timeout10ms);
                strncpy(gCallsign, gCallsignEdit, MSG_CALLSIGN_LEN);
                gCallsign[MSG_CALLSIGN_LEN] = 0;
                MSG_SaveConfig();
                go_home();
            } else if (Key == KEY_EXIT) {
                t9_commit_common(&gCallsignT9LastKey, &gCallsignT9TapIndex, &gCallsignT9Timeout10ms);
                go_home();
            } else {
                t9_handle(gCallsignEdit, &gCallsignLen, MSG_CALLSIGN_LEN, &gCallsignT9Mode,
                          &gCallsignT9LastKey, &gCallsignT9TapIndex, &gCallsignT9Timeout10ms, Key);
            }
            break;

        case MSG_SCREEN_SETTINGS:
            if (Key == KEY_UP || Key == KEY_DOWN) {
                gMsgSettingsCursor = (uint8_t)((gMsgSettingsCursor + (Key == KEY_UP ? (MSG_SETTINGS_COUNT - 1u) : 1u)) % MSG_SETTINGS_COUNT);
            } else if (Key == KEY_MENU) {
                switch (gMsgSettingsCursor) {
                    case 0: gMessengerEnabled = !gMessengerEnabled; MSG_SaveConfig(); break;
                    case 1: open_callsign(); break;
                    case 2: gMessengerCallsignTx = !gMessengerCallsignTx; MSG_SaveConfig(); break;
                    case 3: gMessengerAck = !gMessengerAck; MSG_SaveConfig(); break;
                    case 4: gMessengerHop = (uint8_t)((gMessengerHop + 1u) % 6u); MSG_SaveConfig(); break;
                    case 5: gMessengerBeep = !gMessengerBeep; MSG_SaveConfig(); break;
                    case 6: gMessengerLed = (uint8_t)((gMessengerLed + 1u) % 3u); MSG_SaveConfig(); break;
                    case 7: gMessengerDebug = !gMessengerDebug; MSG_SaveConfig(); break;
                    case 8: go_home(); break;
                    default: break;
                }
            } else if (Key == KEY_EXIT) go_home();
            break;

        default:
            go_home();
            break;
    }

    gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
    redraw();
}
