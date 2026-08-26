/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include <string.h>

#if !defined(ENABLE_OVERLAY)
	#include "ARMCM0.h"
#endif
#ifdef ENABLE_FMRADIO
	#include "app/fm.h"
#endif
#include "app/uart.h"
#include "board.h"
#include "bsp/dp32g030/dma.h"
#include "bsp/dp32g030/gpio.h"
#ifndef ENABLE_UART_CHIRP_LITE
	#include "driver/aes.h"
#endif
#include "driver/backlight.h"
#include "driver/bk4819.h"
#include "driver/crc.h"
#include "driver/eeprom.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "version.h"

#if defined(ENABLE_OVERLAY)
	#include "sram-overlay.h"
#endif


#define DMA_INDEX(x, y) (((x) + (y)) % sizeof(UART_DMA_Buffer))

typedef struct {
	uint16_t ID;
	uint16_t Size;
} Header_t;

typedef struct {
	uint8_t  Padding[2];
	uint16_t ID;
} Footer_t;

typedef struct {
	Header_t Header;
	uint32_t Timestamp;
} CMD_0514_t;

typedef struct {
	Header_t Header;
	struct {
		char     Version[16];
		bool     bHasCustomAesKey;
		bool     bIsInLockScreen;
		uint8_t  Padding[2];
		uint32_t Challenge[4];
	} Data;
} REPLY_0514_t;

typedef struct {
	Header_t Header;
	uint16_t Offset;
	uint8_t  Size;
	uint8_t  Padding;
	uint32_t Timestamp;
} CMD_051B_t;

typedef struct {
	Header_t Header;
	struct {
		uint16_t Offset;
		uint8_t  Size;
		uint8_t  Padding;
		uint8_t  Data[128];
	} Data;
} REPLY_051B_t;

typedef struct {
	Header_t Header;
	uint16_t Offset;
	uint8_t  Size;
	bool     bAllowPassword;
	uint32_t Timestamp;
	uint8_t  Data[0];
} CMD_051D_t;

typedef struct {
	Header_t Header;
	struct {
		uint16_t Offset;
	} Data;
} REPLY_051D_t;

#ifndef ENABLE_UART_CHIRP_LITE
typedef struct {
	Header_t Header;
	struct {
		uint16_t RSSI;
		uint8_t  ExNoiseIndicator;
		uint8_t  GlitchIndicator;
	} Data;
} REPLY_0527_t;

typedef struct {
	Header_t Header;
	struct {
		uint16_t Voltage;
		uint16_t Current;
	} Data;
} REPLY_0529_t;

typedef struct {
	Header_t Header;
	uint32_t Response[4];
} CMD_052D_t;

typedef struct {
	Header_t Header;
	struct {
		bool bIsLocked;
		uint8_t Padding[3];
	} Data;
} REPLY_052D_t;

typedef struct {
	Header_t Header;
	uint32_t Timestamp;
} CMD_052F_t;
#endif

#define MSG_DRAFT_EEPROM_START 0x1C00u
#define MSG_DRAFT_EEPROM_END   0x1CC8u

static const uint8_t Obfuscation[16] =
{
	0x16, 0x6C, 0x14, 0xE6, 0x2E, 0x91, 0x0D, 0x40, 0x21, 0x35, 0xD5, 0x40, 0x13, 0x03, 0xE9, 0x80
};

static union
{
	uint8_t Buffer[256];
	struct
	{
		Header_t Header;
		uint8_t Data[252];
	};
} UART_Command;

static uint16_t gUART_WriteIndex;
#ifndef ENABLE_UART_CHIRP_LITE
static uint32_t Timestamp;
static bool bIsEncrypted = true;
#endif

static void SendReply(void *pReply, uint16_t Size)
{
	Header_t Header;
	Footer_t Footer;

	uint8_t *pBytes = (uint8_t *)pReply;
	#ifdef ENABLE_UART_CHIRP_LITE
		for (unsigned int i = 0; i < Size; i++)
			pBytes[i] ^= Obfuscation[i % 16];
	#else
		if (bIsEncrypted)
			for (unsigned int i = 0; i < Size; i++)
				pBytes[i] ^= Obfuscation[i % 16];
	#endif

	Header.ID = 0xCDAB;
	Header.Size = Size;
	UART_Send(&Header, sizeof(Header));
	UART_Send(pReply, Size);

	#ifndef ENABLE_UART_CHIRP_LITE
		if (!bIsEncrypted) {
			Footer.Padding[0] = 0xFF;
			Footer.Padding[1] = 0xFF;
		} else
	#endif
	{
		Footer.Padding[0] = Obfuscation[(Size + 0) % 16] ^ 0xFF;
		Footer.Padding[1] = Obfuscation[(Size + 1) % 16] ^ 0xFF;
	}
	Footer.ID = 0xBADC;

	UART_Send(&Footer, sizeof(Footer));
}

static void SendVersion(void)
{
	REPLY_0514_t Reply = {0};

	Reply.Header.ID = 0x0515;
	Reply.Header.Size = sizeof(Reply.Data);
	strcpy(Reply.Data.Version, Version);
	#ifndef ENABLE_UART_CHIRP_LITE
		Reply.Data.bHasCustomAesKey = bHasCustomAesKey;
		Reply.Data.bIsInLockScreen = bIsInLockScreen;
		Reply.Data.Challenge[0] = gChallenge[0];
		Reply.Data.Challenge[1] = gChallenge[1];
		Reply.Data.Challenge[2] = gChallenge[2];
		Reply.Data.Challenge[3] = gChallenge[3];
	#endif

	SendReply(&Reply, sizeof(Reply));
}

#ifndef ENABLE_UART_CHIRP_LITE
static bool IsBadChallenge(const uint32_t *pKey, const uint32_t *pIn, const uint32_t *pResponse)
{
	unsigned int i;
	uint32_t     IV[4];

	IV[0] = 0;
	IV[1] = 0;
	IV[2] = 0;
	IV[3] = 0;

	AES_Encrypt(pKey, IV, pIn, IV, true);

	for (i = 0; i < 4; i++)
		if (IV[i] != pResponse[i])
			return true;

	return false;
}
#endif

// session init, sends back version info and state
// timestamp is a session id really
static void CMD_0514(const uint8_t *pBuffer)
{
	#ifdef ENABLE_UART_CHIRP_LITE
		(void)pBuffer;
	#else
		const CMD_0514_t *pCmd = (const CMD_0514_t *)pBuffer;
		Timestamp = pCmd->Timestamp;
	#endif

	gSerialConfigCountDown_500ms = 12; // 6 sec

	#ifndef ENABLE_UART_CHIRP_LITE
		#ifdef ENABLE_FMRADIO
			gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
		#endif
		BACKLIGHT_TurnOff();
	#endif

	SendVersion();
}

// read eeprom
static void CMD_051B(const uint8_t *pBuffer)
{
	const CMD_051B_t *pCmd = (const CMD_051B_t *)pBuffer;
	REPLY_051B_t      Reply;
	#ifndef ENABLE_UART_CHIRP_LITE
		if (pCmd->Timestamp != Timestamp)
			return;
	#endif

	gSerialConfigCountDown_500ms = 12; // 6 sec

	#ifndef ENABLE_UART_CHIRP_LITE
		#ifdef ENABLE_FMRADIO
			gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
		#endif
	#endif

	Reply.Header.ID   = 0x051C;
	Reply.Header.Size = pCmd->Size + 4;
	Reply.Data.Offset = pCmd->Offset;
	Reply.Data.Size   = pCmd->Size;
	Reply.Data.Padding = 0;

	#ifdef ENABLE_UART_CHIRP_LITE
		EEPROM_ReadBuffer(pCmd->Offset, Reply.Data.Data, pCmd->Size);
	#else
		bool bLocked = false;
		if (bHasCustomAesKey)
			bLocked = gIsLocked;
		if (!bLocked)
			EEPROM_ReadBuffer(pCmd->Offset, Reply.Data.Data, pCmd->Size);
	#endif

	SendReply(&Reply, pCmd->Size + 8);
}

// write eeprom
static void CMD_051D(const uint8_t *pBuffer)
{
	const CMD_051D_t *pCmd = (const CMD_051D_t *)pBuffer;
	REPLY_051D_t Reply;
	#ifndef ENABLE_UART_CHIRP_LITE
		if (pCmd->Timestamp != Timestamp)
			return;
	#endif
	#ifndef ENABLE_UART_CHIRP_LITE
		bool bReloadEeprom;
		bool bIsLocked;
	#endif

	gSerialConfigCountDown_500ms = 12; // 6 sec
	
	#ifndef ENABLE_UART_CHIRP_LITE
		bReloadEeprom = false;
	#endif

	#ifndef ENABLE_UART_CHIRP_LITE
		#ifdef ENABLE_FMRADIO
			gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
		#endif
	#endif

	Reply.Header.ID   = 0x051E;
	Reply.Header.Size = sizeof(Reply.Data);
	Reply.Data.Offset = pCmd->Offset;

	#ifndef ENABLE_UART_CHIRP_LITE
		bIsLocked = bHasCustomAesKey ? gIsLocked : bHasCustomAesKey;
		if (!bIsLocked)
	#endif
	{
		for (uint8_t i = 0;
		     i < pCmd->Size
		#ifdef ENABLE_UART_CHIRP_LITE
		     && (uint16_t)(pCmd->Offset + i) < MSG_DRAFT_EEPROM_START
		#endif
		     ; i += 8u)
		{
			const uint16_t Offset = pCmd->Offset + i;

			#ifndef ENABLE_UART_CHIRP_LITE
			/* Standard UV-K5 CHIRP images use 0x1C00 for DTMF contacts.
			 * GOGUFW Lite uses it for Messenger drafts, so never let a
			 * generic channel upload silently erase those drafts. */
			if ((uint16_t)(Offset - MSG_DRAFT_EEPROM_START) <
			    (MSG_DRAFT_EEPROM_END - MSG_DRAFT_EEPROM_START))
				continue;
			#endif

			#ifdef ENABLE_UART_CHIRP_LITE
				EEPROM_WriteBuffer(Offset, &pCmd->Data[i]);
			#else
				if (Offset >= 0x0F30 && Offset < 0x0F40)
					if (!gIsLocked)
						bReloadEeprom = true;

				if ((Offset < 0x0E98 || Offset >= 0x0EA0) || !bIsInLockScreen || pCmd->bAllowPassword)
					EEPROM_WriteBuffer(Offset, &pCmd->Data[i]);
			#endif
		}

		#ifndef ENABLE_UART_CHIRP_LITE
			if (bReloadEeprom)
				SETTINGS_InitEEPROM();
		#endif
	}

	SendReply(&Reply, sizeof(Reply));
}

#ifndef ENABLE_UART_CHIRP_LITE
// read RSSI
static void CMD_0527(void)
{
	REPLY_0527_t Reply;

	Reply.Header.ID             = 0x0528;
	Reply.Header.Size           = sizeof(Reply.Data);
	Reply.Data.RSSI             = BK4819_ReadRegister(BK4819_REG_67) & 0x01FF;
	Reply.Data.ExNoiseIndicator = BK4819_ReadRegister(BK4819_REG_65) & 0x007F;
	Reply.Data.GlitchIndicator  = BK4819_ReadRegister(BK4819_REG_63);

	SendReply(&Reply, sizeof(Reply));
}

// read ADC
static void CMD_0529(void)
{
	REPLY_0529_t Reply;

	Reply.Header.ID   = 0x52A;
	Reply.Header.Size = sizeof(Reply.Data);

	// Original doesn't actually send current!
	BOARD_ADC_GetBatteryInfo(&Reply.Data.Voltage, &Reply.Data.Current);

	SendReply(&Reply, sizeof(Reply));
}

static void CMD_052D(const uint8_t *pBuffer)
{
	const CMD_052D_t *pCmd = (const CMD_052D_t *)pBuffer;
	REPLY_052D_t      Reply;
	bool              bIsLocked;

	#ifdef ENABLE_FMRADIO
		gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
	#endif
	Reply.Header.ID   = 0x052E;
	Reply.Header.Size = sizeof(Reply.Data);

	bIsLocked = bHasCustomAesKey;

	if (!bIsLocked)
		bIsLocked = IsBadChallenge(gCustomAesKey, gChallenge, pCmd->Response);

	if (!bIsLocked)
	{
		bIsLocked = IsBadChallenge(gDefaultAesKey, gChallenge, pCmd->Response);
		if (bIsLocked)
			gTryCount++;
	}

	if (gTryCount < 3)
	{
		if (!bIsLocked)
			gTryCount = 0;
	}
	else
	{
		gTryCount = 3;
		bIsLocked = true;
	}
	
	gIsLocked            = bIsLocked;
	Reply.Data.bIsLocked = bIsLocked;

	SendReply(&Reply, sizeof(Reply));
}

// session init, sends back version info and state
// timestamp is a session id really
// this command also disables dual watch, crossband, 
// DTMF side tones, freq reverse, PTT ID, DTMF decoding, frequency offset
// exits power save, sets main VFO to upper,
static void CMD_052F(const uint8_t *pBuffer)
{
	const CMD_052F_t *pCmd = (const CMD_052F_t *)pBuffer;

	gEeprom.DUAL_WATCH                               = DUAL_WATCH_OFF;
	gEeprom.CROSS_BAND_RX_TX                         = CROSS_BAND_OFF;
	gEeprom.RX_VFO                                   = 0;
	gEeprom.DTMF_SIDE_TONE                           = false;
	gEeprom.VfoInfo[0].FrequencyReverse              = false;
	gEeprom.VfoInfo[0].pRX                           = &gEeprom.VfoInfo[0].freq_config_RX;
	gEeprom.VfoInfo[0].pTX                           = &gEeprom.VfoInfo[0].freq_config_TX;
	gEeprom.VfoInfo[0].TX_OFFSET_FREQUENCY_DIRECTION = TX_OFFSET_FREQUENCY_DIRECTION_OFF;
	gEeprom.VfoInfo[0].DTMF_PTT_ID_TX_MODE           = PTT_ID_OFF;
#ifdef ENABLE_DTMF_CALLING
	gEeprom.VfoInfo[0].DTMF_DECODING_ENABLE          = false;
#endif

	#ifdef ENABLE_NOAA
		gIsNoaaMode = false;
	#endif

	if (gCurrentFunction == FUNCTION_POWER_SAVE)
		FUNCTION_Select(FUNCTION_FOREGROUND);

	gSerialConfigCountDown_500ms = 12; // 6 sec

	Timestamp = pCmd->Timestamp;

	// turn the LCD backlight off
	BACKLIGHT_TurnOff();

	SendVersion();
}
#endif

#ifdef ENABLE_UART_RW_BK_REGS
static void CMD_0601_ReadBK4819Reg(const uint8_t *pBuffer)
{
	typedef struct  __attribute__((__packed__)) {
		Header_t header;
		uint8_t reg;
	} CMD_0601_t;

	CMD_0601_t *cmd = (CMD_0601_t*) pBuffer;

	struct __attribute__((__packed__)) {
		Header_t header;
		struct __attribute__((__packed__)) {
			uint8_t reg;
			uint16_t value;
		} data;
	} reply;

	reply.header.ID = 0x0601;
	reply.header.Size = sizeof(reply.data);
	reply.data.reg = cmd->reg;
	reply.data.value = BK4819_ReadRegister(cmd->reg);
	SendReply(&reply, sizeof(reply));
}

static void CMD_0602_WriteBK4819Reg(const uint8_t *pBuffer)
{
	typedef struct __attribute__((__packed__)) {
		Header_t header;
		uint8_t reg;
		uint16_t value;
	} CMD_0602_t;

	CMD_0602_t *cmd = (CMD_0602_t*) pBuffer;
	BK4819_WriteRegister(cmd->reg, cmd->value);
}
#endif

bool UART_IsCommandAvailable(void)
{
	const uint16_t DmaLength = DMA_CH0->ST & 0xFFFU;
	uint16_t CommandLength;

	while (1)
	{
		if (gUART_WriteIndex == DmaLength)
			return false;

		while (gUART_WriteIndex != DmaLength && UART_DMA_Buffer[gUART_WriteIndex] != 0xABU)
			gUART_WriteIndex = DMA_INDEX(gUART_WriteIndex, 1);

		if (gUART_WriteIndex == DmaLength)
			return false;

		if (gUART_WriteIndex < DmaLength)
			CommandLength = DmaLength - gUART_WriteIndex;
		else
			CommandLength = (DmaLength + sizeof(UART_DMA_Buffer)) - gUART_WriteIndex;

		if (CommandLength < 8)
			return 0;

		if (UART_DMA_Buffer[DMA_INDEX(gUART_WriteIndex, 1)] == 0xCD)
			break;

		gUART_WriteIndex = DMA_INDEX(gUART_WriteIndex, 1);
	}

	uint16_t Index = DMA_INDEX(gUART_WriteIndex, 2);
	const uint16_t Size = (UART_DMA_Buffer[DMA_INDEX(Index, 1)] << 8) | UART_DMA_Buffer[Index];

	if ((Size + 8u) > sizeof(UART_DMA_Buffer))
	{
		gUART_WriteIndex = DmaLength;
		return false;
	}

	if (CommandLength < (Size + 8))
		return false;

	Index = DMA_INDEX(Index, 2);
	const uint16_t TailIndex = DMA_INDEX(Index, Size + 2);

	#ifdef ENABLE_UART_CHIRP_LITE
		for (uint16_t i = 0; i < (Size + 2u); i++)
			UART_Command.Buffer[i] = UART_DMA_Buffer[DMA_INDEX(Index, i)] ^ Obfuscation[i % 16];
	#else
		for (uint16_t i = 0; i < (Size + 2u); i++)
			UART_Command.Buffer[i] = UART_DMA_Buffer[DMA_INDEX(Index, i)];

		if (UART_Command.Header.ID == 0x0514)
			bIsEncrypted = false;
		if (UART_Command.Header.ID == 0x6902)
			bIsEncrypted = true;
		if (bIsEncrypted)
			for (uint16_t i = 0; i < (Size + 2u); i++)
				UART_Command.Buffer[i] ^= Obfuscation[i % 16];
	#endif

	gUART_WriteIndex = DMA_INDEX(TailIndex, 2);

	const uint16_t CRC = UART_Command.Buffer[Size] | (UART_Command.Buffer[Size + 1] << 8);

	return (CRC_Calculate(UART_Command.Buffer, Size) != CRC) ? false : true;
}

void UART_HandleCommand(void)
{
	switch (UART_Command.Header.ID)
	{
		case 0x0514:
			CMD_0514(UART_Command.Buffer);
			break;
	
		case 0x051B:
			CMD_051B(UART_Command.Buffer);
			break;
	
		case 0x051D:
			CMD_051D(UART_Command.Buffer);
			break;
	
		#ifndef ENABLE_UART_CHIRP_LITE
		case 0x0527:
			CMD_0527();
			break;
	
		case 0x0529:
			CMD_0529();
			break;
	
		case 0x052D:
			CMD_052D(UART_Command.Buffer);
			break;
	
		case 0x052F:
			CMD_052F(UART_Command.Buffer);
			break;
		#endif
	
		case 0x05DD: // reset
			#if defined(ENABLE_OVERLAY)
				overlay_FLASH_RebootToBootloader();
			#else
				NVIC_SystemReset();
			#endif
			break;
			
#ifdef ENABLE_UART_RW_BK_REGS
		case 0x0601:
			CMD_0601_ReadBK4819Reg(UART_Command.Buffer);
			break;
		
		case 0x0602:
			CMD_0602_WriteBK4819Reg(UART_Command.Buffer);
			break;
#endif
	}
}
