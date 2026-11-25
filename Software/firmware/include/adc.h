#ifndef ADC_H
#define ADC_H
#include <stdint.h>
#include "em_gpio.h"

#define ADC_OK        0
#define ADC_TIMEOUT  (-1)

void adc_init(void);
int  adc_read_mV(GPIO_Port_TypeDef port, uint8_t pin, /*out*/ uint32_t *mv);

#endif
