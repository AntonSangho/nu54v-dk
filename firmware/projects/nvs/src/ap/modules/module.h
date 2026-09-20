#ifndef MODULE_H_
#define MODULE_H_

#include "ap_def.h"



#ifdef __cplusplus
extern "C"
{
#endif


  typedef enum
  {
    MODULE_PRI_HIGH = 1,
    MODULE_PRI_NORMAL,
    MODULE_PRI_LOW,
    MODULE_PRI_MAX,
  } ModulePriority_t;

  typedef struct module_t_
  {
    const char       name[32];
    ModulePriority_t priority;
    bool (*init)(void);
    void (*update)(void const *arg);   // bare-metal 용 (NU87 호환). Zephyr 에서는 모듈이 자기 스레드를 만든다
    void        *arg;
  } module_t;

#define MODULE_DEF(x_name) static __attribute__((section(".module"))) volatile module_t module_##x_name =


bool moduleInit(void);
bool moduleUpdate(void);
bool moduleIsReady(void);
void moduleWaitReady(void);

#ifdef __cplusplus
}
#endif


#endif
