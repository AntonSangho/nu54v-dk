/*
 * rtc.c — 날짜·시계 (nRF54L15)
 *
 * 이 칩에는 달력 RTC 가 없다. NU87(RTL8720DF) 과 같은 방식으로 기준 시각과 카운터로 만든다.
 *
 *     epoch = base_epoch + (GRTC 카운터 - base_count) / 1초
 *
 * GRTC 는 LFXO(32.768 kHz, Y1) 로 돌고 System OFF 에서도 계속 센다.
 * base_epoch / base_count / 시간대는 **보존 RAM**(보드 DTS 의 retainedmem0)에 둔다.
 * 리셋이나 System OFF 로는 지워지지 않고, 전원이 완전히 끊기면 사라진다 → rtcIsTimeSet() 이 false.
 *
 * ⚠ GRTC 카운터는 리셋하면 0 부터 다시 센다 (실측). 그래서 현재 시각을 주기적으로
 *   (RTC_SYNC_PERIOD_MS) 보존 RAM 에 다시 적고(rtcSync), 리셋·System OFF 직전에도 적는다.
 *   부팅 때 카운터가 기준보다 작으면 되감긴 것이므로 마지막으로 적어 둔 시각에서 이어간다.
 *   그 사이(리셋에 걸린 시간, 저장 주기만큼)는 시계가 뒤처진다.
 *
 * 시간대는 nvs 에도 저장해 전원을 껐다 켜도 남는다 (시각 자체는 다시 맞춰야 한다).
 *
 * 저전력 : 1초 틱 인터럽트를 쓰지 않는다. 시각은 물어볼 때 계산한다.
 */

#include "rtc.h"


#ifdef _USE_HW_RTC
#include "cli.h"
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/drivers/timer/nrf_grtc_timer.h>

#ifdef _USE_HW_NVS
#include "nvs.h"
#define RTC_NVS_TZ_NAME       "rtc_tz"
#endif


#define RTC_MAGIC             0x52544331UL    // "RTC1"
#define RTC_USER_REG_MAX      4

#define RTC_TZ_DEFAULT_MIN    540             // UTC+9 (한국)
#define RTC_SYNC_PERIOD_MS    60000           // 보존 RAM 에 현재 시각을 다시 적는 주기


typedef struct
{
  uint32_t magic;
  uint32_t base_epoch;        // 기준 시각 (UTC, 초)
  uint64_t base_count;        // 그때의 GRTC 카운터
  int16_t  tz_min;            // UTC 로부터의 분
  uint16_t reserved;
  uint32_t user_reg[RTC_USER_REG_MAX];
  uint32_t crc;
} rtc_retained_t;


#if CLI_USE(HW_RTC)
static void cliRtc(cli_args_t *args);
#endif
static bool     rtcLoad(void);
static bool     rtcSave(void);
static uint32_t rtcCalcCrc(rtc_retained_t *p_data);
static uint32_t rtcCivilToEpoch(uint32_t year, uint32_t month, uint32_t day,
                                uint32_t hour, uint32_t minute, uint32_t second);
static void     rtcEpochToCivil(uint32_t epoch, rtc_info_t *p_info);


static void rtcSyncTimeout(struct k_timer *timer);

static bool is_init = false;
static rtc_retained_t rtc_data;
static K_TIMER_DEFINE(sync_timer, rtcSyncTimeout, NULL);
static const struct device *h_retained = DEVICE_DT_GET(DT_ALIAS(retainedmemdevice));




bool rtcInit(void)
{
  is_init = device_is_ready(h_retained);

  if (is_init)
  {
    if (rtcLoad() != true)
    {
      // 전원이 새로 들어왔다 → 시각 없음, 시간대만 기본값
      memset(&rtc_data, 0, sizeof(rtc_data));
      rtc_data.magic  = RTC_MAGIC;
      rtc_data.tz_min = RTC_TZ_DEFAULT_MIN;
      rtcSave();
    }
  }

  // 카운터가 되감겼으면(리셋) 마지막으로 적어 둔 시각에서 이어간다.
  if (is_init && rtc_data.base_epoch > 0 && z_nrf_grtc_timer_read() < rtc_data.base_count)
  {
    rtc_data.base_count = z_nrf_grtc_timer_read();
    rtcSave();
  }

#ifdef _USE_HW_NVS
  // 시간대는 전원을 껐다 켜도 남도록 nvs 에서 읽는다.
  {
    int16_t tz_min;

    if (nvsGet(RTC_NVS_TZ_NAME, &tz_min, sizeof(tz_min)) == true)
    {
      rtc_data.tz_min = tz_min;
      rtcSave();
    }
  }
#endif

  logPrintf("[%s] rtcInit()\n", is_init ? "OK" : "E_");
  if (is_init)
  {
    rtc_info_t info;

    logPrintf("     UTC%+d:%02d\n", rtc_data.tz_min / 60, abs(rtc_data.tz_min) % 60);
    if (rtcIsTimeSet() && rtcGetInfo(&info))
    {
      logPrintf("     20%02d-%02d-%02d %02d:%02d:%02d\n",
                info.date.year, info.date.month, info.date.day,
                info.time.hours, info.time.minutes, info.time.seconds);
    }
    else
    {
      logPrintf("     시각 미설정\n");
    }
  }

  if (is_init)
  {
    k_timer_start(&sync_timer, K_MSEC(RTC_SYNC_PERIOD_MS), K_MSEC(RTC_SYNC_PERIOD_MS));
  }

#if CLI_USE(HW_RTC)
  cliAdd("rtc", cliRtc);
#endif

  return is_init;
}

// 보존 RAM
//
bool rtcLoad(void)
{
  rtc_retained_t data;

  if (retained_mem_read(h_retained, 0, (uint8_t *)&data, sizeof(data)) != 0)
  {
    return false;
  }

  if (data.magic != RTC_MAGIC || data.crc != rtcCalcCrc(&data))
  {
    return false;
  }

  rtc_data = data;
  return true;
}

bool rtcSave(void)
{
  if (is_init != true) return false;

  rtc_data.magic = RTC_MAGIC;
  rtc_data.crc   = rtcCalcCrc(&rtc_data);

  return retained_mem_write(h_retained, 0, (uint8_t *)&rtc_data, sizeof(rtc_data)) == 0;
}

uint32_t rtcCalcCrc(rtc_retained_t *p_data)
{
  uint32_t  crc = 0;
  uint8_t  *p_buf = (uint8_t *)p_data;

  // crc 필드 앞까지 더한다.
  for (uint32_t i = 0; i < offsetof(rtc_retained_t, crc); i++)
  {
    crc += p_buf[i];
  }

  return crc ^ RTC_MAGIC;
}

bool rtcIsTimeSet(void)
{
  if (is_init != true) return false;

  return (rtc_data.base_epoch > 0) && (z_nrf_grtc_timer_read() >= rtc_data.base_count);
}

// 현재 시각을 보존 RAM 에 다시 적는다 (기준을 지금으로 옮긴다).
// 리셋이나 System OFF 로 카운터가 되감겨도 여기까지는 살아남는다.
//
bool rtcSync(void)
{
  uint32_t epoch = rtcGetEpochTime();

  if (epoch == 0) return false;

  rtc_data.base_epoch = epoch;
  rtc_data.base_count = z_nrf_grtc_timer_read();

  return rtcSave();
}

void rtcSyncTimeout(struct k_timer *timer)
{
  rtcSync();
}

uint32_t rtcGetEpochTime(void)
{
  uint64_t now;
  uint32_t sec;


  if (rtcIsTimeSet() != true) return 0;

  now = z_nrf_grtc_timer_read();
  sec = (uint32_t)((now - rtc_data.base_count) / sys_clock_hw_cycles_per_sec());

  return rtc_data.base_epoch + sec;
}

bool rtcSetEpochTime(uint32_t epoch)
{
  if (is_init != true) return false;

  rtc_data.base_epoch = epoch;
  rtc_data.base_count = z_nrf_grtc_timer_read();

  return rtcSave();
}

int16_t rtcGetTimeZone(void)
{
  return rtc_data.tz_min;
}

bool rtcSetTimeZone(int16_t offset_min)
{
  if (is_init != true) return false;

  rtc_data.tz_min = offset_min;

#ifdef _USE_HW_NVS
  nvsSet(RTC_NVS_TZ_NAME, &offset_min, sizeof(offset_min));
#endif

  return rtcSave();
}

// 지역 시각 (시간대 적용)
//
bool rtcGetInfo(rtc_info_t *rtc_info)
{
  uint32_t epoch = rtcGetEpochTime();

  if (epoch == 0) return false;

  rtcEpochToCivil((uint32_t)((int32_t)epoch + rtc_data.tz_min * 60), rtc_info);

  return true;
}

bool rtcGetTime(rtc_time_t *rtc_time)
{
  rtc_info_t info;

  if (rtcGetInfo(&info) != true) return false;

  *rtc_time = info.time;
  return true;
}

bool rtcGetDate(rtc_date_t *rtc_date)
{
  rtc_info_t info;

  if (rtcGetInfo(&info) != true) return false;

  *rtc_date = info.date;
  return true;
}

bool rtcSetInfo(rtc_info_t *rtc_info)
{
  uint32_t epoch;

  epoch = rtcCivilToEpoch(2000 + rtc_info->date.year, rtc_info->date.month, rtc_info->date.day,
                          rtc_info->time.hours, rtc_info->time.minutes, rtc_info->time.seconds);

  // 넣은 값은 지역 시각이므로 UTC 로 바꿔 저장한다.
  return rtcSetEpochTime((uint32_t)((int32_t)epoch - rtc_data.tz_min * 60));
}

bool rtcSetTime(rtc_time_t *rtc_time)
{
  rtc_info_t info;

  if (rtcGetInfo(&info) != true)
  {
    // 시각이 없으면 날짜는 2026-01-01 로 둔다.
    info.date.year  = 26;
    info.date.month = 1;
    info.date.day   = 1;
  }
  info.time = *rtc_time;

  return rtcSetInfo(&info);
}

bool rtcSetDate(rtc_date_t *rtc_date)
{
  rtc_info_t info;

  if (rtcGetInfo(&info) != true)
  {
    info.time.hours   = 0;
    info.time.minutes = 0;
    info.time.seconds = 0;
  }
  info.date = *rtc_date;

  return rtcSetInfo(&info);
}

bool rtcSetReg(uint32_t index, uint32_t data)
{
  if (is_init != true || index >= RTC_USER_REG_MAX) return false;

  rtc_data.user_reg[index] = data;

  return rtcSave();
}

bool rtcGetReg(uint32_t index, uint32_t *p_data)
{
  if (is_init != true || index >= RTC_USER_REG_MAX) return false;

  *p_data = rtc_data.user_reg[index];

  return true;
}

// epoch ↔ 연월일 (Howard Hinnant 의 days_from_civil / civil_from_days)
//
uint32_t rtcCivilToEpoch(uint32_t year, uint32_t month, uint32_t day,
                         uint32_t hour, uint32_t minute, uint32_t second)
{
  int32_t  y = (int32_t)year - (month <= 2 ? 1 : 0);
  int32_t  era = (y >= 0 ? y : y - 399) / 400;
  uint32_t yoe = (uint32_t)(y - era * 400);
  uint32_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int32_t  days = era * 146097 + (int32_t)doe - 719468;

  return (uint32_t)days * 86400 + hour * 3600 + minute * 60 + second;
}

void rtcEpochToCivil(uint32_t epoch, rtc_info_t *p_info)
{
  int32_t  days = (int32_t)(epoch / 86400);
  uint32_t secs = epoch % 86400;

  int32_t  z = days + 719468;
  int32_t  era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t  y = (int32_t)yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  uint32_t d = doy - (153 * mp + 2) / 5 + 1;
  uint32_t m = mp + (mp < 10 ? 3 : -9);

  y += (m <= 2);

  p_info->date.year  = (uint8_t)(y - 2000);
  p_info->date.month = (uint8_t)m;
  p_info->date.day   = (uint8_t)d;
  p_info->date.week  = (uint8_t)((days + 4) % 7);      // 1970-01-01 = 목요일

  p_info->time.hours   = (uint8_t)(secs / 3600);
  p_info->time.minutes = (uint8_t)((secs % 3600) / 60);
  p_info->time.seconds = (uint8_t)(secs % 60);
}


#if CLI_USE(HW_RTC)
static const char *week_str[7] = {"일", "월", "화", "수", "목", "금", "토"};

static void cliRtcInfo(void)
{
  rtc_info_t info;

  if (rtcGetInfo(&info) != true)
  {
    cliPrintf("시각 미설정 (rtc set date/time 또는 rtc set epoch)\n");
    return;
  }

  cliPrintf("20%02d-%02d-%02d (%s) %02d:%02d:%02d   UTC%+d:%02d\n",
            info.date.year, info.date.month, info.date.day, week_str[info.date.week % 7],
            info.time.hours, info.time.minutes, info.time.seconds,
            rtc_data.tz_min / 60, abs(rtc_data.tz_min) % 60);
  cliPrintf("epoch : %u (UTC)\n", rtcGetEpochTime());
}

void cliRtc(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliRtcInfo();
    cliPrintf("base  : epoch %u, count %llu\n", rtc_data.base_epoch, rtc_data.base_count);
    cliPrintf("now   : count %llu (%d Hz)\n", z_nrf_grtc_timer_read(), sys_clock_hw_cycles_per_sec());
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "show"))
  {
    while(cliKeepLoop())
    {
      cliRtcInfo();
      delay(1000);
    }
    cliRead();
    ret = true;
  }

  if (args->argc == 5 && args->isStr(0, "set") && args->isStr(1, "date"))
  {
    rtc_date_t date;

    date.year  = (uint8_t)(args->getData(2) % 100);
    date.month = (uint8_t)args->getData(3);
    date.day   = (uint8_t)args->getData(4);

    cliPrintf("rtc set date : %s\n", rtcSetDate(&date) ? "OK" : "Fail");
    cliRtcInfo();
    ret = true;
  }

  if (args->argc == 5 && args->isStr(0, "set") && args->isStr(1, "time"))
  {
    rtc_time_t time;

    time.hours   = (uint8_t)args->getData(2);
    time.minutes = (uint8_t)args->getData(3);
    time.seconds = (uint8_t)args->getData(4);

    cliPrintf("rtc set time : %s\n", rtcSetTime(&time) ? "OK" : "Fail");
    cliRtcInfo();
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "set") && args->isStr(1, "epoch"))
  {
    uint32_t epoch = (uint32_t)args->getData(2);

    cliPrintf("rtc set epoch : %s\n", rtcSetEpochTime(epoch) ? "OK" : "Fail");
    cliRtcInfo();
    ret = true;
  }

  // 시간대 (시 단위). 예: rtc tz 9
  if (args->argc == 2 && args->isStr(0, "tz"))
  {
    int16_t hour = (int16_t)args->getData(1);

    cliPrintf("rtc tz UTC%+d : %s\n", hour, rtcSetTimeZone(hour * 60) ? "OK" : "Fail");
    cliRtcInfo();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("rtc info\n");
    cliPrintf("rtc show\n");
    cliPrintf("rtc set date [year] [month] [day]\n");
    cliPrintf("rtc set time [h] [m] [s]\n");
    cliPrintf("rtc set epoch [sec]\n");
    cliPrintf("rtc tz [hour]\n");
  }
}
#endif

#endif
