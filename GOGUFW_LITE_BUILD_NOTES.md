# GOGUFW UV-K5 Lite Base - Memory Test

This package is only a **base memory-test configuration** for Armel's UV-K5 firmware.
It does **not** include GOGUFW Messenger or Range Check yet.

## Disabled for space

- AirCopy
- FM Broadcast Radio
- NOAA
- Voice prompts
- VOX
- Alarm
- DTMF Calling
- Spectrum analyzer
- TX1750
- Power-on password
- Boot beeps
- Charge level extra display
- Debug register/UART tools

Notes:
- `game` and `screenshot` features were not found as active Makefile feature flags in this source tree.
- Basic `app/dtmf.o` is still compiled by the upstream Makefile because parts of the firmware may still depend on tone/audio helper code. The user-facing DTMF calling feature is disabled via `ENABLE_DTMF_CALLING=0`.

## Build

Use either the default Makefile, whose defaults have been changed to Lite values:

```sh
make clean
make
```

or the explicit profile:

```sh
make -f Makefile.gogufw-lite clean
make -f Makefile.gogufw-lite
```

## Important

Before flashing any UV-K5 port work:

1. Confirm the firmware builds successfully.
2. Check `arm-none-eabi-size firmware` output.
3. Confirm FLASH stays below the 60K linker limit.
4. Backup EEPROM with CHIRP.
5. Test on one radio only.

## This environment

The ChatGPT sandbox used to prepare this zip does not include `arm-none-eabi-gcc` or Docker,
so the firmware could not be built here. Build size must be measured locally.


Correction: FM Radio is intentionally kept ENABLED for the Lite base.
