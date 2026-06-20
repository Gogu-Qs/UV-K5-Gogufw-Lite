#include "dtmf.h"
#include <string.h>

char gDTMF_String[15];
char gDTMF_InputBox[15];
uint8_t gDTMF_InputBox_Index;
bool gDTMF_InputMode;
uint8_t gDTMF_PreviousIndex;
char gDTMF_RX_live[20];
uint8_t gDTMF_RX_live_timeout;
DTMF_ReplyState_t gDTMF_ReplyState;

bool DTMF_ValidateCodes(char *pCode, const unsigned int size)
{
    if (pCode == NULL || size == 0) return false;
    for (unsigned int i = 0; i < size; i++) {
        const char c = pCode[i];
        if (c == 0 || c == 0xff) return false;
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'D') || c == '*' || c == '#' || c == '-' || c == ' ')) return false;
    }
    return true;
}

char DTMF_GetCharacter(const unsigned int code)
{
    static const char tbl[] = "0123456789ABCD*#";
    return (code < sizeof(tbl) - 1) ? tbl[code] : '-';
}

void DTMF_clear_input_box(void)
{
    memset(gDTMF_InputBox, '-', sizeof(gDTMF_InputBox));
    gDTMF_InputBox_Index = 0;
    gDTMF_InputMode = false;
    gDTMF_PreviousIndex = 0;
}

void DTMF_Append(const char code)
{
    if (gDTMF_InputBox_Index < sizeof(gDTMF_InputBox)) {
        gDTMF_InputBox[gDTMF_InputBox_Index++] = code;
    }
}

void DTMF_Reply(void) {}
void DTMF_SendEndOfTransmission(void) {}
