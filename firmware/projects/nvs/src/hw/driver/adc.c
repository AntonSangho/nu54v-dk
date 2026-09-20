/*
 * adc.c — SAADC (nrf54l15-bd adc.c 구조, Zephyr)
 *
 * 채널은 보드 DTS 의 zephyr,user io-channels 에서 가져온다.
 * 분압이 있는 채널은 scale(×1000) 로 분압 전 전압으로 환산한다.
 *
 * 저전력 : 읽을 때만 SAADC 가 동작한다 (Zephyr nrfx 드라이버가 변환 뒤 끈다).
 *          VBAT 분압기(470K/1M) 누설은 4.1 V 에서 약 2.8 uA 로 상시 흐른다.
 */

#include "adc.h"


#ifdef _USE_HW_ADC
#include "cli.h"
#include <zephyr/drivers/adc.h>


#define NAME_DEF(x)  x, #x

// 분압기 출력 임피던스(470K ‖ 1M = 320 kΩ)가 커서 한 번 읽으면 수십 mV 흔들린다.
// DTS 의 하드웨어 오버샘플링(16회)에 더해 소프트웨어로도 평균을 낸다.
#define ADC_AVG_COUNT   16

#ifdef _USE_HW_RTOS
#define lock()      k_mutex_lock(&mutex_lock, K_FOREVER);
#define unLock()    k_mutex_unlock(&mutex_lock);
#else
#define lock()
#define unLock()
#endif


typedef struct
{
  struct adc_dt_spec  h_dt;
  AdcPinName_t        pin_name;
  const char         *p_name;
  uint32_t            scale;      // 분압 보정 ×1000 (1000 = 분압 없음)
} adc_tbl_t;


#if CLI_USE(HW_ADC)
static void cliAdc(cli_args_t *args);
#endif

#ifdef _USE_HW_RTOS
static K_MUTEX_DEFINE(mutex_lock);
#endif

static bool    is_init = false;
static int16_t adc_data_buf[ADC_MAX_CH];

static const adc_tbl_t adc_tbl[ADC_MAX_CH] =
{
  {ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), vbat), NAME_DEF(ADC_VBAT), 1470},   // P1.12 AIN5, 470K/1M
};




bool adcInit(void)
{
  bool ret = true;


  for (int i=0; i<ADC_MAX_CH; i++)
  {
    if (!adc_is_ready_dt(&adc_tbl[i].h_dt))
    {
      logPrintf("[E_] adc : %s not ready\n", adc_tbl[i].h_dt.dev->name);
      ret = false;
      break;
    }

    int err = adc_channel_setup_dt(&adc_tbl[i].h_dt);
    if (err < 0)
    {
      logPrintf("[E_] adc : Could not setup channel #%d (%d)\n", i, err);
      ret = false;
      break;
    }
  }

  is_init = ret;

  logPrintf("[%s] adcInit()\n", is_init ? "OK":"E_");

#if CLI_USE(HW_ADC)
  cliAdd("adc", cliAdc);
#endif
  return ret;
}

bool adcIsInit(void)
{
  return is_init;
}

int32_t adcRead(uint8_t ch)
{
  int err;
  int16_t buf = 0;
  struct adc_sequence sequence =
  {
    .buffer      = &buf,
    .buffer_size = sizeof(buf),
  };


  if (ch >= ADC_MAX_CH || is_init != true)
  {
    return 0;
  }

  lock();
  (void)adc_sequence_init_dt(&adc_tbl[ch].h_dt, &sequence);

  err = adc_read(adc_tbl[ch].h_dt.dev, &sequence);
  if (err >= 0)
  {
    adc_data_buf[ch] = buf;
  }
  else
  {
    adc_data_buf[ch] = 0;
  }
  unLock();

  return adc_data_buf[ch];
}

int32_t adcRead8(uint8_t ch)
{
  return adcRead(ch)>>4;
}

int32_t adcRead10(uint8_t ch)
{
  return adcRead(ch)>>2;
}

int32_t adcRead12(uint8_t ch)
{
  return adcRead(ch)>>0;
}

int32_t adcRead16(uint8_t ch)
{
  return adcRead(ch)<<4;
}

uint8_t adcGetRes(uint8_t ch)
{
  if (ch >= ADC_MAX_CH) return 0;

  return adc_tbl[ch].h_dt.resolution;
}

// count 회 읽어 평균한 raw 값
//
int32_t adcReadAverage(uint8_t ch, uint16_t count)
{
  int32_t sum = 0;

  if (count == 0) return 0;

  // SAADC 를 다시 켠 직후의 첫 변환은 버린다 (샘플 캡 충전).
  (void)adcRead(ch);

  for (int i=0; i<count; i++)
  {
    sum += adcRead(ch);
  }

  return sum / count;
}

int32_t adcReadMilliVolt(uint8_t ch)
{
  return adcConvMilliVolt(ch, adcReadAverage(ch, ADC_AVG_COUNT));
}

int32_t adcConvMilliVolt(uint8_t ch, int32_t adc_value)
{
  int32_t mv = adc_value;


  if (ch >= ADC_MAX_CH) return 0;

  // gain/기준전압/해상도는 DTS 값을 그대로 쓴다.
  if (adc_raw_to_millivolts_dt(&adc_tbl[ch].h_dt, &mv) < 0)
  {
    return 0;
  }

  return (mv * (int32_t)adc_tbl[ch].scale) / 1000;
}

float adcReadVoltage(uint8_t ch)
{
  return (float)adcReadMilliVolt(ch) / 1000.0f;
}

float adcConvVoltage(uint8_t ch, int32_t adc_value)
{
  return (float)adcConvMilliVolt(ch, adc_value) / 1000.0f;
}

const char *adcGetName(uint8_t ch)
{
  if (ch >= ADC_MAX_CH) return "Unknown";

  return adc_tbl[ch].p_name;
}


#if CLI_USE(HW_ADC)
void cliAdc(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    cliPrintf("adc init : %d\n", is_init);
    for (int i=0; i<ADC_MAX_CH; i++)
    {
      int32_t raw = adcReadAverage(i, ADC_AVG_COUNT);

      cliPrintf("%02d. %-12s : %5d raw, %5d mV (x%d.%03d)\n",
                i, adcGetName(i), raw, adcConvMilliVolt(i, raw),
                adc_tbl[i].scale / 1000, adc_tbl[i].scale % 1000);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "show") == true)
  {
    cliShowCursor(false);
    while(cliKeepLoop())
    {
      for (int i=0; i<ADC_MAX_CH; i++)
      {
        int32_t raw = adcReadAverage(i, ADC_AVG_COUNT);

        cliPrintf("%02d. %-12s : %5d raw, %5d mV \n", i, adcGetName(i), raw, adcConvMilliVolt(i, raw));
      }
      delay(200);
      cliPrintf("\x1B[%dA", ADC_MAX_CH);
    }
    cliPrintf("\x1B[%dB", ADC_MAX_CH);
    cliShowCursor(true);
    cliRead();
    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("adc info\n");
    cliPrintf("adc show\n");
  }
}
#endif

#endif
