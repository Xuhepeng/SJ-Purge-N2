#ifndef __PURGE_CONFIGSTORE_H
#define __PURGE_CONFIGSTORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*
 * 从内部 Flash 读取掉电保存的参数，并回填到 g_purge_ctrl。
 *
 * 返回值：
 * 1U  - 读取成功，且数据通过有效性校验
 * 0U  - Flash 中没有有效参数，或参数校验失败
 */
uint8_t PurgeConfigStore_Load(void);

/*
 * 将当前 g_purge_ctrl 中可配置参数保存到内部 Flash。
 *
 * 返回值：
 * 1U  - 擦写成功，且写后回读校验通过
 * 0U  - 擦写失败或写后校验失败
 */
uint8_t PurgeConfigStore_Save(void);

#ifdef __cplusplus
}
#endif

#endif /* __PURGE_CONFIGSTORE_H */
