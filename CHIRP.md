# GOGUFW Lite CHIRP module

Use `UV-K5_GOGUFW_Lite_1.0.2_chirp_module.py` with a current CHIRP daily build.

1. In CHIRP, enable **Developer mode**.
2. Choose **File > Load Module** and select the module.
3. Connect the normal UV-K5 two-pin programming cable while the radio is on.
4. Select **GOGUFW / UV-K5 / Lite 1.0.2** and download from the
   radio before making changes.
5. Save the downloaded image as a backup, then edit and upload normally.

The radio does not use USB directly. The programming cable contains the USB
adapter; the firmware communicates with it through the UV-K5 UART pins.

The module is based on CHIRP's current UV-K5 Egzumer memory layout, but its
upload is intentionally limited to `0x0000-0x1BFF`. It never writes the
Messenger draft area at `0x1C00-0x1CC7` or the calibration/configuration area
above it.
