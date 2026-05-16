#include "sensor_module.h"
#include "auto_exp_control.h"
#include "ip_param_manager.h"
#include "param_dump.h"
#include "param_manager_port.h"
#include "param_storage_flashdb.h"
#include <stdio.h>

extern void app_frame_handler(void);
extern void app_frame_handler_demo(void);

/* ================================================================
 *  系统初始化流程
 *
 *  1. param_init(storage)               框架初始化
 *  2. sensor_module_init()              IP 模块注册
 *  3. auto_exp_module_init()            Applet 模块注册
 *  4. param_load_all()                  两阶段:
 *       a. 遍历哈希表, entry->vtable->load → Flash → 缓存
 *       b. 按 MODULE_INIT_ORDER 调 init:
 *            IP_SENSOR.init()   → 传感器硬件就绪
 *            IP_ACQUISITION.init() → 采集 IP 就绪
 *            IP_LVDS_RX.init()  → LVDS 就绪
 *            IP_PWM.init()      → PWM 就绪
 *            MODULE_AUTO_EXP.init() → ae_init_cb: 缓存→算法实例
 *  5. param_check_flush_integrity()     校验 MODULE_INIT_ORDER 覆盖完整性
 *  6. 业务层动态约束: fps → exposure_max
 *  7. param_validate_all()              范围裁剪
 *  8. param_flush()                     首次写入硬件 (IP dirty → 寄存器)
 *  9. 启动帧中断 / 任务调度 → app_frame_handler()
 * ================================================================ */

static void dump_cb(const char *line, void *user_data)
{
    (void)user_data;
    printf("%s", line);
}

void app_param_manager_init(void)
{
    int ret;

#if defined(PARAM_MANAGER_PORT_FREERTOS)
    system_mutex_init();
#endif

    const param_storage_drv_t *storage = param_storage_flashdb_get_driver();
    ret = param_init(storage);
    if (ret != PARAM_OK) {
        printf("[PM] init failed: %d\n", ret);
        return;
    }

#ifdef PARAM_MODULE_AUTO_REGISTER
    param_modules_register_all();
#else
    sensor_module_init();
    auto_exp_module_init();
#endif

    ret = param_load_all();
    if (ret != PARAM_OK)
        printf("[PM] load_all ret=%d\n", ret);

    ret = param_check_flush_integrity();
    if (ret != PARAM_OK)
        printf("[PM] WARNING: MODULE_INIT_ORDER 未覆盖所有模块!\n");

    param_value_t fps, exposure_max;
    param_read(PID_IP_SENSOR_FPS, &fps);
    exposure_max.u32 = (uint32_t)(990000u / (fps.u32+1));
    param_set_range(PID_IP_SENSOR_EXPOSURE, NULL, &exposure_max);
    printf("[PM] fps=%u -> exposure_max=%u us\n", fps.u32, exposure_max.u32);

    param_validate_all();

    ret = param_flush();

    printf("[PM] init done: load_all=%d flush=%d\n", ret, ret);
    printf("[PM] --- dump after init ---\n");
    param_dump(0, dump_cb, NULL);
}

void app_constraint_demo(void)
{
    param_value_t v;

    printf("\n=== 动态约束: 锁定低帧率曝光 ===\n");
    {
        param_value_t hi = { .u32 = 50000 };
        param_set_range(PID_IP_SENSOR_EXPOSURE, NULL, &hi);

        v.u32 = 80000;
        param_write(PID_IP_SENSOR_EXPOSURE, v);
        param_read(PID_IP_SENSOR_EXPOSURE, &v);
        printf("  write 80000 -> clamped to %u\n", v.u32);
    }
    param_reset_one(PID_IP_SENSOR_EXPOSURE);
    printf("  range cleared, reset to default\n");
}

void app_demo(void)
{
    app_param_manager_init();
    app_frame_handler_demo();
    app_constraint_demo();

    printf("\n[PM] === all demos done ===\n");
}
int main(int argc, char const *argv[])
{
    (void)argc;
    (void)argv;

     extern int param_run_all_tests(void);
     param_run_all_tests();  // 取消注释以先跑测试再跑demo

    app_demo();  
    return 0;
}

