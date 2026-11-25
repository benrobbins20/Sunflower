#include "adc.h"
#include "bgm22_iadc.h"
#include "em_device.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "em_iadc.h"
#include <stdbool.h>

#define ADC_SRC_CLK_HZ      20000000u // 20MHz CLK_SRC_ADC using FSRCO (can sleep EM2 for adc reads)
#define ADC_CLK_HZ          10000000u // 10MHz max clock frequency with SCHED0 prescaler
#define ADC_VREF_MV         3300u // 3.3V rail
#define ADC_MAX_COUNT       4095u // 12-bit ADC
#define ADC_TIMEOUT_LOOPS   100000u // conversion timeout loops


void adc_init(void) {

    // init the clocks for IADC and GPIO
    CMU_ClockEnable(cmuClock_IADC0, true);
    CMU_ClockEnable(cmuClock_GPIO, true);

    // start from known state
    IADC_reset(IADC0);

    // Set FSRCO as clock source from CMU
    CMU_ClockSelectSet(cmuClock_IADCCLK, cmuSelect_FSRCO);

    // init settings
    IADC_Init_t init = IADC_INIT_DEFAULT;
    IADC_AllConfigs_t all_configs = IADC_ALLCONFIGS_DEFAULT;

    // keep conversion components warm, eg: AFE, ref voltage, amps and buffers: no warm-up delay per read (switch to normal/suspend later for power)
    init.warmup = iadcWarmupKeepWarm;

    // 1st prescaler (HSCLKRATE)
    init.srcClkPrescale = IADC_calcSrcClkPrescale(
        IADC0, 
        ADC_SRC_CLK_HZ, 
        0 // 0 uses CMU's clock eg: CMU_ClockFreqGet(IADC_CMU_CLOCK(iadc));
    ); 

    // config 0: VDD reference, full scale = 3.3 V, gain 1x, OSR x2
    all_configs.configs[0].reference = iadcCfgReferenceVddx;
    all_configs.configs[0].vRef = ADC_VREF_MV;
    all_configs.configs[0].analogGain = iadcCfgAnalogGain1x;
    all_configs.configs[0].osrHighSpeed = iadcCfgOsrHighSpeed2x;

    // 2nd prescaler (SCHED0)
    all_configs.configs[0].adcClkPrescale = IADC_calcAdcClkPrescale(
        IADC0, 
        ADC_CLK_HZ,
        0, 
        iadcCfgModeNormal,
        init.srcClkPrescale
    );

    // single-conversion mode: one result per start command
    IADC_InitSingle_t single = IADC_INITSINGLE_DEFAULT;

    // input parked on GND until adc_read_mV() picks a pin; negative = GND -> single-ended, unsigned
    IADC_SingleInput_t input = IADC_SINGLEINPUT_DEFAULT;
    input.posInput = iadcPosInputGnd;
    input.negInput = iadcNegInputGnd;

    // write all settings to the IADC registers and enable it
    IADC_init(IADC0, &init, &all_configs);
    IADC_initSingle(IADC0, &single, &input);
}

static void adc_alloc_bus(GPIO_Port_TypeDef port, uint8_t pin) {
    
    // parity check: 0 in bit 0 means even and div/2
    // start with even pin numbers (style choice, first pin 0 is even)
    bool even = (pin & 1u) == 0u;

    // portA, even and odd analog bus allocation
    if (port == gpioPortA) {
        GPIO->ABUSALLOC |= even ? GPIO_ABUSALLOC_AEVEN0_ADC0 : GPIO_ABUSALLOC_AODD0_ADC0;
    }

    // portB
    else if (port == gpioPortB) {
        GPIO->BBUSALLOC |= even ? GPIO_BBUSALLOC_BEVEN0_ADC0 : GPIO_BBUSALLOC_BODD0_ADC0;
    }

    // ports CD share a bus
    else {
        GPIO->CDBUSALLOC |= even ? GPIO_CDBUSALLOC_CDEVEN0_ADC0 : GPIO_CDBUSALLOC_CDODD0_ADC0;
    }
}

int adc_read_mV(GPIO_Port_TypeDef port, uint8_t pin, /*out*/ uint32_t *mv) {

    // connect abus to ADC0
    adc_alloc_bus(port, pin);

    // single ended input (non-differential signal), route pin to positive input, negative = GND
    IADC_SingleInput_t input = IADC_SINGLEINPUT_DEFAULT;
    input.posInput = IADC_portPinToPosInput(port, pin);
    input.negInput = iadcNegInputGnd;
    IADC_updateSingleInput(IADC0, &input);

    // start conversion
    IADC_command(IADC0, iadcCmdStartSingle);

    // while the data valid bit for IADC is false (0), keep looping for ADC_TIMEOUT_LOOPS,
    // return error code if conversion fails to populate in FIFO
    uint32_t loops = ADC_TIMEOUT_LOOPS;
    while ((IADC0->STATUS & IADC_STATUS_SINGLEFIFODV) == 0u) {
        if (--loops == 0u) {
            return ADC_TIMEOUT;
        }
    }

    // pop raw counts from FIFO queue
    uint32_t counts = IADC_pullSingleFifoResult(IADC0).data;

    /* convert raw counts to mV 
    (counts / MAX) × 3300 == (counts * 3300) / MAX
    for integer division a/b, (a+b/2)/b provides rounding instead of truncation
    eg. 2000 * 3300 = 6_600_000
    add half of a divisor (also int div), 6_600_000 + 2047 = 6_602_047
    6_602_047 / 4095 = 1612 mV
    TRUNCATE: 6_600_000 / 4095 = 1611.7 -> 1611mV, ROUND: 6_602_047 / 4095 = 1612.2 -> 1612mV */
    uint32_t scaled = counts * ADC_VREF_MV;
    uint32_t rounded = scaled + (ADC_MAX_COUNT / 2u);
    *mv = rounded / ADC_MAX_COUNT;

    return ADC_OK;
}
