#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"
#include <app_version.h>          // VERSION 파일에서 생성된다


/* 버전은 VERSION 파일 하나에서 나온다.
 * 같은 값이 MCUboot 이미지 버전(CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION)으로도 들어가므로
 * cli 의 `info` 한 줄로 업데이트 성공 여부를 판정할 수 있다 (baram-term 쪽 요청). */
#define _DEF_FIRMWATRE_VERSION      APP_VERSION_TWEAK_STRING
#define _DEF_BOARD_NAME             "NU54-DK-DFU"



#define _HW_DEF_RTOS_THREAD_PRI_CLI           5
#define _HW_DEF_RTOS_THREAD_MEM_CLI           (4*1024)


#define _USE_HW_RTOS


#define _USE_HW_RESET
#define _USE_HW_POWER


#define _USE_HW_LED
#define      HW_LED_MAX_CH          4

/* 하드웨어 채널은 VCOM1(uart20), VCOM0(uart30). 이후 가상 채널(BLE 등)은 uart_driver_t 를 등록해 붙인다.
 * 채널 번호는 기능을 켜고 끄는 것과 상관없이 고정한다 (docs/05_uart.md 채널 표). */
#define _USE_HW_UART
#define      HW_UART_CH_LOG         _DEF_UART1        // uart20 (VCOM1)
#define      HW_UART_CH_CLI         HW_UART_CH_LOG
#define      HW_UART_CH_BLE         _DEF_UART2        // BLE NUS (가상 채널)
#define      HW_UART_MAX_CH         2

#define _USE_HW_LOG
#define      HW_LOG_CH              HW_UART_CH_LOG
#define      HW_LOG_BOOT_BUF_MAX    2048
#define      HW_LOG_LIST_BUF_MAX    4096

#define _USE_HW_I2C
#define      HW_I2C_MAX_CH          1                 // _DEF_I2C1 : i2c21 (Qwiic J5, PMIC)

#define _USE_HW_SHTC3
#define      HW_SHTC3_I2C_CH        _DEF_I2C1
#define      HW_SHTC3_I2C_ADDR      0x70

/* BLE : 스택 / 역할 / 서비스를 따로 켠다. Kconfig 는 firmware/conf/ble*.conf 를
 * 프로젝트 CMakeLists 의 EXTRA_CONF_FILE 로 붙인다 (짝이 안 맞으면 #error 로 잡힌다). */
#define _USE_HW_BLE
#define      HW_BLE_DEVICE_NAME     "NU54V-DK"
#define      HW_BLE_BOARD_ID        0x01              // 광고 제조사 데이터의 보드 종류
#define      HW_BLE_FW_VER_MAJOR    1
#define      HW_BLE_FW_VER_MINOR    0

#define _USE_HW_BLE_PERIPHERAL
#define _USE_HW_BLE_NUS
// #define _USE_HW_BLE_CENTRAL          // 중앙 역할 (자리만, conf/ble_central.conf)
// #define _USE_HW_BLE_NUS_CLIENT       // NUS 클라이언트 (자리만)

/* DFU : MCUboot + SMP. 전송 경로를 따로 켠다 (Kconfig 는 firmware/conf/dfu*.conf) */
#define _USE_HW_DFU
#define _USE_HW_DFU_BLE
#define _USE_HW_DFU_SERIAL

#define _USE_HW_RTC

#define _USE_HW_NVS

#define _USE_HW_PMIC
#define      HW_PMIC_I2C_CH         _DEF_I2C1
#define      HW_PMIC_I2C_ADDR       0x6A
// 배터리 : 리튬 1셀 4.2 V, 300 mAh → 충전 전류 0.5C = 150 mA (최대 1C = 300 mA)
#define      HW_PMIC_ICHG_MA        150
#define      HW_PMIC_ICHG_MAX_MA    300

#define _USE_HW_ADC
#define      HW_ADC_MAX_CH          ADC_PIN_MAX

#define _USE_HW_TEMP

#define _USE_HW_BUTTON
#define      HW_BUTTON_MAX_CH       BUTTON_PIN_MAX

#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    32
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    8
#define      HW_CLI_LINE_BUF_MAX    64

//-- CLI
//
#define _USE_CLI_HW_INFO            1       // info : 펌웨어 이름·버전·빌드 시각
#define _USE_CLI_HW_UART            1
#define _USE_CLI_HW_LOG             1
#define _USE_CLI_HW_RESET           1
#define _USE_CLI_HW_POWER           1
#define _USE_CLI_HW_I2C             1
#define _USE_CLI_HW_SHTC3           1
#define _USE_CLI_HW_BLE             1
#define _USE_CLI_HW_RTC             1
#define _USE_CLI_HW_NVS             1
#define _USE_CLI_HW_PMIC            1
#define _USE_CLI_HW_ADC             1
#define _USE_CLI_HW_TEMP            1
#define _USE_CLI_HW_BUTTON          1


// 보드 스위치 SW1~SW4 (DTS button0~button3)
//
// ADC 채널 (보드 DTS zephyr,user io-channels)
//
typedef enum
{
  ADC_VBAT,           // P1.12 (AIN5) : 배터리 전압 분압 470K/1M
  ADC_PIN_MAX,
} AdcPinName_t;

typedef enum
{
  BTN1,
  BTN2,
  BTN3,
  BTN4,
  BUTTON_PIN_MAX,
} ButtonPinName_t;


#endif
