#include "adc.h"
#include "em_device.h"
#include "em_cmu.h"
#include "em_iadc.h"

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

    // Set RSRCO as clock source from CMU
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

int adc_read_mV(GPIO_Port_TypeDef port, uint8_t pin, uint32_t *mv) {
    (void)port;
    (void)pin;
    *mv = 0;
    return ADC_OK;
}
