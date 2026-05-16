cmake_minimum_required(VERSION 3.15)
project(embedded-memory-param-manager VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ================================================================
# 编译选项
# ================================================================
option(PARAM_DEBUG_NAME "Enable parameter debug name tracking" OFF)
option(PARAM_MODULE_AUTO_REGISTER "Enable automatic module registration via sections" OFF)
option(PARAM_MANAGER_NO_OS "Build for bare-metal without RTOS (ARM Cortex-M)" ON)
option(USE_FLASHDB "Enable FlashDB persistence backend (requires FlashDB library)" OFF)

# 检测 ARM 平台 (用于裸机编译)
if(CMAKE_C_COMPILER_ID MATCHES "GNU" AND CMAKE_C_COMPILER MATCHES "arm-none-eabi")
    set(TARGET_ARM_CORTEX_M TRUE)
endif()

# 对于非 ARM 平台，禁用裸机模式，使用默认的空实现
if(NOT TARGET_ARM_CORTEX_M)
    set(PARAM_MANAGER_NO_OS OFF)
endif()

if(PARAM_DEBUG_NAME)
    add_compile_definitions(PARAM_DEBUG_NAME)
endif()

if(PARAM_MODULE_AUTO_REGISTER)
    add_compile_definitions(PARAM_MODULE_AUTO_REGISTER)
endif()

if(PARAM_MANAGER_NO_OS)
    add_compile_definitions(PARAM_MANAGER_NO_OS)
endif()

# ================================================================
# 警告级别 (嵌入式友好)
# ================================================================
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
    # 启用严格 aliasing 检查以便发现问题
    # add_compile_options(-fstrict-aliasing -Wstrict-aliasing=1)
elseif(MSVC)
    add_compile_options(/W4 /WX-)
endif()

# ================================================================
# 头文件目录
# ================================================================
set(PARAM_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/port
    ${CMAKE_SOURCE_DIR}/src/init
    ${CMAKE_SOURCE_DIR}/src/modules/app/auto_exp
    ${CMAKE_SOURCE_DIR}/src/modules/ip/sensor
    ${CMAKE_SOURCE_DIR}/src/modules/ip/pwm
)

# ================================================================
# 源文件 — 核心框架 (必须)
# ================================================================
set(PARAM_CORE_SOURCES
    src/core/param_manager.c
    src/core/param_dump.c
    src/core/app_param_manager.c
    src/core/ip_param_manager.c
)

# ================================================================
# 源文件 — 移植层
# ================================================================
set(PARAM_PORT_SOURCES
    src/port/param_manager_port.c
)

# ================================================================
# 源文件 — 应用编排层
# ================================================================
set(PARAM_INIT_SOURCES
    examples/basic/app_frame_handler.c
)

# ================================================================
# 源文件 — 模块
# ================================================================
set(PARAM_MODULE_SOURCES
    src/modules/ip/sensor/sensor_module.c
    src/modules/app/auto_exp/auto_exp_control.c
    src/modules/app/auto_exp/ae_instance.c
)

# ================================================================
# FlashDB 支持
# ================================================================
if(USE_FLASHDB)
    add_compile_definitions(USE_FLASHDB)
    find_path(FLASHDB_INCLUDE_DIRS "flashdb.h" PATHS ${CMAKE_SOURCE_DIR}/../FlashDB)
    if(FLASHDB_INCLUDE_DIRS)
        include_directories(${FLASHDB_INCLUDE_DIRS})
        find_library(FLASHDB_LIB NAMES fdb PATHS ${CMAKE_SOURCE_DIR}/../FlashDB/lib)
        if(FLASHDB_LIB)
            set(FLASHDB_LIBRARIES fdb)
        endif()
    endif()

    if(NOT FLASHDB_INCLUDE_DIRS)
        message(WARNING "FlashDB not found. Please install FlashDB or disable USE_FLASHDB")
    endif()
endif()

# 始终包含 storage 源文件（条件编译处理有/无 FlashDB）
list(APPEND PARAM_PORT_SOURCES src/port/param_storage_flashdb.c)

# ================================================================
# 汇总源文件
# ================================================================
set(PARAM_ALL_SOURCES
    ${PARAM_CORE_SOURCES}
    ${PARAM_PORT_SOURCES}
    ${PARAM_INIT_SOURCES}
    ${PARAM_MODULE_SOURCES}
)

# ================================================================
# 静态库 — 核心功能
# ================================================================
add_library(param_manager_lib STATIC
    ${PARAM_CORE_SOURCES}
    ${PARAM_PORT_SOURCES}
    ${PARAM_INIT_SOURCES}
    ${PARAM_MODULE_SOURCES}
)

target_include_directories(param_manager_lib PUBLIC
    ${PARAM_INCLUDE_DIRS}
)

target_compile_definitions(param_manager_lib PUBLIC
    PARAM_HASH_SIZE=256
)

# ================================================================
# 示例程序 — basic demo (含测试运行)
# ================================================================
add_executable(demo_basic
    examples/basic/example_usage.c
    test/test_main.c
    test/test_common.c
    test/test_read_write.c
    test/test_boundary.c
    test/test_init_order.c
    test/test_error_handling.c
    test/test_load_save.c
    test/test_flush.c
    test/test_reset.c
    test/test_other.c
    test/test_multi_bank.c
    external/unity/unity.c
)

target_link_libraries(demo_basic PRIVATE param_manager_lib)
target_include_directories(demo_basic PRIVATE
    ${PARAM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/test
    ${CMAKE_SOURCE_DIR}/external/unity
)
target_compile_definitions(demo_basic PRIVATE PARAM_TEST_NO_MAIN)
target_compile_options(demo_basic PRIVATE -Wno-unused-parameter)

# ================================================================
# 示例程序 — pwm demo
# ================================================================
add_executable(demo_pwm
    examples/pwm/main.c
)

target_link_libraries(demo_pwm PRIVATE param_manager_lib)
target_include_directories(demo_pwm PRIVATE ${PARAM_INCLUDE_DIRS})

# ================================================================
# 单元测试
# ================================================================
option(BUILD_TESTS "Build unit tests" OFF)

add_library(unity STATIC
    external/unity/unity.c
)
target_include_directories(unity PUBLIC
    ${CMAKE_SOURCE_DIR}/external/unity
)

add_executable(param_tests
    test/test_main.c
    test/test_common.c
    test/test_read_write.c
    test/test_boundary.c
    test/test_init_order.c
    test/test_error_handling.c
    test/test_load_save.c
    test/test_flush.c
    test/test_reset.c
    test/test_other.c
    test/test_multi_bank.c
)
target_link_libraries(param_tests PRIVATE param_manager_lib unity)
target_include_directories(param_tests PRIVATE
    ${PARAM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/test
    ${CMAKE_SOURCE_DIR}/external/unity
)
target_compile_options(param_tests PRIVATE
    -Wno-unused-parameter
)

if(BUILD_TESTS)
    add_subdirectory(test)
endif()

# ================================================================
# 编译产物输出目录
# ================================================================
foreach(CONFIG_TYPE IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${CONFIG_TYPE}" CONFIG_TYPE_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER}
        ${CMAKE_SOURCE_DIR}/out/build/${CONFIG_TYPE})
endforeach()

# ================================================================
# 安装目标 (可选)
# ================================================================
install(TARGETS param_manager_lib demo_basic
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
)

install(DIRECTORY src/core src/port src/init src/modules
    DESTINATION include/param_manager
    FILES_MATCHING PATTERN "*.h"
)

# ================================================================
# 打印配置摘要
# ================================================================
message(STATUS "")
message(STATUS "=== ${PROJECT_NAME} Configuration ===")
message(STATUS "C Standard:        C${CMAKE_C_STANDARD}")
message(STATUS "PARAM_DEBUG_NAME:   ${PARAM_DEBUG_NAME}")
message(STATUS "PARAM_MODULE_AUTO_REGISTER: ${PARAM_MODULE_AUTO_REGISTER}")
message(STATUS "PARAM_MANAGER_NO_OS: ${PARAM_MANAGER_NO_OS}")
message(STATUS "USE_FLASHDB:       ${USE_FLASHDB}")
message(STATUS "FlashDB found:     ${FLASHDB_INCLUDE_DIRS}")
message(STATUS "==============================-")

