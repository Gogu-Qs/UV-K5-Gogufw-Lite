# RF10 safety fallback

Base: RF7b range-state/test-inbox hotfix.

Goal:
- Do not use RF8/RF9 RX-prime changes that killed normal voice RX.
- Keep RF7b TX path that was confirmed to send message/ping.
- Keep normal voice RX as priority.
- Add only safe UI/state fixes:
  - callsign remains 6 chars via MSG_CALLSIGN_LEN
  - sent status timeout: ? -> retry once -> x
  - retry is delayed, not immediate back-to-back

Intentionally NOT included:
- No BK4819_PrepareFSKReceive integration.
- No continuous FSK RX prime.
- No RF8 register override path.

Expected test:
1. Normal voice RX works after boot.
2. Normal PTT voice TX works.
3. Messenger TX/ping still produce FSK at receiver.
4. Sent item changes ? to x after retry timeout if no ACK.
5. Range WAIT exits after timeout.
