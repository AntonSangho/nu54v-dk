#include "i2c.h"





#ifdef _USE_HW_I2C
#include "cli.h"
#include <zephyr/drivers/i2c.h>


#ifdef _USE_HW_RTOS
#define lock()      k_mutex_lock(&mutex_lock, K_FOREVER);
#define unLock()    k_mutex_unlock(&mutex_lock);
#else
#define lock()
#define unLock()
#endif



#if CLI_USE(HW_I2C)
static void cliI2C(cli_args_t *args);
#endif

#ifdef _USE_HW_RTOS
static K_MUTEX_DEFINE(mutex_lock);
#endif

static uint32_t i2c_timeout[I2C_MAX_CH];
static uint32_t i2c_errcount[I2C_MAX_CH];
static uint32_t i2c_freq[I2C_MAX_CH];

static bool is_init = false;
static bool is_begin[I2C_MAX_CH];


typedef struct
{
  const struct device *h_i2c;

} i2c_tbl_t;

// 버스 전원(저전력)은 DTS 의 zephyr,pm-device-runtime-auto 로 드라이버가 전송마다 자동 관리한다.
//
static i2c_tbl_t i2c_tbl[I2C_MAX_CH] =
{
  { DEVICE_DT_GET(DT_NODELABEL(i2c21)) },     // _DEF_I2C1 : P1.02 SDA, P1.03 SCL
};


static bool i2cResult(uint8_t ch, int i2c_ret);




bool i2cInit(void)
{
  uint32_t i;


  for (i=0; i<I2C_MAX_CH; i++)
  {
    i2c_timeout[i] = 10;
    i2c_errcount[i] = 0;
    is_begin[i] = false;
  }


  logPrintf("[OK] i2cInit()\n");

#if CLI_USE(HW_I2C)
  cliAdd("i2c", cliI2C);
#endif

  is_init = true;
  return true;
}

bool i2cIsInit(void)
{
  return is_init;
}

bool i2cBegin(uint8_t ch, uint32_t freq_khz)
{
  bool ret = false;
  uint32_t speed;

  if (ch >= I2C_MAX_CH)
  {
    return false;
  }


  switch(ch)
  {
    default:
      i2c_freq[ch] = freq_khz;

      if (freq_khz <= 100)
        speed = I2C_SPEED_STANDARD;
      else if (freq_khz <= 400)
        speed = I2C_SPEED_FAST;
      else
        speed = I2C_SPEED_FAST_PLUS;

      if (device_is_ready(i2c_tbl[ch].h_i2c))
      {
        if (i2c_configure(i2c_tbl[ch].h_i2c, I2C_MODE_CONTROLLER | I2C_SPEED_SET(speed)) == 0)
        {
          ret = true;
        }
      }
      is_begin[ch] = ret;
      break;
  }

  return ret;
}

bool i2cIsBegin(uint8_t ch)
{
  return is_begin[ch];
}

void i2cReset(uint8_t ch)
{
}

bool i2cIsDeviceReady(uint8_t ch, uint8_t dev_addr)
{
  bool ret = false;
  uint8_t data;

  if (ch >= I2C_MAX_CH)
  {
    return false;
  }

  // 1바이트 읽기로 ACK 확인 (0 바이트 쓰기는 TWIM 이 지원하지 않는다)
  //
  lock();
  if (i2c_read(i2c_tbl[ch].h_i2c, &data, 1, dev_addr) == 0)
  {
    ret = true;
  }
  unLock();

  return ret;
}

bool i2cRecovery(uint8_t ch)
{
  bool ret;

  if (ch >= I2C_MAX_CH)
  {
    return false;
  }

  lock();
  i2c_recover_bus(i2c_tbl[ch].h_i2c);
  unLock();

  ret = i2cBegin(ch, i2c_freq[ch]);

  return ret;
}

bool i2cReadByte (uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t timeout)
{
  return i2cReadBytes(ch, dev_addr, reg_addr, p_data, 1, timeout);
}

bool i2cReadBytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  int i2c_ret;
  uint8_t reg = (uint8_t)reg_addr;

  if (ch >= I2C_MAX_CH)
  {
    return false;
  }

  lock();
  i2c_ret = i2c_write_read(i2c_tbl[ch].h_i2c, dev_addr, &reg, 1, p_data, length);
  unLock();

  return i2cResult(ch, i2c_ret);
}

bool i2cReadA16Bytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  int i2c_ret;
  uint8_t reg[2];

  if (ch >= I2C_MAX_CH)
  {
    return false;
  }

  reg[0] = (uint8_t)(reg_addr >> 8);
  reg[1] = (uint8_t)(reg_addr >> 0);

  lock();
  i2c_ret = i2c_write_read(i2c_tbl[ch].h_i2c, dev_addr, reg, 2, p_data, length);
  unLock();

  return i2cResult(ch, i2c_ret);
}

bool i2cReadData(uint8_t ch, uint16_t dev_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  int i2c_ret;

  if (ch >= I2C_MAX_CH)
  {
    return false;
  }

  lock();
  i2c_ret = i2c_read(i2c_tbl[ch].h_i2c, p_data, length, dev_addr);
  unLock();

  return i2cResult(ch, i2c_ret);
}

bool i2cWriteByte (uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t data, uint32_t timeout)
{
  return i2cWriteBytes(ch, dev_addr, reg_addr, &data, 1, timeout);
}

bool i2cWriteBytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  int i2c_ret;
  uint8_t buf[length + 1];

  if (ch >= I2C_MAX_CH)
  {
    return false;
  }

  lock();
  buf[0] = reg_addr;
  memcpy(&buf[1], p_data, length);
  i2c_ret = i2c_write(i2c_tbl[ch].h_i2c, buf, length + 1, dev_addr);
  unLock();

  return i2cResult(ch, i2c_ret);
}

bool i2cWriteA16Bytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  int i2c_ret;
  uint8_t buf[length + 2];

  if (ch >= I2C_MAX_CH)
  {
    return false;
  }

  lock();
  buf[0] = (uint8_t)(reg_addr >> 8);
  buf[1] = (uint8_t)(reg_addr >> 0);
  memcpy(&buf[2], p_data, length);
  i2c_ret = i2c_write(i2c_tbl[ch].h_i2c, buf, length + 2, dev_addr);
  unLock();

  return i2cResult(ch, i2c_ret);
}

bool i2cWriteData(uint8_t ch, uint16_t dev_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  int i2c_ret;

  if (ch >= I2C_MAX_CH)
  {
    return false;
  }

  lock();
  i2c_ret = i2c_write(i2c_tbl[ch].h_i2c, p_data, length, dev_addr);
  unLock();

  return i2cResult(ch, i2c_ret);
}

void i2cSetTimeout(uint8_t ch, uint32_t timeout)
{
  i2c_timeout[ch] = timeout;
}

uint32_t i2cGetTimeout(uint8_t ch)
{
  return i2c_timeout[ch];
}

void i2cClearErrCount(uint8_t ch)
{
  i2c_errcount[ch] = 0;
}

uint32_t i2cGetErrCount(uint8_t ch)
{
  return i2c_errcount[ch];
}

bool i2cResult(uint8_t ch, int i2c_ret)
{
  if (i2c_ret != 0)
  {
    i2c_errcount[ch]++;
    return false;
  }
  return true;
}



#if CLI_USE(HW_I2C)
void cliI2C(cli_args_t *args)
{
  bool ret = false;
  bool i2c_ret;

  uint8_t print_ch;
  uint8_t ch;
  uint16_t dev_addr;
  uint16_t reg_addr;
  uint16_t length;
  uint32_t pre_time;


  if (args->argc == 2 && args->isStr(0, "scan") == true)
  {
    uint32_t dev_cnt = 0;
    print_ch = (uint16_t) args->getData(1);

    print_ch = constrain(print_ch, 1, I2C_MAX_CH);
    print_ch -= 1;

    for (int i=0x04; i<= 0x7F; i++)
    {
      if (i2cIsDeviceReady(print_ch, i) == true)
      {
        cliPrintf("I2C CH%d Addr 0x%02X : OK\n", print_ch+1, i);
        dev_cnt++;
      }
    }
    if (dev_cnt == 0)
    {
      cliPrintf("no found\n");
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "begin") == true)
  {
    print_ch = (uint16_t) args->getData(1);

    print_ch = constrain(print_ch, 1, I2C_MAX_CH);
    print_ch -= 1;

    i2c_ret = i2cBegin(print_ch, 400);
    if (i2c_ret == true)
    {
      cliPrintf("I2C CH%d Begin OK\n", print_ch + 1);
    }
    else
    {
      cliPrintf("I2C CH%d Begin Fail\n", print_ch + 1);
    }
    ret = true;
  }

  if (args->argc == 5 && args->isStr(0, "read") == true)
  {
    print_ch = (uint16_t) args->getData(1);
    print_ch = constrain(print_ch, 1, I2C_MAX_CH);

    dev_addr = (uint16_t) args->getData(2);
    reg_addr = (uint16_t) args->getData(3);
    length   = (uint16_t) args->getData(4);
    ch       = print_ch - 1;

    for (int i=0; i<length; i++)
    {
      uint8_t i2c_data;
      i2c_ret = i2cReadByte(ch, dev_addr, reg_addr+i, &i2c_data, 100);

      if (i2c_ret == true)
      {
        cliPrintf("%d I2C - 0x%02X : 0x%02X\n", print_ch, reg_addr+i, i2c_data);
      }
      else
      {
        cliPrintf("%d I2C - Fail \n", print_ch);
        break;
      }
    }
    ret = true;
  }

  if (args->argc == 5 && args->isStr(0, "write") == true)
  {
    print_ch = (uint16_t) args->getData(1);
    print_ch = constrain(print_ch, 1, I2C_MAX_CH);

    dev_addr = (uint16_t) args->getData(2);
    reg_addr = (uint16_t) args->getData(3);
    length   = (uint16_t) args->getData(4);
    ch       = print_ch - 1;

    pre_time = millis();
    i2c_ret = i2cWriteByte(ch, dev_addr, reg_addr, (uint8_t)length, 100);

    if (i2c_ret == true)
    {
      cliPrintf("%d I2C - 0x%02X : 0x%02X, %d ms\n", print_ch, reg_addr, length, millis()-pre_time);
    }
    else
    {
      cliPrintf("%d I2C - Fail \n", print_ch);
    }
    ret = true;
  }


  if (ret == false)
  {
    cliPrintf( "i2c begin ch[1~%d]\n", I2C_MAX_CH);
    cliPrintf( "i2c scan  ch[1~%d]\n", I2C_MAX_CH);
    cliPrintf( "i2c read  ch dev_addr reg_addr length\n");
    cliPrintf( "i2c write ch dev_addr reg_addr data\n");
  }
}

#endif

#endif
