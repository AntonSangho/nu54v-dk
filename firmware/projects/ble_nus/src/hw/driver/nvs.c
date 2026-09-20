/*
 * nvs.c — 이름으로 찾는 설정 저장소
 *
 * API 는 NU87 과 같다 (nvsSet/nvsGet/nvsIsExist). 저장은 Zephyr Settings 서브시스템에 맡긴다.
 * 백엔드는 ZMS (Zephyr Memory Storage) — 지우기 없이 쓰는 RRAM/MRAM 용으로 만들어진 것이라
 * nRF54L15 의 RRAM 에 맞다. 저장 위치는 보드 DTS 의 storage_partition (36 KB).
 *
 * 키는 "nu54/<이름>" 으로 저장한다. 같은 이름으로 다시 쓰면 새 레코드가 덧붙고,
 * 읽을 때는 마지막 값이 나온다 (ZMS 가 정리한다).
 */

#include "nvs.h"


#ifdef _USE_HW_NVS
#include "cli.h"
#include <zephyr/settings/settings.h>


#define NVS_PREFIX        "nu54"
#define NVS_NAME_MAX      SETTINGS_MAX_NAME_LEN
#define NVS_DATA_MAX      128


typedef struct
{
  const char *p_name;       // 찾는 이름 (NULL 이면 전부)
  uint8_t    *p_data;
  uint32_t    length;       // 버퍼 크기
  uint32_t    read_len;     // 실제로 읽은 크기
  bool        is_found;
} nvs_load_t;


#if CLI_USE(HW_NVS)
static void cliNvs(cli_args_t *args);
#endif


static bool is_init = false;




bool nvsInit(void)
{
  is_init = (settings_subsys_init() == 0);

  logPrintf("[%s] nvsInit()\n", is_init ? "OK" : "E_");

#if CLI_USE(HW_NVS)
  cliAdd("nvs", cliNvs);
#endif

  return is_init;
}

bool nvsIsInit(void)
{
  return is_init;
}

// settings_load_subtree_direct() 콜백. 찾는 이름이면 값을 버퍼로 읽는다.
//
static int nvsLoadCallback(const char *p_key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param)
{
  nvs_load_t *p_load = (nvs_load_t *)param;
  ssize_t     read_len;


  // 정확히 일치하는 키는 p_key 가 빈 문자열로 온다.
  if (p_key != NULL && p_key[0] != 0)
  {
    return 0;
  }

  p_load->is_found = true;

  if (p_load->p_data == NULL || p_load->length == 0)
  {
    return 0;
  }

  read_len = read_cb(cb_arg, p_load->p_data, MIN(p_load->length, len));
  if (read_len > 0)
  {
    p_load->read_len = read_len;
  }

  return 0;
}

static bool nvsLoad(const char *p_name, void *p_data, uint32_t length, uint32_t *p_read_len)
{
  char       key[NVS_NAME_MAX];
  nvs_load_t load;


  if (is_init != true || p_name == NULL) return false;

  snprintf(key, sizeof(key), NVS_PREFIX "/%s", p_name);

  load.p_name   = p_name;
  load.p_data   = (uint8_t *)p_data;
  load.length   = length;
  load.read_len = 0;
  load.is_found = false;

  if (settings_load_subtree_direct(key, nvsLoadCallback, &load) != 0)
  {
    return false;
  }

  if (p_read_len != NULL)
  {
    *p_read_len = load.read_len;
  }

  return load.is_found;
}

bool nvsIsExist(const char *p_name)
{
  return nvsLoad(p_name, NULL, 0, NULL);
}

bool nvsSet(const char *p_name, void *p_data, uint32_t length)
{
  char key[NVS_NAME_MAX];


  if (is_init != true || p_name == NULL || p_data == NULL) return false;

  snprintf(key, sizeof(key), NVS_PREFIX "/%s", p_name);

  return settings_save_one(key, p_data, length) == 0;
}

bool nvsGet(const char *p_name, void *p_data, uint32_t length)
{
  uint32_t read_len = 0;

  if (nvsLoad(p_name, p_data, length, &read_len) != true)
  {
    return false;
  }

  return read_len > 0;
}

bool nvsDel(const char *p_name)
{
  char key[NVS_NAME_MAX];


  if (is_init != true || p_name == NULL) return false;

  snprintf(key, sizeof(key), NVS_PREFIX "/%s", p_name);

  return settings_delete(key) == 0;
}


#if CLI_USE(HW_NVS)
static int cliNvsList(const char *p_key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param)
{
  uint32_t *p_count = (uint32_t *)param;

  cliPrintf("  %-16s %3d B\n", p_key, (int)len);
  (*p_count)++;

  return 0;
}

void cliNvs(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    uint32_t count = 0;

    cliPrintf("init   : %s\n", is_init ? "True" : "False");
    cliPrintf("backend: Settings + ZMS, storage_partition\n");
    cliPrintf("prefix : %s/\n", NVS_PREFIX);
    settings_load_subtree_direct(NVS_PREFIX, cliNvsList, &count);
    cliPrintf("count  : %d\n", count);
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "set"))
  {
    const char *p_name = args->getStr(1);
    const char *p_str  = args->getStr(2);

    cliPrintf("nvs set %s = \"%s\" : %s\n", p_name, p_str,
              nvsSet(p_name, (void *)p_str, strlen(p_str) + 1) ? "OK" : "Fail");
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "get"))
  {
    char        buf[NVS_DATA_MAX] = {0};
    const char *p_name = args->getStr(1);

    if (nvsGet(p_name, buf, sizeof(buf) - 1) == true)
      cliPrintf("nvs get %s = \"%s\"\n", p_name, buf);
    else
      cliPrintf("nvs get %s : 없음\n", p_name);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "del"))
  {
    const char *p_name = args->getStr(1);

    cliPrintf("nvs del %s : %s\n", p_name, nvsDel(p_name) ? "OK" : "Fail");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("nvs info\n");
    cliPrintf("nvs set name str\n");
    cliPrintf("nvs get name\n");
    cliPrintf("nvs del name\n");
  }
}
#endif

#endif
