# RF13 - RF10 safe base + NUNU 20.5 BK4819 receive layer

Base: RF10 safe timing/no-rxprime branch.

Implemented from NUNU 20.5 RF model:
- `MSG_EnableRX(true/false)` style receive enable/disable.
- `MSG_ConfigureFSK_NUNU(rx)` with NUNU FSK700-style BK4819 setup:
  - REG_70 Tone2 enable/gain 96
  - REG_72 = 7227
  - REG_58 = FSK enable + RX gain 3 + FSK RX/TX mode 0
  - REG_5A/5B NUNU sync bytes
  - REG_5C hardware CRC disabled
  - REG_5E threshold on RX
  - REG_5D kept at GOGUFW 100-byte wire frame length
- `MSG_StorePacket(interrupt_bits)` called globally from `CheckRadioInterrupts()`, not only Messenger UI.
- NUNU-style RX event order:
  - RX_SYNC: reset buffer/status=RECEIVING
  - FIFO_ALMOST_FULL: drain REG_5F words according to REG_5E low bits
  - RX_FINISHED: drain remaining frame, clear FIFO, re-enable RX, then parse
- RADIO_SetupRegisters() now enables Messenger FSK RX and FSK IRQ bits only when MsgRx is ON.
- FUNCTION_TRANSMIT disables Messenger FSK RX before normal voice TX.

Preserved from GOGUFW:
- UI/menu/timing base from RF10.
- GOGUFW 100-byte outer frame: ABCD + 94-byte GGM2 packet + CRC + DCBA.
- GOGUFW parser/ACK/PONG/Inbox handling.

Known risk:
- NUNU physical sync bytes are now written in RX setup. If UV-K1 GOGUFW TX uses a different physical sync pattern, RX sync may still not increment; if that happens, the next adjustment should be only the REG_5A/5B sync pair, not a full RX rewrite.

Validation performed here:
- Host gcc syntax check passed for app/messenger.c, app/app.c, functions.c, radio.c.
- Full ARM build could not be run because arm-none-eabi-gcc is not installed in this environment.
