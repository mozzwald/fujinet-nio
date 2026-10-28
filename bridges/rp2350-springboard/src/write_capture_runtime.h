#pragma once
#include <stdbool.h>
#include <stddef.h>
/* Called only for explicit CDC capture commands, never per bus transaction.
 * Format one JSON response in the existing console's bounded output buffer. */
bool sb_write_capture_console(const char *command,char *out,size_t n,bool bus_ready);
