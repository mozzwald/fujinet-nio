/* Isolated msc-003 initialization hook. Baseline source/PIO/kernel untouched.
 * Observer input code/configuration is the previously tested 14-word program.
 * PIO2 FIFO/IRQ are local, untrusted telemetry, never consumed by ARM. */
#include <stdbool.h>
#ifndef APIO_EMULATION
#include "hardware/pio.h"
#endif
#define sb_pio_rearm_program_init __real_sb_pio_rearm_program_init
#define sb_rom_plus_cs1_probe_init sb_passive_config
#include "../../tests/cs1_passive_probe.c"
#undef sb_pio_rearm_program_init
#undef sb_rom_plus_cs1_probe_init
void __wrap_sb_pio_rearm_program_init(bool enable) {
#ifndef APIO_EMULATION
    /* Quiesce only PIO2 before writing its program, including on manual rearm.
     * Do not touch ROM PIO0/PIO1, GPIO pads, CPU NVIC or baseline stop policy. */
    pio_set_sm_mask_enabled(pio2,15,false);
    pio2->inte0=0;pio2->inte1=0;pio2->irq=0xff;
    pio_sm_clear_fifos(pio2,0);pio_sm_restart(pio2,0);
#endif
    sb_passive_config(enable);
#ifndef APIO_EMULATION
    /* Input-only observer starts during disarmed configuration. The original
     * park/idle/start functions and their ROM enable order remain unchanged. */
    pio_set_sm_mask_enabled(pio2,1,true);
#endif
}
