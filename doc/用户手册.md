# 嵌入式内存参数管理器 — 用户手册

## 1. 概述

嵌入式内存参数管理器是一个面向 FPGA软核如Xilinx Microblaze、ARM Cortex-M1 等资源受限平台的 **OOP-in-C 参数管理框架**。它提供统一的参数读写、范围校验、持久化存储、硬件同步和命令执行能力，将"参数"从散落的全局变量提升为一等公民。

### 核心特性

| 特性 | 说明 |
|------|------|
| 零动态分配 | 所有参数条目、模块实例均在编译期静态定义 |
| vtable 多态 | Applet 参数和 IP 参数通过虚函数表统一操作 |
| 类型安全 | 7 种参数类型 (UINT/INT/FLOAT/BOOL/ENUM/BLOB/STRING)，写入自动校验 |
| 范围裁剪 | 运行时设置/修改参数范围，超限值自动裁剪 |
| 脏数据追踪 | 双轨 dirty 标记 (Applet 简单标记 / IP 64-bit 位图) |
| 持久化 | 可插拔存储后端 (FlashDB / LittleFS / 裸 NAND)，支持 ctx 多实例 |
| 多分区切换 | 运行时替换存储驱动，支持 A/B Bank 固件升级 |
| EXEC 命令 | 参数化命令注册，dump 可见，与参数 ID 空间统一 |
| 线程安全 | LOCK/UNLOCK 机制支持裸机 (关中断) 和 RTOS (互斥锁) |
| 统计诊断 | 运行时的模块数/参数数/dirty/flush 错误等统计 |

---

## 2. 设计思想

### 2.1 为什么用 vtable 多态

传统方法用 `switch(e->type)` 分支处理不同类型的参数读写。当类型超过 5 种、操作超过 6 种时，switch-case 爆炸且不易扩展。

本框架为每种参数类型提供一套完整的操作函数（pre_write / cache_update / read / save / load / reset），编译成编译器常量分派表 `g_type_handlers[PARAM_TYPE_COUNT]`。新增类型只需在分派表追加一行，**不改一行业务逻辑**。

### 2.2 为什么 Applet 和 IP 分层

| | Applet 参数 | IP 参数 |
|------|------------|--------|
| 面向 | 业务逻辑 (自动曝光、PID 参数...) | 硬件寄存器 (传感器曝光、FPS...) |
| 写入路径 | 缓存 → apply 校验 → 标记 dirty | 缓存 → driver 回调 → 直通/缓存 |
| 校验 | 范围/枚举/apply 三层 | driver 内部处理 |
| flush | 模块级统一回调 | 按 dirty_map 位图逐条写硬件 |

两者共用同一根链表和同一套公开 API (`param_write` / `param_read` / `param_flush`)，通过 **模块级 vtable** 区分 flush/deinit/init 行为。

### 2.3 EXEC 命令：参数化注册，即插即用

EXEC 命令通过 `PARAM_FLAG_EXEC` 标志位注册为"伪参数"，享有与普通参数相同的 ID 空间和 dump 可见性。框架在 `param_write_raw` / `param_write_immediate` / `param_exec` 三条路径中自动检测 EXEC 标志，统一路由到模块的 `exec_cb` 回调。

**用户无需判断参数类型** — 串口协议解析器只需调 `param_write_raw(id, data, len)`，框架自动分发。

### 2.4 为什么手动注册而非自动扫描

嵌入式项目中参数定义分散在不同的 `.c` 文件，没有统一的反射/扫描机制。本框架通过 `PARAM_UINT(...)` 等宏在编译期静态定义，再通过 `xxx_module_init()` 或 `PARAM_MODULE_AUTO_REGISTER` (链接器段) 统一注册，既保证 ROM 优先，又具备灵活性。

---

## 3. 架构

```
┌────────────────────────────────────────────────────────┐
│                    Application Layer                   │
│   param_write()  param_read()  param_exec()  ...       │
└──────────────────────┬─────────────────────────────────┘
                       │  vtable 分派
┌──────────────────────┴─────────────────────────────────┐
│                   Core Framework                       │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ Hash Table  │  │ Module List  │  │ Stats Counter │  │
│  │ (256 slots) │  │ (ORDER sort) │  │               │  │
│  └──────┬──────┘  └──────┬───────┘  └───────────────┘  │
│         │                │                              │
│  ┌──────┴────────────────┴──────────────────────────┐  │
│  │              param_entry (8B base)               │  │
│  │  param_id + vtable pointer                       │  │
│  └──────────────────────┬───────────────────────────┘  │
└─────────────────────────┼──────────────────────────────┘
                          │
        ┌─────────────────┼─────────────────────┐
        ▼                 ▼                      ▼
┌──────────────┐  ┌──────────────┐  ┌──────────────────┐
│  Applet      │  │  IP          │  │  Storage Driver  │
│  vtable      │  │  vtable      │  │  (FlashDB etc.)  │
│              │  │              │  │                  │
│  7 type      │  │  driver      │  │  ctx+init/load   │
│  handlers    │  │  callbacks   │  │  /save/erase/    │
│              │  │  + exec_cb   │  │  deinit          │
└──────────────┘  └──────────────┘  └──────────────────┘
```

### 核心数据结构

| 结构体 | 大小 | 位置 | 说明 |
|--------|------|------|------|
| `param_entry` | 8B | .rodata | 参数基类: ID + vtable 指针 |
| `param_range_entry_t` | ~32B | .rodata | UINT/INT/FLOAT 条目 (含范围) |
| `param_enum_entry_t` | ~28B | .rodata | ENUM 条目 (含枚举值表) |
| `param_bool_entry_t` | ~20B | .rodata | BOOL 条目 |
| `param_blob_entry_t` | ~24B | .rodata | BLOB 条目 (含长度) |
| `param_string_entry_t` | ~24B | .rodata | STRING 条目 (含 max_len) |
| `ip_param_t` | ~20B | .rodata | IP 参数条目 (含 EXEC 支持) |
| `param_module_node` | ~36B | .data | 模块链表节点 (含 exec_cb) |
| `param_module` | ~48B | .data | Applet 模块 (含回调) |
| `ip_instance` | ~80B | .data | IP 实例 (含 64bit dirty_map) |
| `g_pm` | ~2.2KB | .bss | 全局单例 (含 256 槽哈希表) |

---

## 4. 接口参考

### 4.1 生命周期

```c
int  param_init(const param_storage_drv_t *storage);
void param_deinit(void);
void param_set_storage(const param_storage_drv_t *storage);
```

- `param_init`: 框架初始化，传入持久化驱动 (可为 NULL)。重复调用返回 `PARAM_ERR_BUSY`。
- `param_deinit`: 逆序释放所有模块和存储后端，清零全局状态。
- `param_set_storage`: 运行时替换持久化后端驱动。仅替换内部指针，不触发 deinit/init。常用于多分区 (A/B Bank) 切换。

### 4.2 参数读写

```c
int param_write(uint32_t param_id, param_value_t value);
int param_write_immediate(uint32_t param_id, param_value_t value);
int param_write_raw(uint32_t param_id, const uint8_t *data, uint16_t len);
int param_read(uint32_t param_id, param_value_t *value);
```

- `param_write`: 写入缓存，标记 dirty，待 flush 统一下发。
- `param_write_immediate`: 直通硬件/回调，不产生 dirty。
- `param_write_raw`: 原始字节流写入。**EXEC 参数自动路由到 exec_cb**，普通参数转为 `param_value_t` 写入缓存。
- `param_read`: 读取当前缓存值。

### 4.3 类型化包装

```c
int param_write_u32(uint32_t id, uint32_t val);
int param_write_i32(uint32_t id, int32_t val);
int param_write_f32(uint32_t id, float val);
int param_write_bool(uint32_t id, bool val);

int param_read_u32(uint32_t id, uint32_t *val);
int param_read_i32(uint32_t id, int32_t *val);
int param_read_f32(uint32_t id, float *val);
int param_read_bool(uint32_t id, bool *val);
```

封装 `param_write` / `param_read`，避免手动构造 `param_value_t`。

### 4.4 持久化

```c
int param_save_all(void);
int param_save_one(uint32_t param_id);
int param_load_all(void);
int param_load_one(uint32_t param_id);
```

- `param_load_all`: 两阶段执行 — (1) 从 Flash 恢复所有缓存值，(2) 按初始化顺序调用各模块 init 回调。
- `param_save_all`: 遍历哈希表保存所有 `PARAM_FLAG_PERSIST` 标记的参数。

### 4.5 Flush 和完整性

```c
int param_flush(void);
int param_check_flush_integrity(void);
```

- `param_flush`: 遍历模块链表，对所有 dirty 模块刷入硬件。单模块失败不中断后续模块。
- `param_check_flush_integrity`: 校验所有已注册模块都在 MODULE_INIT_ORDER 中，用于开发期检查。

### 4.6 Reset

```c
int param_reset_all(void);
int param_reset_one(uint32_t param_id);
```

重置为编译期定义的默认值 (default_val)，同时清除 dirty 标记。

### 4.7 运行时范围控制

```c
int param_set_range(uint32_t param_id,
                    const param_value_t *min_val,
                    const param_value_t *max_val);
void param_validate_all(void);
```

- `param_set_range`: 运行时修改参数的范围，仅对 Applet 的 UINT/INT/FLOAT 生效。`NULL` 表示不修改对应边界。
- `param_validate_all`: 对所有已注册参数执行范围裁剪。

### 4.8 EXEC 命令系统

```c
int param_exec(uint32_t cmd_id, void *arg);
```

EXEC 命令是"参数化注册的命令"。通过 `PARAM_IP_EXEC` / `PARAM_EXEC` 宏注册，
享有与普通参数相同的 32 位 ID 和 dump 可见性 (`f=E`)。

**路由机制：**

| 调用方式 | EXEC 参数行为 |
|----------|--------------|
| `param_write(id, v)` | 返回 `PARAM_ERR_READONLY` (被拦截) |
| `param_write_immediate(id, v)` | 自动路由到 `exec_cb(local_id, (void *)v.u32)` |
| `param_write_raw(id, data, len)` | 自动路由到 `exec_cb(local_id, (void *)data)` |
| `param_exec(cmd_id, arg)` | 校验已注册后调 `exec_cb(local_id, arg)` |

串口协议解析器只需调 `param_write_raw(id, data, len)`，框架自动判断是参数还是命令。

- `cmd_id`: 32 位命令 ID (MAKE_PARAM_ID 风格)
- `arg`: 命令参数，可为 NULL、整数值 `(void *)(uintptr_t)` 或结构体指针

### 4.9 参数标志位

| 标志 | 宏 | dump | 说明 |
|------|-----|------|------|
| `PARAM_FLAG_PERSIST` | `(1u << 0)` | `P` | 持久化到 Flash |
| `PARAM_FLAG_READONLY` | `(1u << 1)` | `R` | 只读，禁止写入 |
| `PARAM_FLAG_HIDDEN` | `(1u << 2)` | `H` | 对用户隐藏 |
| `PARAM_FLAG_DEPRECATED` | `(1u << 3)` | `D` | 已废弃 |
| `PARAM_FLAG_EXEC` | `(1u << 4)` | `E` | exec 命令 |

### 4.10 统计和遍历

```c
void param_get_stats(param_stats_t *stats);
void param_clear_stats(void);
void param_foreach(uint16_t module_id, param_foreach_fn cb, void *user_data);
```

### 4.11 存储后端多实例 (ctx)

`param_storage_drv_t` 包含 `void *ctx` 成员，所有回调的第一个参数为 `ctx`:

```c
typedef struct {
    void *ctx;
    int (*init)(void *ctx);
    int (*load)(void *ctx, uint32_t param_id, uint8_t *data, uint16_t len);
    int (*save)(void *ctx, uint32_t param_id, const uint8_t *data, uint16_t len);
    int (*erase_all)(void *ctx);
    int (*deinit)(void *ctx);
} param_storage_drv_t;
```

同一套回调函数可通过不同 `ctx` 支持多个物理存储实例 (如多 Flash 扇区、A/B Bank)。

### 4.12 多分区切换

```c
// 创建两个存储实例 (不同 Flash 扇区)
param_storage_drv_t g_bank0 = flashdb_create(&cfg_sectorA);
param_storage_drv_t g_bank1 = flashdb_create(&cfg_sectorB);

// 启动时选择活跃分区
uint8_t active = read_boot_flag();
param_init(&g_bank[active]);
param_load_all();

// 切换分区 (固件升级等场景)
param_save_all();
write_boot_flag(new_bank);
param_set_storage(&g_bank[new_bank]);
param_load_all();
```

### 4.13 状态码

| 常量 | 值 | 含义 |
|------|-----|------|
| PARAM_OK | 0 | 成功 |
| PARAM_ERR_INVALID_ID | -1 | 无效参数 ID |
| PARAM_ERR_OUT_OF_RANGE | -2 | 值超出范围 |
| PARAM_ERR_READONLY | -3 | 只读参数 |
| PARAM_ERR_TYPE_MISMATCH | -4 | 类型不匹配 |
| PARAM_ERR_NOT_FOUND | -5 | 模块/参数未找到 |
| PARAM_ERR_FLASH_FAIL | -6 | Flash 操作失败 |
| PARAM_ERR_ALREADY_REG | -7 | 重复注册 |
| PARAM_ERR_NO_MEMORY | -8 | 哈希表满 |
| PARAM_ERR_TIMEOUT | -9 | 操作超时 |
| PARAM_ERR_BUSY | -10 | 重复初始化 |

---

## 5. 适用场景

| 场景 | 契合度 |
|------|--------|
| ISP/图像传感器参数管理 (曝光、增益、FPS、分辨率) | ⭐⭐⭐⭐⭐ |
| 电机控制参数 (PID、速度、电流) | ⭐⭐⭐⭐⭐ |
| 电源管理参数 (电压、阈值、休眠定时) | ⭐⭐⭐⭐ |
| 通信协议参数 (波特率、超时、重传次数) | ⭐⭐⭐ |
| 算法配置参数 (权重、阈值、模式选择) | ⭐⭐⭐ |
| Bootloader 参数 (启动分区、固件版本号) | ⭐⭐⭐ |
| 单纯键值存储 (无范围/枚举校验需求) | ⭐⭐ (可以但浪费) |

---

## 6. 使用说明

### 6.1 定义模块 ID

在 `core/module_ids.h` 中追加 (0x01~0x7F 为建议的 Applet 区段，0x80~0xFF 为建议的 IP 区段):

```c
#define MODULE_PID_CTRL  0x02u  // Applet 模块 0x01~0x7F
#define IP_MOTOR         0x84u  // IP 模块 0x80~0xFF
```

并加入 MODULE_INIT_ORDER。

### 6.2 定义 Applet 参数

```c
// 在模块的 .c 文件中
#include "applet_param_manager.h"

PARAM_UINT(pid_kp, MAKE_PARAM_ID(MODULE_PID_CTRL, 0),
           PARAM_FLAG_PERSIST, 100, 0, 500);
PARAM_FLOAT(pid_kd, MAKE_PARAM_ID(MODULE_PID_CTRL, 1),
            PARAM_FLAG_PERSIST, 0.1f, 0.0f, 1.0f);

PARAM_TABLE(pid_params, &pid_kp.base, &pid_kd.base);
```

### 6.3 定义 Applet 模块

```c
static int pid_apply(uint32_t param_id, param_value_t value) {
    // 校验/转换逻辑
    return PARAM_OK;
}

static int pid_flush(void *ctx) {
    // 将缓存参数下发硬件
    return PARAM_OK;
}

PARAM_MODULE_DEFINE(pid_ctrl, MODULE_PID_CTRL, "PID_Control",
                    pid_flush, pid_apply);

void pid_ctrl_module_init(void) {
    pid_ctrl_module.init = pid_init_callback;  // 可选
    param_module_register(&pid_ctrl_module,
                          pid_params, PARAM_COUNT(pid_params));
}
```

### 6.4 定义 IP 参数和 Driver

```c
#include "ip_param_manager.h"

PARAM_IP_UINT(ip_motor_speed, MAKE_PARAM_ID(IP_MOTOR, 0),
              PARAM_FLAG_PERSIST, 1000);

PARAM_TABLE(motor_params, &ip_motor_speed.base);

static int motor_read(void *drv, uint16_t lid, param_value_t *v) {
    // 从硬件寄存器读
    return PARAM_OK;
}

static int motor_write(void *drv, uint16_t lid, param_value_t v) {
    // 写到硬件寄存器
    return PARAM_OK;
}

static int motor_init(void *drv) {
    // 初始化硬件
    return PARAM_OK;
}

IP_DRIVER_DEFINE(motor, IP_MOTOR, "Motor_IP",
                 &g_motor_dev, motor_read, motor_write);

void motor_module_init(void) {
    motor_instance.init_cb = motor_init;
    ip_driver_register(&motor_instance,
                       motor_params, PARAM_COUNT(motor_params));
}
```

### 6.5 定义 EXEC 命令

```c
// 命令 ID 枚举
enum { SHUTDOWN_CMD = 0, RESET_CMD = 1 };

// 注册 EXEC 参数 — dump 可见，f=E
PARAM_IP_EXEC(ip_sensor_shutdown, MAKE_PARAM_ID(IP_SENSOR, SHUTDOWN_CMD));
PARAM_IP_EXEC(ip_sensor_reset,    MAKE_PARAM_ID(IP_SENSOR, RESET_CMD));

// 加入参数表
PARAM_TABLE(sensor_params,
    &ip_sensor_exposure.base,
    &ip_sensor_shutdown.base,   // ← EXEC 命令和普通参数共存
    &ip_sensor_reset.base,
);

// exec 回调
static int sensor_exec(uint16_t local_id, void *arg) {
    switch (local_id) {
    case SHUTDOWN_CMD:
        sensor_device_shutdown(&g_sensor_dev);
        return PARAM_OK;
    case RESET_CMD:
        if (arg) { /* arg 为结构体指针 */ }
        sensor_device_reset(&g_sensor_dev);
        return PARAM_OK;
    }
    return PARAM_ERR_NOT_FOUND;
}

void sensor_module_init(void) {
    sensor_instance.node.exec_cb = sensor_exec;  // ← 注册 exec 回调
    ip_driver_register(&sensor_instance, sensor_params, PARAM_COUNT(sensor_params));
}

// 调用
param_exec(MAKE_PARAM_ID(IP_SENSOR, SHUTDOWN_CMD), NULL);
param_write_raw(MAKE_PARAM_ID(IP_SENSOR, RESET_CMD), raw_data, len);  // 也路由到 exec_cb
```

### 6.6 系统初始化

```c
void app_init(void) {
    // 1. 框架初始化
    param_init(param_storage_flashdb_get_driver());

    // 2. 注册所有模块
    pid_ctrl_module_init();
    motor_module_init();

    // 3. 从 Flash 加载参数 + 初始化模块
    param_load_all();

    // 4. 校验模块顺序覆盖
    param_check_flush_integrity();

    // 5. 运行时动态约束 (可选)
    param_value_t max_speed = { .u32 = 5000 };
    param_set_range(MAKE_PARAM_ID(IP_MOTOR, 0), NULL, &max_speed);
    param_validate_all();

    // 6. 首次写入硬件
    param_flush();
}
```

### 6.7 运行时操作

```c
// 读参数
param_value_t v;
param_read(MAKE_PARAM_ID(MODULE_PID_CTRL, 0), &v);
printf("kp = %u\n", v.u32);

// 写参数 (缓存模式)
v.u32 = 150;
param_write(MAKE_PARAM_ID(MODULE_PID_CTRL, 0), v);

// 刷入硬件
param_flush();

// 保存到 Flash
param_save_all();

// 执行命令
param_exec(MAKE_PARAM_ID(IP_SENSOR, SHUTDOWN_CMD), NULL);
```

### 6.8 dump 输出示例

```
[OV4689_Sensor_IP] id=0x0080 params=8 dirty=0
 [0000]ip_sensor_exposure    UINT=10000(0x2710)    d=0 f=P
 [0001]ip_sensor_gain        FLOAT=1.000           d=0 f=P
 [0002]ip_sensor_fps         UINT=30(0x001E)       d=0 f=P
 [0003]ip_sensor_resolution  INT=1                 d=0 f=P
 [0004]ip_sensor_shutdown    EXEC                  d=0 f=E
 [0005]ip_sensor_roi_x       UINT=1920(0x0780)     d=0 f=P
 [0006]ip_sensor_frame_cnt   UINT=0(0x0000)        d=0 f=R
 [0007]ip_sensor_cur_luma    UINT=0(0x0000)        d=0 f=R
```

flags 列: `P`=PERSIST, `R`=READONLY, `E`=EXEC, `-`=无标志, 多标志组合如 `PR`。

---

## 7. 注意事项

| 事项 | 说明 |
|------|------|
| **BLOB/STRING 缓冲区生命周期** | `cache.ptr` 和 `default_val.ptr` 必须指向静态或全局变量，框架不管理内存 |
| **哈希表大小** | 默认 256 槽，通过 `PARAM_HASH_SIZE` 宏调整，**必须为 2 的幂** |
| **IP 参数上限** | 每个 IP Driver 最多 64 个参数 (IP_DIRTY_MAP_BITS = 64) |
| **EXEC 命令必须注册** | `param_exec()` 会校验 cmd_id 已作为 PARAM_FLAG_EXEC 参数注册，未注册返回 PARAM_ERR_NOT_FOUND |
| **EXEC 参数不可 param_write** | `param_write` 遇到 EXEC 参数返回 PARAM_ERR_READONLY |
| **模块 ID 不可改** | 已在 Flash 中存储过的模块/参数 ID 不能修改，否则持久化数据丢失 |
| **init 回调中可读** | `param_load_all()` 的第二阶段 init 中已经可以调用 `param_read()` 读到恢复的缓存值 |
| **重复初始化** | 已初始化状态下再次调 `param_init()` 返回 `PARAM_ERR_BUSY` |
| **flush 失败** | 单模块 flush 失败不中断其他模块，错误累计在 `flush_error_count` |
| **ROM vs RAM** | 条目定义使用 `static` 宏，直接放入 .rodata 段，不占用 RAM |
| **DEBUG_NAME** | 定义 `PARAM_DEBUG_NAME` 会在每个条目中嵌入 `name` 字符串指针，增加 ROM 开销 |

---

## 8. 资源占用

### Core 代码规模

| 文件 | 行数 | Flash (ARM Thumb2 预估) |
|------|------|--------------------------|
| param_manager.c | ~790 | ~4.5 KB |
| applet_param_manager.c | ~580 | ~3.3 KB |
| ip_param_manager.c | ~390 | ~2.3 KB |
| param_dump.c | ~420 | ~3.3 KB |

> **Core 合计: ARM 目标 Flash 预估 ≤ 14 KB**

### RAM 占用

| 资源 | 大小 | 说明 |
|------|------|------|
| `g_pm` 全局单例 | ~2.1 KB | 含 256 槽哈希表 (每个 8B 指针 = 2KB) |
| 每 Applet 模块 | ~48B | 含链表节点 + 回调指针 |
| 每 IP 实例 | ~80B | 含链表节点 + driver 回调 + 64bit dirty_map |
| 每 Applet entry (ROM) | 20~32B | 根据类型不同 (保存在 .rodata) |
| 每 IP entry (ROM) | ~20B | 保存在 .rodata |

### 裁剪建议

- **不需要 dump** → 排除 `param_dump.c`，节省 ~3 KB Flash
- **不需要 DEBUG_NAME** → 注释掉 `param_manager.h` 的 `#define PARAM_DEBUG_NAME`，每个 entry 节省 4~8B
- **参数少** → 调小 `PARAM_HASH_SIZE`，每减半省 1 倍指针数组的 RAM

---

## 9. 移植指南

### 9.1 实现存储后端

实现 `param_storage_drv_t` 接口 (所有回调的第一个参数为 `ctx`):

```c
param_storage_drv_t g_my_storage = {
    .ctx       = &g_flash_handle,
    .init      = my_init,
    .load      = my_load,
    .save      = my_save,
    .erase_all = my_erase_all,
    .deinit    = my_deinit,
};
```

参考 `port/param_storage_flashdb.c`。

### 9.2 实现移植层

在 `port/param_manager_port.c` 中根据目标平台提供:

| 平台 | LOCK/UNLOCK | malloc/free |
|------|-------------|-------------|
| FreeRTOS | 互斥锁 | `pvPortMalloc` / `vPortFree` |
| 裸机 ARM | `__disable_irq()` / `__enable_irq()` (嵌套计数) | 静态内存池 |
| PC 测试 | 空实现 | `malloc` / `free` |

### 9.3 编译配置

```cmake
option(PARAM_DEBUG_NAME "Enable param debug names" OFF)
option(PARAM_MODULE_AUTO_REGISTER "Auto register via linker sections" OFF)
option(PARAM_MANAGER_NO_OS "Bare-metal without RTOS" ON)
option(USE_FLASHDB "Use FlashDB backend" OFF)
```

---

## 10. 编译和测试

### 编译示例程序

```bash
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --target example_demo
```

### 编译带测试的示例程序

```bash
cmake -S . -B build_test -DBUILD_TESTS=ON -G "MinGW Makefiles"
cmake --build build_test --target example_demo
./build_test/example_demo.exe    # 先跑 133 个测试，再跑 demo
```

### 运行独立单元测试

```bash
cmake -S . -B build_test -DBUILD_TESTS=ON
cmake --build build_test --target param_tests
./build_test/test/param_tests.exe
```

当前测试覆盖: **133 个用例全部通过**。

---

## 11. 许可

MIT License
