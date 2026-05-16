#include "auto_exp_control.h"
#include "sensor_module.h"
#include "param_manager.h"
#include <stdio.h>

void app_frame_handler(void)
{
    ae_frame_input_t  in;
    ae_frame_output_t out;
    param_value_t     v;

    param_read(PID_IP_SENSOR_CUR_LUMA, &v);
    in.current_luma = v.u32;

    param_read(PID_IP_SENSOR_EXPOSURE, &v);
    in.current_exp = v.u32;

    param_read(PID_IP_SENSOR_GAIN, &v);
    in.current_gain = v.f32;

    param_read(PID_IP_SENSOR_FPS, &v);
    in.current_fps = v.u32;

    ae_instance_run(&g_ae_instance, &in, &out);

    v.u32 = out.target_exposure;
    param_write(PID_IP_SENSOR_EXPOSURE, v);

    v.f32 = out.target_gain;
    param_write(PID_IP_SENSOR_GAIN, v);

    param_flush();
}

void app_frame_handler_demo(void)
{
    param_value_t v;

    printf("\n=== 帧处理演示: 快照 → AE算法 → 写回 ===\n");

    v.u32 = 100;
    param_write(PID_IP_SENSOR_CUR_LUMA, v);
    printf("  cur_luma=%u (偏暗, target=128)\n", v.u32);

    app_frame_handler();

    param_read(PID_IP_SENSOR_EXPOSURE, &v);
    printf("  → exposure=%u\n", v.u32);
    param_read(PID_IP_SENSOR_GAIN, &v);
    printf("  → gain=%.2f\n", v.f32);

    v.u32 = 200;
    param_write(PID_IP_SENSOR_CUR_LUMA, v);
    printf("  cur_luma=%u (偏亮)\n", v.u32);

    app_frame_handler();

    param_read(PID_IP_SENSOR_EXPOSURE, &v);
    printf("  → exposure=%u\n", v.u32);
    param_read(PID_IP_SENSOR_GAIN, &v);
    printf("  → gain=%.2f\n", v.f32);
}
