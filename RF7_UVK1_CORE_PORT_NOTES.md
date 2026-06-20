# RF7 UV-K1 core port notes

Goal: stop re-inventing the RF layer and move the UV-K5 port closer to the working UV-K1 GOGUFW 0.6.5 RF behavior.

Key RF7 changes:
- Restored UV-K1 0.6.5 GOGUFW outer FSK frame layout:
  - word[0] = 0xABCD
  - word[1..47] = 94-byte native GGM2 packet
  - word[48] = outer CRC16 over the 94-byte packet
  - word[49] = 0xDCBA
- Restored UV-K1 long-preamble REG_59 TX values:
  - clear = 0x80F8
  - ready = 0x00F8
  - start = 0x28F8
- Kept UV-K1-style TX repeats:
  - message TX repeats twice
  - Range PING repeats three times
- Added age timer refresh for Sent/Inbox/Read/Range screens.
- Added status-bar unread envelope flag and Messenger HOME key 1 notification test.
- Added global PTT voice restore hook through GENERIC_Key_PTT.
- Kept UI/menu/callsign behavior from the latest working UI parity package.

Limitations:
- RX decode is still not the focus of this package; this is a TX/frame/voice/notify recovery step.
- Full ACK matching/retry scheduler is still simplified compared with UV-K1 0.6.5 and should be the next port target after confirming TX audio/frame compatibility.
