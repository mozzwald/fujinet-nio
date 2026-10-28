/* Read-only event instrumentation around the existing strict PIO fixture.
 * No change to its safety checks, pin sampling, owner tags or CPU behavior. */
#define sb_cosim_step sb_preparation_original_step
#include "persistent_cosim.c"
#undef sb_cosim_step
int sb_cosim_step(unsigned clocks) {
    while(clocks--) {
        bool before=sb_cosim_driven();
        int result=sb_preparation_original_step(1);
        if(before && !sb_cosim_driven())event(7,s.loadedowner,0);
        if(result)return result;
    }
    return 0;
}
uint32_t sb_preparation_instruction(unsigned block,unsigned pc) {
    return epio_get_instr(s.e,block,pc);
}
