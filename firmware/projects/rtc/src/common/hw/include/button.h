#ifndef BUTTON_H_
#define BUTTON_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_BUTTON

#define BUTTON_MAX_CH       HW_BUTTON_MAX_CH

#define BUTTON_EVT_PRESSED  (1<<0)
#define BUTTON_EVT_RELEASED (1<<1)
#define BUTTON_EVT_CLICK    (1<<2)
#define BUTTON_EVT_LONG     (1<<3)


bool     buttonInit(void);
bool     buttonGetPressed(uint8_t ch);
uint32_t buttonGetData(void);
uint8_t  buttonGetPressedCount(void);
uint32_t buttonGetPressedTime(uint8_t ch);
const char *buttonGetName(uint8_t ch);

uint32_t buttonGetEvent(uint8_t ch);
void     buttonSetLongTime(uint8_t ch, uint32_t long_ms);
void     buttonSetEventISR(void (*func)(void));

#endif

#ifdef __cplusplus
}
#endif



#endif
