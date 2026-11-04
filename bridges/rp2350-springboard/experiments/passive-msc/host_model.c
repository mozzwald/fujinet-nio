/* Same full per-clock ROM projection with actual integrated observer hook. */
#define sb_rom_plus_cs1_probe_init __wrap_sb_pio_rearm_program_init
#define SB_WITH_CS1_PASSIVE 1
#include "../../tests/cs0_idle_cosim.c"
