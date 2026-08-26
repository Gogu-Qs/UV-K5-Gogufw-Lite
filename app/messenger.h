#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/keyboard.h"

#define MSG_TEXT_LEN 36u
#define MSG_DRAFT_CAPACITY 5u
#define MSG_CALLSIGN_LEN 6u

typedef enum {
    MSG_SCREEN_HOME = 0,
    MSG_SCREEN_INBOX,
    MSG_SCREEN_SENT,
    MSG_SCREEN_DRAFTS,
    MSG_SCREEN_COMPOSE,
    MSG_SCREEN_READ,
    MSG_SCREEN_RANGE,
    MSG_SCREEN_CALLSIGN,
    MSG_SCREEN_SETTINGS,
} MSG_Screen_t;

typedef enum {
    MSG_STATUS_PENDING = 0,
    MSG_STATUS_ACKED,
    MSG_STATUS_FAILED,
    MSG_STATUS_NONE,
} MSG_Status_t;

void MSG_Open(void);
void MSG_RangeOpen(void);
void MSG_OpenCallsign(void);
void MSG_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);
void MSG_Tick10ms(void);
void MSG_RestoreVoiceForPTT(void);
void MSG_EnableRX(bool enable);
void MSG_StorePacket(uint16_t interrupt_bits);
bool MSG_HasUnread(void);
void MSG_LoadConfig(void);
void MSG_SaveConfigNow(void);

extern bool gMessengerEnabled;
extern bool gMessengerRangeRsp;
extern bool gMessengerCallsignTx;
extern bool gMessengerAck;
extern uint8_t gMessengerHop;
extern bool gMessengerBeep;
extern uint8_t gMessengerLed;

MSG_Screen_t MSG_GetScreen(void);
uint8_t MSG_GetHomeCursor(void);
uint8_t MSG_GetListCursor(void);
uint8_t MSG_GetListScroll(void);
uint8_t MSG_GetSettingsCursor(void);
uint8_t MSG_GetRangeStatus(void);
uint8_t MSG_GetRangeScroll(void);
bool MSG_GetRangeFound(void);
uint8_t MSG_GetHeardCount(void);
const char *MSG_GetHeardCallsign(uint8_t idx);
uint16_t MSG_GetHeardAge(uint8_t idx);
int16_t MSG_GetHeardRssi(uint8_t idx);
uint8_t MSG_GetHeardType(uint8_t idx);
const char *MSG_GetComposeText(void);
uint8_t MSG_GetComposeLen(void);
uint8_t MSG_GetT9Mode(void); /* 0=b, 1=B, 2=2 */
void MSG_SetCallsignFromMenuEdit(const char *text);
const char *MSG_GetCallsign(void);
const char *MSG_GetCallsignEdit(void);
uint8_t MSG_GetCallsignMode(void);
uint8_t MSG_GetDraftCount(void);
const char *MSG_GetDraft(uint8_t idx);
uint8_t MSG_GetInboxCount(void);
uint8_t MSG_GetSentCount(void);
const char *MSG_GetListText(uint8_t idx);
MSG_Status_t MSG_GetSentStatus(uint8_t idx);
bool MSG_GetInboxUnread(uint8_t idx);
uint16_t MSG_GetListAge(uint8_t idx);
uint8_t MSG_GetReadSource(void);
uint8_t MSG_GetReadIndex(void);
const char *MSG_GetReadText(void);
const char *MSG_GetReadFrom(void);
MSG_Status_t MSG_GetReadStatus(void);
uint16_t MSG_GetReadAge(void);
