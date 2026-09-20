/*
 * button.c — 보드 스위치 SW1~SW4
 *
 * 저전력 : 주기 스캔을 하지 않는다.
 *   - 핀 변화(누름/뗌)는 GPIO SENSE 인터럽트로만 알아챈다 (보드 DTS sense-edge-mask).
 *   - 인터럽트가 오면 디바운스 타이머를 한 번 돌려 상태를 확정한다. 흔들림이 이어지면 타이머를 다시 미룬다.
 *   - 누른 시간은 조회할 때 계산한다 → 눌린 채로 있어도 CPU 는 깨어나지 않는다.
 *   - 길게 누름은 누를 때 건 1회 타이머로 한 번만 깨어나 알린다.
 *
 * 인식하는 동작 (buttonGetEvent 로 꺼내면 지워진다)
 *   BUTTON_EVT_PRESSED   눌림
 *   BUTTON_EVT_RELEASED  뗌
 *   BUTTON_EVT_CLICK     길게 누름 시간 전에 뗌
 *   BUTTON_EVT_LONG      길게 누름 시간이 지남 (떼기 전에 발생, 이후 뗄 때 CLICK 은 없음)
 */

#include "button.h"


#ifdef _USE_HW_BUTTON
#include "cli.h"
#include "uart.h"
#include <zephyr/drivers/gpio.h>


#define NAME_DEF(x)             x, #x

#define BUTTON_DEBOUNCE_MS      20
#define BUTTON_LONG_MS          1000


typedef struct
{
  bool           pressed;
  uint32_t       pressed_start_time;
  uint32_t       pressed_end_time;

  atomic_t       event;             // BUTTON_EVT_xxx 비트
  bool           long_fired;
  uint32_t       long_time;
  struct k_timer long_timer;
} button_t;

typedef struct
{
  struct gpio_dt_spec h_dt;           // 극성(ACTIVE_LOW)/풀업은 DTS 에서 → 읽은 값 1 = 눌림
  ButtonPinName_t     pin_name;
  const char         *p_name;
} button_pin_t;


#if CLI_USE(HW_BUTTON)
static void cliButton(cli_args_t *args);
#endif
static void buttonGpioCallback(const struct device *port, struct gpio_callback *cb, uint32_t pins);
static void buttonDebounce(struct k_timer *timer);
static void buttonUpdate(void);
static void buttonLongTimeout(struct k_timer *timer);


static const button_pin_t button_pin[BUTTON_MAX_CH] =
{
  {GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios), NAME_DEF(BTN1)},    // SW1 : P1.13
  {GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios), NAME_DEF(BTN2)},    // SW2 : P1.09
  {GPIO_DT_SPEC_GET(DT_ALIAS(sw2), gpios), NAME_DEF(BTN3)},    // SW3 : P1.08
  {GPIO_DT_SPEC_GET(DT_ALIAS(sw3), gpios), NAME_DEF(BTN4)},    // SW4 : P0.04
};

static button_t             button_tbl[BUTTON_MAX_CH];
static struct gpio_callback button_cb[BUTTON_MAX_CH];
static void               (*event_func)(void) = NULL;

static K_TIMER_DEFINE(debounce_timer, buttonDebounce, NULL);




bool buttonInit(void)
{
  bool ret = true;


  for (int i=0; i<BUTTON_MAX_CH; i++)
  {
    const struct gpio_dt_spec *p_dt = &button_pin[i].h_dt;

    if (!gpio_is_ready_dt(p_dt) ||
        gpio_pin_configure_dt(p_dt, GPIO_INPUT) < 0 ||
        gpio_pin_interrupt_configure_dt(p_dt, GPIO_INT_EDGE_BOTH) < 0)
    {
      ret = false;
      continue;
    }

    gpio_init_callback(&button_cb[i], buttonGpioCallback, BIT(p_dt->pin));
    gpio_add_callback(p_dt->port, &button_cb[i]);

    button_tbl[i].pressed            = false;
    button_tbl[i].pressed_start_time = 0;
    button_tbl[i].pressed_end_time   = 0;
    button_tbl[i].long_fired         = false;
    button_tbl[i].long_time          = BUTTON_LONG_MS;
    atomic_clear(&button_tbl[i].event);
    k_timer_init(&button_tbl[i].long_timer, buttonLongTimeout, NULL);
    k_timer_user_data_set(&button_tbl[i].long_timer, &button_tbl[i]);
  }

  buttonUpdate();

  logPrintf("[%s] buttonInit()\n", ret ? "OK" : "E_");

#if CLI_USE(HW_BUTTON)
  cliAdd("button", cliButton);
#endif

  return ret;
}

// 핀이 바뀔 때마다 디바운스 시간을 다시 잡는다 (흔들림이 멈춘 뒤 한 번만 처리).
//
void buttonGpioCallback(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
  k_timer_start(&debounce_timer, K_MSEC(BUTTON_DEBOUNCE_MS), K_NO_WAIT);
}

void buttonDebounce(struct k_timer *timer)
{
  buttonUpdate();
}

void buttonUpdate(void)
{
  bool is_changed = false;


  for (int i=0; i<BUTTON_MAX_CH; i++)
  {
    bool pressed = gpio_pin_get_dt(&button_pin[i].h_dt) == 1;

    if (pressed == button_tbl[i].pressed)
    {
      continue;
    }

    button_t *p_btn = &button_tbl[i];

    if (pressed)
    {
      p_btn->pressed_start_time = millis();
      p_btn->long_fired         = false;
      atomic_or(&p_btn->event, BUTTON_EVT_PRESSED);
      k_timer_start(&p_btn->long_timer, K_MSEC(p_btn->long_time), K_NO_WAIT);
    }
    else
    {
      p_btn->pressed_end_time = millis();
      k_timer_stop(&p_btn->long_timer);
      atomic_or(&p_btn->event, p_btn->long_fired ? BUTTON_EVT_RELEASED : (BUTTON_EVT_RELEASED | BUTTON_EVT_CLICK));
    }

    p_btn->pressed = pressed;
    is_changed = true;
  }

  if (is_changed && event_func != NULL)
  {
    event_func();
  }
}

void buttonLongTimeout(struct k_timer *timer)
{
  button_t *p_btn = (button_t *)k_timer_user_data_get(timer);

  if (p_btn->pressed)
  {
    p_btn->long_fired = true;
    atomic_or(&p_btn->event, BUTTON_EVT_LONG);

    if (event_func != NULL)
    {
      event_func();
    }
  }
}

// 쌓인 이벤트(BUTTON_EVT_xxx 비트)를 꺼내고 지운다.
//
uint32_t buttonGetEvent(uint8_t ch)
{
  if (ch >= BUTTON_MAX_CH) return 0;

  return (uint32_t)atomic_clear(&button_tbl[ch].event);
}

void buttonSetLongTime(uint8_t ch, uint32_t long_ms)
{
  if (ch >= BUTTON_MAX_CH) return;

  button_tbl[ch].long_time = long_ms;
}

void buttonSetEventISR(void (*func)(void))
{
  event_func = func;
}

bool buttonGetPressed(uint8_t ch)
{
  if (ch >= BUTTON_MAX_CH)
  {
    return false;
  }

  return button_tbl[ch].pressed;
}

uint32_t buttonGetData(void)
{
  uint32_t ret = 0;


  for (int i=0; i<BUTTON_MAX_CH; i++)
  {
    ret |= (buttonGetPressed(i)<<i);
  }

  return ret;
}

uint8_t buttonGetPressedCount(void)
{
  uint8_t ret = 0;

  for (int i=0; i<BUTTON_MAX_CH; i++)
  {
    if (buttonGetPressed(i) == true)
    {
      ret++;
    }
  }

  return ret;
}

// 눌려 있으면 지금까지 누른 시간, 떼었으면 마지막으로 누른 시간
//
uint32_t buttonGetPressedTime(uint8_t ch)
{
  if (ch >= BUTTON_MAX_CH) return 0;

  if (button_tbl[ch].pressed)
    return millis() - button_tbl[ch].pressed_start_time;
  else
    return button_tbl[ch].pressed_end_time - button_tbl[ch].pressed_start_time;
}

const char *buttonGetName(uint8_t ch)
{
  if (ch >= BUTTON_MAX_CH) return "Unknown";

  return button_pin[ch].p_name;
}


#if CLI_USE(HW_BUTTON)
static volatile bool cli_event = false;

static void cliButtonEvent(void)
{
  cli_event = true;
}

void cliButton(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    for (int i=0; i<BUTTON_MAX_CH; i++)
    {
      cliPrintf("%-6s : P%d.%02d, %s\n",
                buttonGetName(i),
                button_pin[i].h_dt.port == DEVICE_DT_GET(DT_NODELABEL(gpio0)) ? 0 :
                button_pin[i].h_dt.port == DEVICE_DT_GET(DT_NODELABEL(gpio1)) ? 1 : 2,
                button_pin[i].h_dt.pin,
                buttonGetPressed(i) ? "pressed" : "released");
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "show"))
  {
    while(cliKeepLoop())
    {
      for (int i=0; i<BUTTON_MAX_CH; i++)
      {
        cliPrintf("%d", buttonGetPressed(i));
      }
      delay(50);
      cliPrintf("\r");
    }
    cliRead();
    ret = true;
  }

  // 눌림/뗌/클릭/길게 누름이 생길 때마다 한 줄씩 출력 (이벤트 콜백 사용, 이벤트가 없으면 sleep)
  //
  if (args->argc == 1 && args->isStr(0, "event"))
  {
    for (int i=0; i<BUTTON_MAX_CH; i++)
    {
      buttonGetEvent(i);
    }

    buttonSetEventISR(cliButtonEvent);
    while(cliKeepLoop())
    {
      if (cli_event)
      {
        cli_event = false;
        for (int i=0; i<BUTTON_MAX_CH; i++)
        {
          uint32_t event = buttonGetEvent(i);

          if (event & BUTTON_EVT_PRESSED)  cliPrintf("%-6s pressed\n", buttonGetName(i));
          if (event & BUTTON_EVT_LONG)     cliPrintf("%-6s long\n", buttonGetName(i));
          if (event & BUTTON_EVT_CLICK)    cliPrintf("%-6s click\n", buttonGetName(i));
          if (event & BUTTON_EVT_RELEASED) cliPrintf("%-6s released, %d ms\n", buttonGetName(i), buttonGetPressedTime(i));
        }
      }
      uartWaitRx(cliGetPort(), 10);
    }
    buttonSetEventISR(NULL);
    cliRead();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("button info\n");
    cliPrintf("button show\n");
    cliPrintf("button event\n");
  }
}
#endif



#endif
