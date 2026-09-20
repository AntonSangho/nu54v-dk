#ifndef HW_H_
#define HW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#include "led.h"
#include "uart.h"
#include "cli.h"
#include "log.h"
#include "reset.h"
#include "power.h"
#include "i2c.h"
#include "shtc3.h"
#include "adc.h"
#include "temp.h"
#include "button.h"


bool hwInit(void);


#ifdef __cplusplus
}
#endif

#endif
