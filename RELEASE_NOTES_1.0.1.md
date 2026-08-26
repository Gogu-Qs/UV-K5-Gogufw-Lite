## GOGUFW Lite 1.0.1

This release focuses on Messenger reliability, UV-K1 compatibility and safe CHIRP support.

### Messenger and Range Check

- Improved first-message reception from power save with an invisible wake preamble before the first text-message attempt.
- Kept ACK and PONG reception on the correct channel while Dual Watch is enabled.
- Added an ACK queue for closely timed replies.
- Kept the complete 12-second Range Check collection window while displaying results as they arrive.
- Fixed Sent occasionally displaying the previously selected quick message even though the correct new message was transmitted.
- Preserved the existing GOGUFW message, ACK, retry, ping and pong packet formats.

### CHIRP

- Added the custom `Quansheng / UV-K5 GOGUFW Lite / 1.0.1` radio definition.
- Added the minimal programming interface required for clone/download/upload/reset operations.
- Limited uploads to `0x0000-0x1BFF`, preserving GOGUFW Drafts storage and calibration data.

### Interface and memory

- Startup now shows `GOGUFW LITE` and version `1.0.1` while retaining both user-configurable startup lines.
- Retained the flashlight with a smaller implementation.
- Removed production-only Messenger debug remnants and kept the Lite feature profile.

### Files

- `GOGUFW_LITE_UV-K5_1.0.1.packed.bin` — packed firmware image.
- `GOGUFW_LITE_UV-K5_1.0.1.bin` — raw firmware image.
- `UV-K5_GOGUFW_Lite_1.0.1_chirp_module.py` — CHIRP module.

Back up the radio and calibration data before flashing. This experimental third-party firmware is provided without warranty.
