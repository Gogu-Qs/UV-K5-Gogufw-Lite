# RF15B size fix

Build overflow in RF15A was ~960 bytes.

To recover flash without touching Messenger/Range RF logic, this package disables:
- ENABLE_AUDIO_BAR
- ENABLE_COPY_CHAN_TO_VFO

Messenger/Range RSSI bars are custom UI and are not removed by disabling ENABLE_AUDIO_BAR.
