#ifndef ADC_H_
#define ADC_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_ADC


#define ADC_MAX_CH    HW_ADC_MAX_CH


bool     adcInit(void);
bool     adcIsInit(void);
int32_t  adcRead(uint8_t ch);
int32_t  adcRead8(uint8_t ch);
int32_t  adcRead10(uint8_t ch);
int32_t  adcRead12(uint8_t ch);
int32_t  adcRead16(uint8_t ch);
int32_t  adcReadAverage(uint8_t ch, uint16_t count);
uint8_t  adcGetRes(uint8_t ch);

// 핀에서 읽은 전압 (mV). 분압이 있는 채널은 분압 전 전압으로 환산한다 (VBAT 등).
int32_t  adcReadMilliVolt(uint8_t ch);
int32_t  adcConvMilliVolt(uint8_t ch, int32_t adc_value);

float    adcReadVoltage(uint8_t ch);
float    adcConvVoltage(uint8_t ch, int32_t adc_value);

const char *adcGetName(uint8_t ch);


#endif

#ifdef __cplusplus
}
#endif

#endif
