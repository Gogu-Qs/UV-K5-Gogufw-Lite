# GOGUFW Lite 1.0.2 for Quansheng UV-K5

GOGUFW Lite brings the GOGUFW Messenger experience to the Quansheng UV-K5. It is designed primarily for reliable text messaging, ACK/retry handling, Range Check and compatibility with GOGUFW UV-K1 radios.

## Main features

- GOGUFW text Messenger with Inbox, Sent and Drafts
- ACK confirmation and automatic retry
- HEARD list
- Range Check with multiple PONG results, RSSI and battery voltage
- Collision-resistant randomized ACK delays and slotted Range Check PONG replies for multi-radio use
- Cross-device messaging with GOGUFW UV-K1
- FM radio
- Flashlight

## Downloads

Download the firmware and CHIRP module from the [latest GOGUFW Lite release](https://github.com/Gogu-Qs/UV-K5-Gogufw-Lite/releases/latest).

- `GOGUFW_LITE_UV-K5_1.0.2.packed.bin`: for flashers that expect the packed Quansheng firmware format.
- `GOGUFW_LITE_UV-K5_1.0.2.bin`: raw firmware image for tools that explicitly require a raw binary.
- `UV-K5_GOGUFW_Lite_1.0.2_chirp_module.py`: custom CHIRP module.

Always back up your radio and calibration data before flashing custom firmware.

## CHIRP

The custom module appears under:

- **Vendor:** GOGUFW
- **Model:** UV-K5
- **Variant:** Lite 1.0.2

Follow the [CHIRP installation and safety instructions](CHIRP.md). The module intentionally uploads only the normal channel/settings area and does not overwrite the GOGUFW Drafts storage or calibration area.

## Compatibility

Version 1.0.2 keeps the established GOGUFW packet format and is intended to communicate with the matching GOGUFW UV-K1 Messenger implementation. The wake preamble is invisible to the Inbox and does not change normal message, ACK, ping or pong packet formats.

## Lite build

Flash and RAM are limited on the UV-K5, so this edition focuses on Messenger reliability instead of feature count. Spectrum Analyzer, games, screenshot utility, NOAA Weather, Aircopy, voice features, alarm and VOX are not included. FM radio and the flashlight remain available.

## Disclaimer

This is experimental third-party firmware and is provided without warranty. Flashing custom firmware is at your own risk.
