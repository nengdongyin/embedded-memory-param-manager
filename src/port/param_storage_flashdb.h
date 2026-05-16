#ifndef PARAM_STORAGE_FLASHDB_H
#define PARAM_STORAGE_FLASHDB_H

#include "param_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  FlashDB 持久化后端
 *
 *  依赖: https://github.com/armink/FlashDB
 *
 *  架构:
 *    FlashDB KVDB 存储 param_id -> param_value_t 的键值对
 *    每个参数的 value 按 param_value_t (8 bytes) 存取
 *
 *  使用方法:
 *    1. 在工程中包含 FlashDB
 *    2. 实现 fdb_kvdb_init() 的 Flash 分区配置
 *    3. 调用 param_storage_flashdb_get_driver() 获取驱动句柄
 *    4. 将该句柄传给 param_init()
 * ================================================================ */

/**
 * @brief 获取 FlashDB 持久化后端驱动
 * @return 驱动句柄 (静态分配, 无需释放)
 */
const param_storage_drv_t *param_storage_flashdb_get_driver(void);

#ifdef __cplusplus
}
#endif

#endif /* PARAM_STORAGE_FLASHDB_H */
