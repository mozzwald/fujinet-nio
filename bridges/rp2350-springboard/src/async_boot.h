#pragma once
#include <stdbool.h>
/* One boot attempt; explicit stop/fault never triggers an automatic retry. */
bool sb_async_boot_request(bool *requested,bool rom_ok,bool ready,bool stopped,
                           bool pending,bool active,bool fault);
