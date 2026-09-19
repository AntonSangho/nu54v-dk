#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION      "V260920R1"
#define _DEF_BOARD_NAME             "NU54-DK-UART"



#define _USE_HW_RTOS


#define _USE_HW_LED
#define      HW_LED_MAX_CH          4

/* 하드웨어 채널은 VCOM1(uart20), VCOM0(uart30). 이후 가상 채널(BLE 등)은 uart_driver_t 를 등록해 붙인다.
 * 채널 번호는 기능을 켜고 끄는 것과 상관없이 고정한다 (docs/05_uart.md 채널 표). */
#define _USE_HW_UART
#define      HW_UART_CH_LOG         _DEF_UART1        // uart20 (VCOM1)
#define      HW_UART_CH_VCOM0       _DEF_UART2        // uart30 (VCOM0)
#define      HW_UART_CH_CLI         HW_UART_CH_LOG
#define      HW_UART_MAX_CH         2

#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    32
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    8
#define      HW_CLI_LINE_BUF_MAX    64

//-- CLI
//
#define _USE_CLI_HW_UART            1


#endif
