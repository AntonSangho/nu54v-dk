#ifndef BSP_H_
#define BSP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "def.h"

#include <hal/nrf_gpio.h>
#include <zephyr/kernel.h>


#ifndef __WEAK
#define __WEAK              __weak
#endif

#define assert              assert_param
#define assert_param(expr)  ((void)0U)

void logPrintf(const char *fmt, ...);


bool bspInit(void);

void delay(uint32_t time_ms);
uint32_t millis(void);


#ifdef __cplusplus
}
#endif

#endif
