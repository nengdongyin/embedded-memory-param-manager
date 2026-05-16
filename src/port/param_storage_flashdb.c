#include "param_storage_flashdb.h"
#include <stdio.h>
#include <string.h>

#ifndef FDB_START_ADDR
#define FDB_START_ADDR   0x080E0000u
#endif
#ifndef FDB_SECTOR_SIZE
#define FDB_SECTOR_SIZE  0x40000u
#endif

#ifndef USE_FLASHDB

static int flashdb_stub_init(void *ctx) { (void)ctx; return 0; }
static int flashdb_stub_load(void *ctx, uint32_t id, uint8_t *d, uint16_t l) { (void)ctx; (void)id; (void)d; (void)l; return -1; }
static int flashdb_stub_save(void *ctx, uint32_t id, const uint8_t *d, uint16_t l) { (void)ctx; (void)id; (void)d; (void)l; return -1; }
static int flashdb_stub_erase_all(void *ctx) { (void)ctx; return 0; }
static int flashdb_stub_deinit(void *ctx) { (void)ctx; return 0; }

static param_storage_drv_t g_flashdb_drv = {
    .ctx       = NULL,
    .init      = flashdb_stub_init,
    .load      = flashdb_stub_load,
    .save      = flashdb_stub_save,
    .erase_all = flashdb_stub_erase_all,
    .deinit    = flashdb_stub_deinit,
};

const param_storage_drv_t *param_storage_flashdb_get_driver(void)
{
    return &g_flashdb_drv;
}

#else

#include "flashdb.h"

#define FDB_KVDB_NAME    "param_db"

static struct fdb_kvdb g_kvdb;
static bool g_kvdb_ready = false;

static struct fdb_default_kv g_default_kv_set[] = {
};

static int flashdb_init(void *ctx)
{
    (void)ctx;
    if (g_kvdb_ready) return 0;

    fdb_err_t result = fdb_kvdb_init(&g_kvdb,
                                     FDB_KVDB_NAME,
                                     "param_storage",
                                     g_default_kv_set,
                                     sizeof(g_default_kv_set) / sizeof(g_default_kv_set[0]));
    if (result != FDB_NO_ERR) {
        return -1;
    }

    g_kvdb_ready = true;
    return 0;
}

static int flashdb_load(void *ctx, uint32_t param_id, uint8_t *data, uint16_t len)
{
    (void)ctx;
    if (!g_kvdb_ready || !data || len == 0) return -1;

    char key[16];
    snprintf(key, sizeof(key), "p%lu", (unsigned long)param_id);

    size_t read_len = fdb_kv_get_blob(&g_kvdb, key, data, len);
    return (read_len > 0) ? 0 : -1;
}

static int flashdb_save(void *ctx, uint32_t param_id, const uint8_t *data, uint16_t len)
{
    (void)ctx;
    if (!g_kvdb_ready || !data || len == 0) return -1;

    char key[16];
    snprintf(key, sizeof(key), "p%lu", (unsigned long)param_id);

    fdb_err_t result = fdb_kv_set_blob(&g_kvdb, key, data, len);
    return (result == FDB_NO_ERR) ? 0 : -1;
}

static int flashdb_erase_all(void *ctx)
{
    (void)ctx;
    if (!g_kvdb_ready) return -1;

    fdb_kv_deinit(&g_kvdb);
    g_kvdb_ready = false;

    return flashdb_init();
}

static int flashdb_deinit(void *ctx)
{
    (void)ctx;
    if (g_kvdb_ready) {
        fdb_kv_deinit(&g_kvdb);
        g_kvdb_ready = false;
    }
    return 0;
}

static param_storage_drv_t g_flashdb_drv = {
    .ctx       = &g_kvdb,
    .init      = flashdb_init,
    .load      = flashdb_load,
    .save      = flashdb_save,
    .erase_all = flashdb_erase_all,
    .deinit    = flashdb_deinit,
};

const param_storage_drv_t *param_storage_flashdb_get_driver(void)
{
    return &g_flashdb_drv;
}

#endif