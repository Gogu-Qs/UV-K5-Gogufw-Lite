# RF14 Packet + Physical FSK Parity Notes

Base: RF13 source tree.

Main changes:

1. Packet layout parity with UV-K1 GOGUFW 0.6.5
   - Native packet remains fixed 94 bytes: GGM2, version 2.
   - Wire frame remains 100 bytes / 50 words: ABCD + 94-byte packet + outer CRC + DCBA.
   - RF callsign fields are fixed 8-byte fields at offsets 11 and 19, matching UV-K1 0.6.5.
   - UV-K5 UI callsign entry remains limited to 6 chars, but RF field width stays 8 bytes for compatibility.
   - ACK packet now mirrors the ACKed MsgID in payload[0..1], matching UV-K1 behavior.
   - ACK/PONG repeated frames use the same MsgID instead of generating new IDs per repeat.
   - Text retry uses the same original MsgID.

2. RX physical modem parity with UV-K1 GOGUFW
   - RF13 RX used NUNU FSK700/sync settings while TX used GOGUFW/Aircopy 1200 settings.
   - RF14 changes RX setup to the same GOGUFW/Aircopy-style physical FSK setup used by UV-K1:
     - REG_70 = 0x00E0
     - REG_72 = 0x3065
     - REG_58 = 0x00C1
     - REG_5C = 0x5665
     - REG_5D = 0x6300
     - REG_5E = 0x3204 during RX
     - REG_59 RX clear/enable = 0x4068 / 0x3068

3. Not changed
   - UI/menu from previous stage retained.
   - TX path remains the RF10/RF13 path.
   - NUNU-style StorePacket FIFO event flow is retained, but modem settings are now GOGUFW-compatible.

Host syntax check passed with gcc -fsyntax-only. Full ARM/Docker build was not available in this environment.
