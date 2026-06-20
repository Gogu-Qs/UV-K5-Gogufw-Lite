#ifndef APP_FLASHLIGHT_H
#define APP_FLASHLIGHT_H

#ifdef ENABLE_FLASHLIGHT

enum FlashlightMode_t {
    FLASHLIGHT_OFF = 0,
    FLASHLIGHT_ON
};

extern enum FlashlightMode_t gFlashLightState;

void FlashlightTimeSlice(void);
void ACTION_FlashLight(void);

#endif

#endif
