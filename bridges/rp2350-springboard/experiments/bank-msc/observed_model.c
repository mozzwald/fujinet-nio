#include <assert.h>
#define sb_cs1_co_step sb_bank_original_step
#include "readonly_model.c"
#undef sb_cs1_co_step
static uint32_t publication[256];
void sb_cs1_co_step(unsigned clocks) {
    while(clocks--) {
        unsigned pc=co.e->block[1].sm[0].pc;
        unsigned before=epio_rx_fifo_depth(co.e,1,0);
        sb_bank_original_step(1);
        if(pc==SB_CS1_READ_SNAPSHOT && epio_rx_fifo_depth(co.e,1,0)>before) {
            assert(co.snapshot_owner<256);publication[co.snapshot_owner]=co.t;
        }
    }
}
uint32_t sb_bank_publication_clock(unsigned owner) {assert(owner<256);return publication[owner];}
