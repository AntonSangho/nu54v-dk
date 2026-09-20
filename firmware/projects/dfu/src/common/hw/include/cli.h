#ifndef CLI_H_
#define CLI_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


// _USE_HW_CLI 가 없으면 모듈의 CLI 코드를 모두 뺀다.
// (매크로 안에서 defined() 를 쓰면 -Wexpansion-to-defined 경고가 나므로 나눠서 정의)
#ifdef _USE_HW_CLI
#define CLI_USE(module)       (_USE_CLI_ ## module)
#else
#define CLI_USE(module)       0
#endif

#ifdef _USE_HW_CLI

#define CLI_CMD_LIST_MAX      HW_CLI_CMD_LIST_MAX
#define CLI_CMD_NAME_MAX      HW_CLI_CMD_NAME_MAX

#define CLI_LINE_HIS_MAX      HW_CLI_LINE_HIS_MAX
#define CLI_LINE_BUF_MAX      HW_CLI_LINE_BUF_MAX




typedef struct
{
  uint16_t   argc;
  char     **argv;

  int32_t  (*getData)(uint8_t index);
  float    (*getFloat)(uint8_t index);
  char    *(*getStr)(uint8_t index);
  bool     (*isStr)(uint8_t index, const char *p_str);
} cli_args_t;

/* 수신 바이트를 cli 보다 먼저 보는 필터.
 *
 * true 를 돌려주면 그 바이트는 cli 가 처리하지 않는다.
 * 같은 포트를 나눠 쓰는 쪽이 자기를 등록한다 (uartSetDriver 와 같은 방식).
 */
typedef bool (*cli_rx_filter_t)(uint8_t ch, uint8_t rx_data);


bool cliInit(void);
bool cliOpen(uint8_t ch, uint32_t baud);
bool cliIsBusy(void);
bool cliOpenLog(uint8_t ch, uint32_t baud);
bool cliSetRxFilter(cli_rx_filter_t filter);

/* 지정한 채널을 필터로 비운다 (cli 가 열고 있는 채널과 무관하게).
 * 필터가 가져가지 않은 바이트를 만나면 거기서 멈추고 false 를 돌려준다. */
bool cliFilterPump(uint8_t ch);
bool cliMain(void);
void cliPrintf(const char *fmt, ...);
bool cliAdd(const char *cmd_str, void (*p_func)(cli_args_t *));
bool cliKeepLoop(void);
void cliLoopIdle(void);
void cliPutch(uint8_t data);
uint8_t  cliGetPort(void);
uint32_t cliAvailable(void);
uint8_t  cliRead(void);
uint32_t cliWrite(uint8_t *p_data, uint32_t length);
bool cliRunStr(const char *fmt, ...);
void cliShowCursor(bool visibility);
void cliMoveUp(uint8_t y);
void cliMoveDown(uint8_t y);
void cliBegin(void);

#endif

#ifdef __cplusplus
}
#endif



#endif