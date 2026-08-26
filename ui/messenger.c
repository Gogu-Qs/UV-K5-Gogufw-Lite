#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app/messenger.h"
#include "driver/st7565.h"
#include "font.h"
#include "ui/helper.h"
#include "ui/messenger.h"
#include "ui/ui.h"

static void px(uint8_t x, uint8_t y, bool on)
{
    if (x >= 128u || y >= 64u) return;
    const uint8_t line = (uint8_t)(y >> 3);
    const uint8_t mask = (uint8_t)(1u << (y & 7u));
    if (on) gFrameBuffer[line][x] |= mask;
    else gFrameBuffer[line][x] &= (uint8_t)~mask;
}

static void fill(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool on)
{
    if (x1 > 127u) x1 = 127u;
    if (y1 > 63u) y1 = 63u;
    for (uint8_t y = y0; y <= y1; y++) for (uint8_t x = x0; x <= x1; x++) px(x, y, on);
}

static void hline(uint8_t x0, uint8_t x1, uint8_t y, bool on)
{
    if (x1 > 127u) x1 = 127u;
    for (uint8_t x = x0; x <= x1; x++) px(x, y, on);
}

static void draw_dots(uint8_t y)
{
    for (uint8_t x = 0u; x < 128u; x = (uint8_t)(x + 4u)) hline(x, (uint8_t)(x + 1u), y, true);
}

static void small3(const char *s, uint8_t x, uint8_t y, bool inv)
{
    uint8_t len = (uint8_t)strlen(s);
    if (inv) fill(x ? (uint8_t)(x - 1u) : 0u, y ? (uint8_t)(y - 1u) : 0u,
                  (uint8_t)(x + len * 4u), (uint8_t)(y + 5u), true);
    while (*s && x < 128u) {
        uint8_t c = (uint8_t)*s++;
        if (c >= 0x20u && c <= 0x7fu) {
            const uint8_t *g = gFont3x5[c - 0x20u];
            for (uint8_t col = 0u; col < 3u; col++) {
                uint8_t bits = g[col];
                for (uint8_t row = 0u; row < 5u; row++) {
                    if (bits & (1u << row)) px((uint8_t)(x + col), (uint8_t)(y + row), !inv);
                }
            }
        }
        x = (uint8_t)(x + 4u);
    }
}

static void text7(const char *s, uint8_t x, uint8_t y, bool inv)
{
    const uint8_t pitch = 7u;
    uint8_t len = (uint8_t)strlen(s);
    if (inv) fill(x ? (uint8_t)(x - 1u) : 0u, y ? (uint8_t)(y - 1u) : 0u,
                  (uint8_t)(x + len * pitch + 1u), (uint8_t)(y + 7u), true);
    while (*s && x < 128u) {
        uint8_t c = (uint8_t)*s++;
        if (c > ' ' && c < 127u) {
            const uint8_t *glyph = gFontSmall[c - ' ' - 1u];
            for (uint8_t col = 0u; col < 6u; col++) {
                uint8_t bits = glyph[col];
                for (uint8_t row = 0u; row < 7u; row++) if (bits & (1u << row)) px((uint8_t)(x + col), (uint8_t)(y + row), !inv);
            }
        }
        x = (uint8_t)(x + pitch);
    }
}

static void title(const char *s)
{
    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    uint8_t len = (uint8_t)strlen(s);
    uint8_t x = (len >= 18u) ? 0u : (uint8_t)((128u - len * 7u) / 2u);
    UI_PrintStringSmallBold(s, x, 0, 0);
}

static void u8toa(uint8_t v, char *b)
{
    if (v >= 100u) { b[0] = (char)('0' + v / 100u); b[1] = (char)('0' + (v / 10u) % 10u); b[2] = (char)('0' + v % 10u); b[3] = 0; }
    else if (v >= 10u) { b[0] = (char)('0' + v / 10u); b[1] = (char)('0' + v % 10u); b[2] = 0; }
    else { b[0] = (char)('0' + v); b[1] = 0; }
}

static void append(char *dst, const char *src, uint8_t max)
{
    uint8_t d = (uint8_t)strlen(dst);
    while (*src && d + 1u < max) dst[d++] = *src++;
    dst[d] = 0;
}

static void age(uint16_t sec, char *b)
{
    if (sec < 60u) { strcpy(b, "NOW"); return; }
    if (sec < 3600u) { u8toa((uint8_t)(sec / 60u), b); append(b, "m", 5u); return; }
    u8toa((uint8_t)(sec / 3600u), b); append(b, "h", 5u);
}

static void count_str(uint8_t a, uint8_t b, char *out)
{
    out[0] = 0;
    u8toa(a, out);
    append(out, "/", 8u);
    char t[4]; u8toa(b, t); append(out, t, 8u);
}

static void right7(const char *s, uint8_t line)
{
    uint8_t len = (uint8_t)strlen(s);
    uint8_t x = (len * 7u >= 120u) ? 0u : (uint8_t)(122u - len * 7u);
    UI_PrintStringSmallNormal(s, x, 0, line);
}

static void right3(const char *s, uint8_t y)
{
    uint8_t len = (uint8_t)strlen(s);
    uint8_t x = (len * 4u >= 128u) ? 0u : (uint8_t)(127u - len * 4u);
    small3(s, x, y, false);
}

static void row7(const char *s, uint8_t line_no, bool sel)
{
    char safe[19];
    strncpy(safe, s, sizeof(safe) - 1u);
    safe[sizeof(safe) - 1u] = 0;
    text7(safe, 1, (uint8_t)(line_no * 8u), sel);
}

static void row_y(const char *s, uint8_t y, bool sel)
{
    char safe[19];
    strncpy(safe, s, sizeof(safe) - 1u);
    safe[sizeof(safe) - 1u] = 0;
    text7(safe, 1, y, sel);
}

static void wrap_y(const char *s, uint8_t y, uint8_t max_lines)
{
    char buf[19];
    for (uint8_t line_no = 0u; line_no < max_lines; line_no++) {
        if (!*s) break;
        uint8_t n = 0u;
        while (s[n] && n < 18u) { buf[n] = s[n]; n++; }
        buf[n] = 0;
        text7(buf, 0, (uint8_t)(y + line_no * 8u), false);
        s += n;
    }
}

#if 0 /* RF16b: right-side home icons removed to save flash */
static void icon_env(uint8_t x, uint8_t y)
{
    hline(x, (uint8_t)(x + 29u), y, true); hline(x, (uint8_t)(x + 29u), (uint8_t)(y + 17u), true);
    vline(x, y, (uint8_t)(y + 17u), true); vline((uint8_t)(x + 29u), y, (uint8_t)(y + 17u), true);
    line((uint8_t)(x + 1u), (uint8_t)(y + 2u), (uint8_t)(x + 14u), (uint8_t)(y + 10u), true);
    line((uint8_t)(x + 28u), (uint8_t)(y + 2u), (uint8_t)(x + 15u), (uint8_t)(y + 10u), true);
    line((uint8_t)(x + 1u), (uint8_t)(y + 16u), (uint8_t)(x + 11u), (uint8_t)(y + 9u), true);
    line((uint8_t)(x + 28u), (uint8_t)(y + 16u), (uint8_t)(x + 18u), (uint8_t)(y + 9u), true);
}

static void icon_pencil(uint8_t x, uint8_t y)
{
    for (uint8_t i = 0u; i < 20u; i++) { uint8_t pxv = (uint8_t)(x + 4u + i); uint8_t py = (uint8_t)(y + 23u - i); px(pxv, py, true); px((uint8_t)(pxv + 1u), py, true); px(pxv, (uint8_t)(py - 1u), true); px((uint8_t)(pxv + 1u), (uint8_t)(py - 1u), true); }
    line((uint8_t)(x + 2u), (uint8_t)(y + 25u), (uint8_t)(x + 6u), (uint8_t)(y + 23u), true);
    line((uint8_t)(x + 2u), (uint8_t)(y + 25u), (uint8_t)(x + 4u), (uint8_t)(y + 21u), true);
    px((uint8_t)(x + 1u), (uint8_t)(y + 26u), true);
    line((uint8_t)(x + 22u), (uint8_t)(y + 3u), (uint8_t)(x + 26u), y, true);
    line((uint8_t)(x + 24u), (uint8_t)(y + 6u), (uint8_t)(x + 29u), (uint8_t)(y + 2u), true);
    line((uint8_t)(x + 26u), y, (uint8_t)(x + 29u), (uint8_t)(y + 2u), true);
    line((uint8_t)(x + 22u), (uint8_t)(y + 3u), (uint8_t)(x + 24u), (uint8_t)(y + 6u), true);
    hline((uint8_t)(x + 10u), (uint8_t)(x + 29u), (uint8_t)(y + 27u), true);
}

static void icon_up(uint8_t x, uint8_t y)
{
    uint8_t cx = (uint8_t)(x + 15u);
    for (uint8_t r = 0u; r < 9u; r++) hline((uint8_t)(cx - r), (uint8_t)(cx + r), (uint8_t)(y + r), true);
    fill((uint8_t)(x + 12u), (uint8_t)(y + 9u), (uint8_t)(x + 18u), (uint8_t)(y + 22u), true);
    hline((uint8_t)(x + 4u), (uint8_t)(x + 26u), (uint8_t)(y + 26u), true);
}

static void icon_floppy(uint8_t x, uint8_t y)
{
    hline(x, (uint8_t)(x + 28u), y, true); line((uint8_t)(x + 29u), y, (uint8_t)(x + 33u), (uint8_t)(y + 4u), true);
    vline((uint8_t)(x + 33u), (uint8_t)(y + 4u), (uint8_t)(y + 23u), true); hline(x, (uint8_t)(x + 33u), (uint8_t)(y + 23u), true); vline(x, y, (uint8_t)(y + 23u), true);
    hline((uint8_t)(x + 4u), (uint8_t)(x + 23u), (uint8_t)(y + 3u), true); hline((uint8_t)(x + 4u), (uint8_t)(x + 23u), (uint8_t)(y + 9u), true);
    vline((uint8_t)(x + 4u), (uint8_t)(y + 3u), (uint8_t)(y + 9u), true); vline((uint8_t)(x + 23u), (uint8_t)(y + 3u), (uint8_t)(y + 9u), true);
    fill((uint8_t)(x + 17u), (uint8_t)(y + 4u), (uint8_t)(x + 20u), (uint8_t)(y + 7u), true);
    hline((uint8_t)(x + 6u), (uint8_t)(x + 27u), (uint8_t)(y + 14u), true); hline((uint8_t)(x + 6u), (uint8_t)(x + 27u), (uint8_t)(y + 22u), true);
    vline((uint8_t)(x + 6u), (uint8_t)(y + 14u), (uint8_t)(y + 22u), true); vline((uint8_t)(x + 27u), (uint8_t)(y + 14u), (uint8_t)(y + 22u), true);
    hline((uint8_t)(x + 9u), (uint8_t)(x + 24u), (uint8_t)(y + 17u), true); hline((uint8_t)(x + 9u), (uint8_t)(x + 24u), (uint8_t)(y + 19u), true);
}

#endif

static void draw_home(void)
{
    static const char *items[] = { "INBOX", "COMPOSE", "SENT", "DRAFTS" };
    title("MESSENGER");
    uint8_t cur = MSG_GetHomeCursor();
    for (uint8_t i = 0u; i < 4u; i++) row_y(items[i], (uint8_t)(10u + i * 9u), cur == i);
    draw_dots(46u);
    small3("SELECT", 0u, 49u, false);
}

static void draw_list(void)
{
    MSG_Screen_t sc = MSG_GetScreen();
    const char *t = (sc == MSG_SCREEN_SENT) ? "SENT" : ((sc == MSG_SCREEN_DRAFTS) ? "DRAFTS" : "INBOX");
    uint8_t count = (sc == MSG_SCREEN_SENT) ? MSG_GetSentCount() : ((sc == MSG_SCREEN_DRAFTS) ? MSG_GetDraftCount() : MSG_GetInboxCount());
    uint8_t cur = MSG_GetListCursor();
    uint8_t scr = MSG_GetListScroll();
    char buf[24], tmp[8];
    title(t);
    count_str(count ? (uint8_t)(cur + 1u) : 0u, count, tmp);
    right7(tmp, 0u);
    if (!count) { UI_PrintStringSmallNormal("EMPTY", 0, 0, 3); return; }
    for (uint8_t row = 0u; row < 6u; row++) {
        uint8_t idx = (uint8_t)(scr + row);
        if (idx >= count) break;
        memset(buf, ' ', 18u); buf[18] = 0;
        const char *text = (sc == MSG_SCREEN_DRAFTS) ? MSG_GetDraft(idx) : MSG_GetListText(idx);
        uint8_t p = 0u;
        if (sc == MSG_SCREEN_DRAFTS) { buf[p++] = (char)('1' + idx); buf[p++] = ' '; }
        else if (sc == MSG_SCREEN_SENT) {
            MSG_Status_t st = MSG_GetSentStatus(idx);
            if (st != MSG_STATUS_NONE)
                buf[p++] = (st == MSG_STATUS_ACKED) ? '+' : ((st == MSG_STATUS_FAILED) ? 'x' : '?');
        }
        else buf[p++] = MSG_GetInboxUnread(idx) ? '*' : ' ';
        for (uint8_t j = 0u; text[j] && p < 12u; j++) buf[p++] = text[j];
        if (sc != MSG_SCREEN_DRAFTS) { char a[5]; age(MSG_GetListAge(idx), a); uint8_t al = (uint8_t)strlen(a); uint8_t start = (al >= 4u) ? 14u : (uint8_t)(18u - al); for (uint8_t j = 0u; j < al && start + j < 18u; j++) buf[start + j] = a[j]; }
        row7(buf, (uint8_t)(row + 1u), idx == cur);
    }
}

static void draw_read(void)
{
    char tmp[8];
    bool sent = MSG_GetReadSource() == MSG_SCREEN_SENT;
    title(sent ? "SENT" : "READ");
    count_str((uint8_t)(MSG_GetReadIndex() + 1u), sent ? MSG_GetSentCount() : MSG_GetInboxCount(), tmp);
    right7(tmp, 0u);
    {
        char agebuf[5];
        char meta[24];
        age(MSG_GetReadAge(), agebuf);
        meta[0] = 0;
        if (sent) {
            append(meta, "TO:ALL ", sizeof(meta));
        } else {
            append(meta, "FROM:", sizeof(meta));
            append(meta, MSG_GetReadFrom(), sizeof(meta));
            append(meta, " ", sizeof(meta));
        }
        append(meta, agebuf, sizeof(meta));
        small3(meta, 0u, 9u, false);
    }
    if (sent) {
        char st[2];
        MSG_Status_t s = MSG_GetReadStatus();
        st[0] = (s == MSG_STATUS_NONE) ? 0 : ((s == MSG_STATUS_ACKED) ? '+' : ((s == MSG_STATUS_FAILED) ? 'x' : '?'));
        st[1] = 0;
        if (st[0]) UI_PrintStringSmallBold(st, 120, 0, 1);
    }
    draw_dots(17u);
    wrap_y(MSG_GetReadText(), 20u, 3u);
    draw_dots(46u);
    small3(sent ? "RESEND" : "REPLY", 0u, 49u, false);
    small3("F:DEL", 104u, 49u, false);
}

static void draw_compose(void)
{
    char c[8];
    title("COMPOSE");
    small3("NEW MESSAGE", 0u, 10u, false);
    count_str(MSG_GetComposeLen(), MSG_TEXT_LEN, c);
    right3(c, 10u);
    draw_dots(17u);
    wrap_y(MSG_GetComposeText(), 20u, 3u);
    draw_dots(46u);
    small3("SEND", 0u, 49u, false);
    small3((MSG_GetT9Mode() == 2u) ? "2" : ((MSG_GetT9Mode() == 1u) ? "B" : "b"), 120u, 49u, false);
}

static const char *pkt_type_name(uint8_t t)
{
    switch (t) {
        case 1u: return "MSG";
        case 2u: return "ACK";
        case 3u: return "PNG";
        case 4u: return "PON";
        default: return "---";
    }
}

static uint8_t rssi_bars(int16_t dbm)
{
    if (dbm > -75) return 5u;
    if (dbm > -88) return 4u;
    if (dbm > -100) return 3u;
    if (dbm > -112) return 2u;
    if (dbm > -124) return 1u;
    return 0u;
}


static void right_text_y(const char *s, uint8_t y)
{
    uint8_t len = (uint8_t)strlen(s);
    uint8_t x = (len * 7u >= 128u) ? 0u : (uint8_t)(127u - len * 7u);
    text7(s, x, y, false);
}

static void draw_rssi_bar(uint8_t x, uint8_t y, uint8_t bars)
{
    if (bars > 5u) bars = 5u;
    for (uint8_t i = 0u; i < 5u; i++) {
        uint8_t h = (uint8_t)(2u + i);
        uint8_t bx = (uint8_t)(x + i * 4u);
        uint8_t by0 = (uint8_t)(y + 6u - h);
        if (i < bars) fill(bx, by0, (uint8_t)(bx + 2u), (uint8_t)(y + 6u), true);
        else hline(bx, (uint8_t)(bx + 2u), (uint8_t)(y + 6u), true);
    }
}

static void draw_range(void)
{
    char tmp[8];
    title("HEARD");
    uint8_t count = MSG_GetHeardCount();
    uint8_t pages = (uint8_t)((count + 2u) / 3u);
    if (pages == 0u) pages = 1u;
    uint8_t page = MSG_GetRangeScroll();
    if (page >= pages) page = (uint8_t)(pages - 1u);
    count_str((uint8_t)(page + 1u), pages, tmp);
    right7(tmp, 0u);
    draw_dots(9u); draw_dots(46u);
    if (count == 0u) {
        text7("NO HEARD", 36u, 24u, false);
    } else {
        for (uint8_t row = 0u; row < 3u; row++) {
            uint8_t idx = (uint8_t)(page * 3u + row);
            if (idx >= count) break;
            char a[6], call6[7];
            const char *call = MSG_GetHeardCallsign(idx);
            uint8_t y = (uint8_t)(15u + row * 10u);
            memset(call6, 0, sizeof(call6));
            for (uint8_t i = 0u; i < 6u && call[i]; i++) call6[i] = call[i];
            age(MSG_GetHeardAge(idx), a);
            text7(call6, 0u, y, false);
            draw_rssi_bar(45u, y, rssi_bars(MSG_GetHeardRssi(idx)));
            text7(pkt_type_name(MSG_GetHeardType(idx)), 72u, y, false);
            right_text_y(a, y);
        }
    }
    small3("PING", 0u, 49u, false);
    small3("EXIT", 112u, 49u, false);
}

static void draw_callsign(void)
{
    title("MSG CSG");
    UI_PrintStringSmallNormal("CALLSIGN:", 0, 0, 1);
    UI_PrintStringSmallBold(MSG_GetCallsignEdit(), 0, 0, 3);
    small3((MSG_GetCallsignMode() == 2u) ? "2" : ((MSG_GetCallsignMode() == 1u) ? "B" : "b"), 120u, 49u, false);
}

static void draw_settings(void)
{
    static const char *names[] = { "MSG RX:ON", "MSG CSG", "CALLTX:ON", "ACK:ON", "HOP:OFF", "BEEP:ON", "LED:1", "BACK" };
    title("MSG SET");
    uint8_t cur = MSG_GetSettingsCursor();
    uint8_t start = (cur >= 5u) ? (uint8_t)(cur - 4u) : 0u;
    for (uint8_t row = 0u; row < 5u; row++) {
        uint8_t idx = (uint8_t)(start + row);
        if (idx >= 9u) break;
        row7(names[idx], (uint8_t)(row + 1u), idx == cur);
    }
    char tmp[8]; count_str((uint8_t)(cur + 1u), 9u, tmp); right7(tmp, 6u);
}

void UI_DisplayMessenger(void)
{
    switch (MSG_GetScreen()) {
        case MSG_SCREEN_HOME: draw_home(); break;
        case MSG_SCREEN_INBOX:
        case MSG_SCREEN_SENT:
        case MSG_SCREEN_DRAFTS: draw_list(); break;
        case MSG_SCREEN_READ: draw_read(); break;
        case MSG_SCREEN_COMPOSE: draw_compose(); break;
        case MSG_SCREEN_RANGE: draw_range(); break;
        case MSG_SCREEN_CALLSIGN: draw_callsign(); break;
        case MSG_SCREEN_SETTINGS: draw_settings(); break;
        default: draw_home(); break;
    }
    ST7565_BlitFullScreen();
}
