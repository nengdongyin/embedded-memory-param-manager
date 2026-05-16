cmake_minimum_required(VERSION 3.15)

set(TEST_TARGET param_tests)

set(TEST_SOURCES
    test_main.c
    test_common.c
    test_read_write.c
    test_boundary.c
    test_init_order.c
    test_error_handling.c
    test_load_save.c
    test_flush.c
    test_reset.c
    test_other.c
    test_multi_bank.c
    unity_core/unity.c
)

set(CORE_SOURCES
    ../core/param_manager.c
    ../core/applet_param_manager.c
    ../core/ip_param_manager.c
    ../core/param_dump.c
    ../port/param_manager_port.c
)

add_executable(${TEST_TARGET}
    ${TEST_SOURCES}
    ${CORE_SOURCES}
)

target_include_directories(${TEST_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/unity_core
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/port
    ${CMAKE_SOURCE_DIR}/modules/ip
    ${CMAKE_SOURCE_DIR}/modules/applet
    ${CMAKE_SOURCE_DIR}/algorithm
)

target_compile_definitions(${TEST_TARGET} PRIVATE
    PARAM_HASH_SIZE=256
    UNITY_EXCLUDE_SETJMP_H
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${TEST_TARGET} PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -fprofile-arcs -ftest-coverage
    )
endif()

target_link_options(${TEST_TARGET} PRIVATE
    -fprofile-arcs -ftest-coverage
)

set_target_properties(${TEST_TARGET} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

enable_testing()
add_test(NAME param_tests COMMAND ${TEST_TARGET})
