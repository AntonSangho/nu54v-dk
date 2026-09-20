#ifndef NVS_H_
#define NVS_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"

#ifdef _USE_HW_NVS


bool nvsInit(void);
bool nvsIsInit(void);

bool nvsIsExist(const char *p_name);
bool nvsSet(const char *p_name, void *p_data, uint32_t length);
bool nvsGet(const char *p_name, void *p_data, uint32_t length);
bool nvsDel(const char *p_name);


#endif


#ifdef __cplusplus
}
#endif

#endif
