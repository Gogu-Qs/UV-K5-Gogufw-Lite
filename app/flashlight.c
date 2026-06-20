#ifdef ENABLE_FLASHLIGHT

#include "driver/gpio.h"
#include "bsp/dp32g030/gpio.h"

#include "flashlight.h"

enum FlashlightMode_t gFlashLightState;

void FlashlightTimeSlice(void)
{
    // GOGUFW Lite: simple flashlight only. No blink/SOS modes.
}

void ACTION_FlashLight(void)
{
    if (gFlashLightState == FLASHLIGHT_OFF) {
        gFlashLightState = FLASHLIGHT_ON;
        GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_FLASHLIGHT);
    } else {
        gFlashLightState = FLASHLIGHT_OFF;
        GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_FLASHLIGHT);
    }
}

#endif
