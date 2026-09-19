#include "shtc3.h"


#ifdef _USE_HW_SHTC3
#include "i2c.h"
#include "uart.h"
#include "cli.h"


// Sensirion SHTC3 온습도 센서 (Qwiic J5)
//
// 저전력 : 측정할 때만 Wakeup → 측정 → Sleep 명령으로 다시 재운다.
//          Sleep 상태 소비전류 0.3 uA (typ). 측정은 클럭 스트레칭 없이 요청 후 대기.
//
#define SHTC3_CMD_WAKEUP          0x3517
#define SHTC3_CMD_SLEEP           0xB098
#define SHTC3_CMD_SOFT_RESET      0x805D
#define SHTC3_CMD_READ_ID         0xEFC8
#define SHTC3_CMD_MEAS_NORMAL     0x7866      // T 먼저, 클럭 스트레칭 없음, Normal mode
#define SHTC3_CMD_MEAS_LOW_POWER  0x609C      // T 먼저, 클럭 스트레칭 없음, Low power mode

#define SHTC3_WAKEUP_US           240         // tWAKE max
#define SHTC3_MEAS_NORMAL_MS      13          // 12.1 ms max
#define SHTC3_MEAS_LOW_POWER_MS   1           // 0.8 ms max

#define SHTC3_ID_MASK             0x083F
#define SHTC3_ID_VALUE            0x0807


#if CLI_USE(HW_SHTC3)
static void cliShtc3(cli_args_t *args);
#endif
static bool shtc3WriteCmd(uint16_t cmd);
static bool shtc3ReadWords(uint16_t *p_data, uint8_t count);
static uint8_t shtc3Crc(const uint8_t *p_data, uint8_t length);

static bool is_init = false;
static bool is_low_power = false;
static const uint8_t i2c_ch = HW_SHTC3_I2C_CH;
static const uint8_t i2c_addr = HW_SHTC3_I2C_ADDR;




bool shtc3Init(void)
{
  bool ret = false;
  uint16_t id;


  if (i2cIsBegin(i2c_ch) != true)
  {
    i2cBegin(i2c_ch, 400);
  }

  if (shtc3GetID(&id) == true)
  {
    ret = true;
  }

  is_init = ret;

  logPrintf("[%s] shtc3Init()\n", ret ? "OK" : "E_");
  if (ret)
    logPrintf("     id : 0x%04X\n", id);

#if CLI_USE(HW_SHTC3)
  cliAdd("shtc3", cliShtc3);
#endif

  return ret;
}

bool shtc3IsInit(void)
{
  return is_init;
}

bool shtc3GetID(uint16_t *p_id)
{
  bool ret = false;
  uint16_t id;


  if (shtc3WriteCmd(SHTC3_CMD_WAKEUP) != true)
  {
    return false;
  }
  k_usleep(SHTC3_WAKEUP_US);

  if (shtc3WriteCmd(SHTC3_CMD_READ_ID) == true && shtc3ReadWords(&id, 1) == true)
  {
    if ((id & SHTC3_ID_MASK) == SHTC3_ID_VALUE)
    {
      *p_id = id;
      ret = true;
    }
  }

  shtc3WriteCmd(SHTC3_CMD_SLEEP);

  return ret;
}

// Low power mode : 측정 시간 0.8 ms (정확도/반복성은 약간 낮아짐)
//
bool shtc3SetLowPower(bool enable)
{
  is_low_power = enable;
  return true;
}

bool shtc3Read(shtc3_info_t *p_info)
{
  bool     ret = false;
  uint16_t data[2];
  uint16_t cmd;
  uint32_t wait_ms;


  if (is_init != true)
  {
    return false;
  }

  if (is_low_power)
  {
    cmd     = SHTC3_CMD_MEAS_LOW_POWER;
    wait_ms = SHTC3_MEAS_LOW_POWER_MS;
  }
  else
  {
    cmd     = SHTC3_CMD_MEAS_NORMAL;
    wait_ms = SHTC3_MEAS_NORMAL_MS;
  }

  if (shtc3WriteCmd(SHTC3_CMD_WAKEUP) != true)
  {
    return false;
  }
  k_usleep(SHTC3_WAKEUP_US);

  if (shtc3WriteCmd(cmd) == true)
  {
    // 측정 중에는 CPU 가 sleep 한다.
    delay(wait_ms);

    if (shtc3ReadWords(data, 2) == true)
    {
      // T = -45 + 175 * raw / 65536 [℃],  RH = 100 * raw / 65536 [%]  (0.01 단위 정수 연산)
      p_info->temp = (int16_t)(((17500 * (int32_t)data[0]) >> 16) - 4500);
      p_info->humi = (int16_t)((10000 * (int32_t)data[1]) >> 16);
      ret = true;
    }
  }

  shtc3WriteCmd(SHTC3_CMD_SLEEP);

  return ret;
}

bool shtc3WriteCmd(uint16_t cmd)
{
  uint8_t buf[2];

  buf[0] = (uint8_t)(cmd >> 8);
  buf[1] = (uint8_t)(cmd >> 0);

  return i2cWriteData(i2c_ch, i2c_addr, buf, 2, 10);
}

// 16비트 워드 + CRC8 단위로 읽는다.
//
bool shtc3ReadWords(uint16_t *p_data, uint8_t count)
{
  uint8_t buf[6];

  if (count > 2)
  {
    return false;
  }

  if (i2cReadData(i2c_ch, i2c_addr, buf, count * 3, 10) != true)
  {
    return false;
  }

  for (int i=0; i<count; i++)
  {
    if (shtc3Crc(&buf[i * 3], 2) != buf[i * 3 + 2])
    {
      return false;
    }
    p_data[i] = ((uint16_t)buf[i * 3] << 8) | buf[i * 3 + 1];
  }

  return true;
}

// CRC-8 : poly 0x31, init 0xFF
//
uint8_t shtc3Crc(const uint8_t *p_data, uint8_t length)
{
  uint8_t crc = 0xFF;

  for (int i=0; i<length; i++)
  {
    crc ^= p_data[i];
    for (int bit=0; bit<8; bit++)
    {
      if (crc & 0x80)
        crc = (crc << 1) ^ 0x31;
      else
        crc = (crc << 1);
    }
  }

  return crc;
}




#if CLI_USE(HW_SHTC3)
static void cliShtc3PrintInfo(shtc3_info_t *p_info)
{
  cliPrintf("T : %s%d.%02d C, RH : %d.%02d %%\n",
            p_info->temp < 0 ? "-" : "", abs(p_info->temp) / 100, abs(p_info->temp) % 100,
            p_info->humi / 100, p_info->humi % 100);
}

void cliShtc3(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    uint16_t id;

    cliPrintf("is_init   : %s\n", is_init ? "True" : "False");
    cliPrintf("i2c ch    : %d, addr 0x%02X\n", i2c_ch + 1, i2c_addr);
    cliPrintf("low power : %s\n", is_low_power ? "On" : "Off");
    if (shtc3GetID(&id) == true)
      cliPrintf("id        : 0x%04X\n", id);
    else
      cliPrintf("id        : Fail\n");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "init") == true)
  {
    cliPrintf("shtc3Init() : %s\n", shtc3Init() ? "OK" : "Fail");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "read") == true)
  {
    shtc3_info_t info;
    uint32_t     pre_time;

    pre_time = millis();
    if (shtc3Read(&info) == true)
    {
      cliShtc3PrintInfo(&info);
      cliPrintf("%d ms\n", millis() - pre_time);
    }
    else
    {
      cliPrintf("shtc3Read() Fail\n");
    }
    ret = true;
  }

  // 키 입력이 있을 때까지 period_ms 주기로 측정 (측정 사이에는 sleep)
  //
  if (args->argc == 2 && args->isStr(0, "read") == true)
  {
    shtc3_info_t info;
    uint32_t     period_ms;

    period_ms = constrain(args->getData(1), 10, 60000);

    while (cliKeepLoop())
    {
      if (shtc3Read(&info) == true)
        cliShtc3PrintInfo(&info);
      else
        cliPrintf("shtc3Read() Fail\n");

      uartWaitRx(cliGetPort(), period_ms);
    }
    cliRead();    // 반복을 멈춘 키는 명령줄에 남기지 않는다
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "lowpower") == true)
  {
    shtc3SetLowPower(args->isStr(1, "on"));
    cliPrintf("low power : %s\n", is_low_power ? "On" : "Off");
    ret = true;
  }


  if (ret == false)
  {
    cliPrintf("shtc3 info\n");
    cliPrintf("shtc3 init\n");
    cliPrintf("shtc3 read [period_ms]\n");
    cliPrintf("shtc3 lowpower on:off\n");
  }
}
#endif

#endif
