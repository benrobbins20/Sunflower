#include "photo.h"
#include "adc.h"
#include "board_pins.h"

void photo_init(void) {
    
}

int photo_read_mV(photo_t sensor, uint32_t *mv) {
    (void)sensor;
    *mv = 0;
    return ADC_OK;
}
