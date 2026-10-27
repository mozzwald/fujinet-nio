#pragma once
#include "persistent_reply.h"
struct sb_async_engine {
    struct sb_persistent_totals totals;
    const volatile uint16_t *table;
};
_Static_assert(offsetof(struct sb_async_engine,table)==24,"async assembly ABI");
_Static_assert(sizeof(struct sb_async_engine)==28,"async engine size");
void sb_async_reply(struct sb_async_engine *,const uint16_t *,const struct sb_cpu_bus *);
