#include "param_manager.h"
#include "ip_param_manager.h"
#include "param_manager_port.h"
#include "param_storage_flashdb.h"
#include "module_ids.h"
#include <stdio.h>
#include <string.h>

#define PWM_REG_PERIOD   0x00
#define PWM_REG_DUTY     0x04
#define PWM_REG_CONTROL  0x08
#define PWM_REG_PRESCALE 0x0C

/* ================================================================
 *  PWM Driver (模拟 Xilinx BSP 风格)
 *
 *  driver 内部维护 base_addr、寄存器偏移、读写时序。
 *  param_manager 完全不接触硬件地址和位宽。
 * ================================================================ */
typedef struct {
    uint32_t base_addr;
    uint32_t shadow[4];
} pwm_driver_t;

static uint32_t pwm_reg_read(pwm_driver_t *p, uint32_t offset)
{
    return p->shadow[offset / 4];
}

static void pwm_reg_write(pwm_driver_t *p, uint32_t offset, uint32_t val)
{
    p->shadow[offset / 4] = val;
    printf("    [HW] base+0x%02lX = %lu\n",
           (unsigned long)offset, (unsigned long)val);
}

/* ================================================================
 *  参数 ID
 * ================================================================ */
enum {
    PWM_ID_PERIOD   = 0,
    PWM_ID_DUTY     = 1,
    PWM_ID_DUTY_PCT = 2,
    PWM_ID_ENABLE   = 3,
    PWM_ID_PRESCALE = 4,
};

#define PID_PWM_PERIOD     MAKE_PARAM_ID(IP_PWM, PWM_ID_PERIOD)
#define PID_PWM_DUTY       MAKE_PARAM_ID(IP_PWM, PWM_ID_DUTY)
#define PID_PWM_DUTY_PCT   MAKE_PARAM_ID(IP_PWM, PWM_ID_DUTY_PCT)
#define PID_PWM_ENABLE     MAKE_PARAM_ID(IP_PWM, PWM_ID_ENABLE)
#define PID_PWM_PRESCALE   MAKE_PARAM_ID(IP_PWM, PWM_ID_PRESCALE)

/* ================================================================
 *  IP 参数 — 静态全局定义, 零 malloc
 * ================================================================ */
PARAM_IP_UINT (pwm_period,   PID_PWM_PERIOD,   PARAM_FLAG_PERSIST, 1000);
PARAM_IP_UINT (pwm_duty,     PID_PWM_DUTY,     PARAM_FLAG_PERSIST, 0);
PARAM_IP_FLOAT(pwm_duty_pct, PID_PWM_DUTY_PCT, PARAM_FLAG_PERSIST, 0.0f);
PARAM_IP_UINT (pwm_enable,   PID_PWM_ENABLE,   0,                   0);
PARAM_IP_UINT (pwm_prescale, PID_PWM_PRESCALE, PARAM_FLAG_PERSIST, 1);

PARAM_TABLE(pwm_params,
    &pwm_period.base,
    &pwm_duty.base,
    &pwm_duty_pct.base,
    &pwm_enable.base,
    &pwm_prescale.base,
);

/* ================================================================
 *  Driver 回调 — 按 local_id 分流
 *
 *  PWM_ID_DUTY_PCT (FLOAT) 和 PWM_ID_DUTY (UINT) 共享同一硬件寄存器,
 *  但提供不同视角: 高级抽象 vs 原始寄存器。
 * ================================================================ */
static int pwm_param_read(void *drv, uint16_t local_id, param_value_t *value)
{
    pwm_driver_t *p = (pwm_driver_t *)drv;

    switch (local_id) {
    case PWM_ID_PERIOD:
        value->u32 = pwm_reg_read(p, PWM_REG_PERIOD);
        break;
    case PWM_ID_DUTY:
        value->u32 = pwm_reg_read(p, PWM_REG_DUTY);
        break;
    case PWM_ID_DUTY_PCT: {
        uint32_t period = pwm_reg_read(p, PWM_REG_PERIOD);
        uint32_t duty   = pwm_reg_read(p, PWM_REG_DUTY);
        value->f32 = (period > 0) ? (float)duty * 100.0f / (float)period : 0.0f;
        break;
    }
    case PWM_ID_ENABLE:
        value->u32 = pwm_reg_read(p, PWM_REG_CONTROL) & 1u;
        break;
    case PWM_ID_PRESCALE:
        value->u32 = pwm_reg_read(p, PWM_REG_PRESCALE);
        break;
    default:
        return PARAM_ERR_INVALID_ID;
    }
    return PARAM_OK;
}

static int pwm_param_write(void *drv, uint16_t local_id, param_value_t value)
{
    pwm_driver_t *p = (pwm_driver_t *)drv;

    switch (local_id) {
    case PWM_ID_PERIOD:
        if (value.u32 == 0) return PARAM_ERR_OUT_OF_RANGE;
        pwm_reg_write(p, PWM_REG_PERIOD, value.u32);
        break;
    case PWM_ID_DUTY: {
        uint32_t period = pwm_reg_read(p, PWM_REG_PERIOD);
        if (value.u32 > period) return PARAM_ERR_OUT_OF_RANGE;
        pwm_reg_write(p, PWM_REG_DUTY, value.u32);
        break;
    }
    case PWM_ID_DUTY_PCT: {
        if (value.f32 < 0.0f || value.f32 > 100.0f)
            return PARAM_ERR_OUT_OF_RANGE;
        uint32_t period = pwm_reg_read(p, PWM_REG_PERIOD);
        uint32_t duty   = (uint32_t)(value.f32 * (float)period / 100.0f);
        pwm_reg_write(p, PWM_REG_DUTY, duty);
        break;
    }
    case PWM_ID_ENABLE: {
        uint32_t ctrl = pwm_reg_read(p, PWM_REG_CONTROL);
        if (value.u32) ctrl |= 1u; else ctrl &= ~1u;
        pwm_reg_write(p, PWM_REG_CONTROL, ctrl);
        break;
    }
    case PWM_ID_PRESCALE:
        if (value.u32 == 0) return PARAM_ERR_OUT_OF_RANGE;
        pwm_reg_write(p, PWM_REG_PRESCALE, value.u32);
        break;
    default:
        return PARAM_ERR_INVALID_ID;
    }
    return PARAM_OK;
}

/* ================================================================
 *  Driver 实例 + 注册
 * ================================================================ */
static pwm_driver_t g_pwm_driver = { .base_addr = 0x40010000 };

IP_DRIVER_DEFINE(pwm, IP_PWM, "AXI_Timer_PWM",
                 &g_pwm_driver, pwm_param_read, pwm_param_write);
IP_DRIVER_INIT(pwm, pwm_params);

/* ================================================================
 *  使用演示
 * ================================================================ */
void pwm_demo_init(void)
{
    int ret;

#if defined(PARAM_MANAGER_PORT_FREERTOS)
    system_mutex_init();
#endif

    const param_storage_drv_t *storage = param_storage_flashdb_get_driver();
    ret = param_init(storage);
    if (ret != PARAM_OK) {
        printf("[PWM] init failed: %d\n", ret);
        return;
    }

    pwm_init();
    param_load_all();
}

void pwm_demo_run(void)
{
    param_value_t v;
    int ret;

    printf("\n=== 1. UINT 参数: 周期 + 预分频 ===\n");
    v.u32 = 1000;
    ret = param_write(PID_PWM_PERIOD, v);
    printf("  write period=%u → ret=%d\n", v.u32, ret);

    v.u32 = 4;
    ret = param_write(PID_PWM_PRESCALE, v);
    printf("  write prescale=%u → ret=%d\n", v.u32, ret);

    printf("\n=== 2. FLOAT 高级抽象: 占空比 75.5%% ===\n");
    v.f32 = 75.5f;
    ret = param_write(PID_PWM_DUTY_PCT, v);
    printf("  write duty_percent=%.1f%% → ret=%d\n", v.f32, ret);

    param_read(PID_PWM_DUTY_PCT, &v);
    printf("  read  duty_percent=%.1f%%\n", v.f32);

    param_read(PID_PWM_DUTY, &v);
    printf("  read  duty_clocks=%u (expect 755)\n", v.u32);

    printf("\n=== 3. UINT 原始寄存器: duty=500 clocks ===\n");
    v.u32 = 500;
    ret = param_write(PID_PWM_DUTY, v);
    printf("  write duty=%u → ret=%d\n", v.u32, ret);

    param_read(PID_PWM_DUTY, &v);
    printf("  read  duty=%u\n", v.u32);

    param_read(PID_PWM_DUTY_PCT, &v);
    printf("  read  duty_percent=%.1f%% (expect 50.0)\n", v.f32);

    printf("\n=== 4. write_immediate 使能 ===\n");
    v.u32 = 1;
    ret = param_write_immediate(PID_PWM_ENABLE, v);
    printf("  write_immediate enable=1 → ret=%d\n", ret);

    printf("\n=== 5. flush + save ===\n");
    ret = param_flush();
    printf("  flush → ret=%d\n", ret);
    ret = param_save_all();
    printf("  save  → ret=%d\n", ret);

    printf("\n=== 6. ip_control 调试 ===\n");
    {
        uint32_t raw;
        ip_read(IP_PWM, PWM_ID_DUTY, (uint8_t *)&raw, 4);
        printf("  ip_read duty=%lu\n", (unsigned long)raw);

        ip_read(IP_PWM, PWM_ID_DUTY_PCT, (uint8_t *)&raw, 4);
        printf("  ip_read duty_pct raw=0x%08lX\n", (unsigned long)raw);
    }

    printf("\n=== PWM demo done ===\n");
}

int main(void)
{
    pwm_demo_init();
    pwm_demo_run();
    return 0;
}
