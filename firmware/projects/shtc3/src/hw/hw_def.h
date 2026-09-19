#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION      "V260920R1"
#define _DEF_BOARD_NAME             "NU54-DK-SHTC3"



#define _USE_HW_RTOS


#define _USE_HW_LED
#define      HW_LED_MAX_CH          4

#define _USE_HW_UART
#define      HW_UART_CH_LOG         _DEF_UART1        // uart20 (VCOM1)
#define      HW_UART_CH_CLI         HW_UART_CH_LOG
#define      HW_UART_MAX_CH         1

#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    32
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    8
#define      HW_CLI_LINE_BUF_MAX    64

#define _USE_HW_I2C
#define      HW_I2C_MAX_CH          1                 // _DEF_I2C1 : i2c21 (Qwiic J5, PMIC)

#define _USE_HW_SHTC3
#define      HW_SHTC3_I2C_CH        _DEF_I2C1
#define      HW_SHTC3_I2C_ADDR      0x70



//-- CLI
//
#define _USE_CLI_HW_UART            1
#define _USE_CLI_HW_I2C             1
#define _USE_CLI_HW_SHTC3           1


#endif
