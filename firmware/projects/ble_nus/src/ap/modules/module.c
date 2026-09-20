/*
 * module.c — ap 모듈 관리 (NU87 구조)
 *
 * MODULE_DEF 로 선언한 모듈은 링커 섹션 .module 에 모인다 (bsp/ldscript/ldscript.ld).
 * moduleInit() 이 우선순위(HIGH → NORMAL → LOW) 순서로 각 모듈의 init() 을 부른다.
 *
 * Zephyr 에서는 모듈이 init() 안에서 자기 스레드를 만든다. 스레드는 moduleWaitReady() 로
 * 모든 모듈의 초기화가 끝난 뒤에 일을 시작한다. 스레드는 폴링하지 않고 이벤트로만 깨어난다.
 */

#include "module.h"


#define MODULE_EVT_READY    BIT(0)


typedef struct
{
  int32_t   count;
  module_t *p_module;
} module_info_t;

static bool moduleBegin(void);
#ifdef _USE_HW_CLI
static void cliModule(cli_args_t *args);
#endif

static module_info_t info;
static bool          is_begin = false;

static K_EVENT_DEFINE(module_event);

extern uint32_t _smodule;
extern uint32_t _emodule;




bool moduleInit(void)
{
  bool ret;

  info.count    = ((int)&_emodule - (int)&_smodule) / sizeof(module_t);
  info.p_module = (module_t *)&_smodule;

  logPrintf("[  ] moduleInit()\n");
  logPrintf("       count : %d\n", info.count);

  ret = moduleBegin();

#ifdef _USE_HW_CLI
  cliAdd("module", cliModule);
#endif

  // 모듈 스레드들이 일을 시작하도록 알린다.
  k_event_post(&module_event, MODULE_EVT_READY);

  return ret;
}

bool moduleBegin(void)
{
  bool ret = true;

  logPrintf("[  ] moduleBegin()\n");

  for (int pri = MODULE_PRI_HIGH; pri < MODULE_PRI_MAX; pri++)
  {
    for (int i = 0; i < info.count; i++)
    {
      assert(info.p_module[i].priority >= MODULE_PRI_HIGH && info.p_module[i].priority < MODULE_PRI_MAX);

      if (info.p_module[i].priority == pri && info.p_module[i].init != NULL)
      {
        bool mod_ret;

        mod_ret  = info.p_module[i].init();
        ret     &= mod_ret;
        logPrintf("       %s %s\n", info.p_module[i].name, mod_ret ? "OK" : "Fail");
      }
      else
      {
        if (info.p_module[i].priority < MODULE_PRI_HIGH || info.p_module[i].priority >= MODULE_PRI_MAX)
        {
          logPrintf("       %s Priority %d Fail\n", info.p_module[i].name, info.p_module[i].priority);
          ret = false;
        }
      }
    }
  }

  is_begin = true;

  return ret;
}

bool moduleIsReady(void)
{
  return is_begin;
}

// 모든 모듈의 init() 이 끝날 때까지 기다린다 (모듈 스레드의 시작 부분에서 부른다).
//
void moduleWaitReady(void)
{
  k_event_wait(&module_event, MODULE_EVT_READY, false, K_FOREVER);
}

bool moduleUpdate(void)
{
  for (int i = 0; i < info.count; i++)
  {
    if (info.p_module[i].update != NULL)
    {
      info.p_module[i].update(info.p_module[i].arg);
    }
  }

  return true;
}

#ifdef _USE_HW_CLI
static void cliModuleThread(const struct k_thread *thread, void *user_data)
{
  struct k_thread *p_thread = (struct k_thread *)thread;
  const char      *p_name   = k_thread_name_get(p_thread);
  size_t           unused   = 0;
  size_t           size     = p_thread->stack_info.size;

  k_thread_stack_space_get(p_thread, &unused);

  cliPrintf("%-12s pri %3d, stack %5d / %5d\n",
            p_name != NULL ? p_name : "-",
            k_thread_priority_get(p_thread),
            (int)(size - unused), (int)size);
}

void cliModule(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    module_t *p_module;

    cliPrintf("count     : %d\n", info.count);
    p_module = info.p_module;
    for (int i = 0; i < info.count; i++)
    {
      cliPrintf("%d : %-16s pri %d\n",
                i,
                p_module[i].name,
                p_module[i].priority);
    }
    ret = true;
  }

  // 스레드별 우선순위 / 스택 사용량 (사용 / 전체 바이트)
  if (args->argc == 1 && args->isStr(0, "thread"))
  {
    k_thread_foreach(cliModuleThread, NULL);
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("module info\n");
    cliPrintf("module thread\n");
  }
}
#endif
