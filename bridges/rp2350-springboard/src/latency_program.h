#pragma once
#include <stdbool.h>
/* Separate, manually armed diagnostic. Never a boot responder. */
enum sb_latency_response { SB_LAT_CPU_ROM, SB_LAT_CPU_ZERO, SB_LAT_CPU_ONES,
                           SB_LAT_PIO_ZERO, SB_LAT_PIO_ONES };
enum sb_latency_marker { SB_LAT_DRIVE, SB_LAT_REQUEST, SB_LAT_REPLY, SB_LAT_PRELOAD };
#define SB_LAT_WORDS 64u
/* Marker side-set changes no instructions or delay fields. */
bool sb_latency_side(unsigned pc, enum sb_latency_marker marker);
void sb_latency_program_init(bool enable, enum sb_latency_response response,
                             enum sb_latency_marker marker);
