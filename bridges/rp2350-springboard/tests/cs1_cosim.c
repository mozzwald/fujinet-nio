/* Reuse the same strict PIO safety harness, with its C CPU disabled. The
 * separately emulated compiled ARM code owns every FIFO read/write instead.
 */
#define main sb_cs1_native_test_main
#include "test_cs1_program.c"
#undef main
static struct sim co;
void sb_cs1_co_init(void) {
    if(co.e) epio_free(co.e);
    init(&co,0); co.stall=true;
}
void sb_cs1_co_bus(uint64_t controls,unsigned address,unsigned expected_scratch) {
    co.regs.scratch=(uint16_t)expected_scratch;
    bus(&co,controls,address,0);
}
void sb_cs1_co_step(unsigned clocks) { step(&co,clocks); }
uint32_t sb_cs1_co_clock(void) { return co.t; }
uint32_t sb_cs1_co_gpio(void) { return (uint32_t)co.pins; }
uint32_t sb_cs1_co_enables(void) { return co.enables; }
uint32_t sb_cs1_co_driven(void) { return driven(&co); }
uint32_t sb_cs1_co_reg(unsigned block,unsigned offset) {
    if(offset==4) {
        uint32_t fstat=0;
        for(unsigned sm=0;sm<4;++sm) {
            if(!epio_rx_fifo_depth(co.e,block,sm)) fstat|=1u<<(8+sm);
            if(epio_tx_fifo_depth(co.e,block,sm)==4) fstat|=1u<<(16+sm);
        }
        return fstat;
    }
    if(offset==8) return co.fdebug[block];
    if(offset==0xc) {
        uint32_t levels=0;
        for(unsigned sm=0;sm<4;++sm)
            levels |= (uint32_t)epio_tx_fifo_depth(co.e,block,sm)<<(8*sm) |
                      (uint32_t)epio_rx_fifo_depth(co.e,block,sm)<<(8*sm+4);
        return levels;
    }
    if(offset==0x16c) {
        uint32_t intr=co.e->block[block].irq.irq<<8;
        for(unsigned sm=0;sm<4;++sm) {
            if(epio_rx_fifo_depth(co.e,block,sm)) intr|=1u<<sm;
            if(epio_tx_fifo_depth(co.e,block,sm)<4) intr|=1u<<(4+sm);
        }
        return intr;
    }
    if(offset==0x30) return co.e->block[block].irq.irq;
    fprintf(stderr,"unexpected MMIO register %u/%x\n",block,offset); exit(2);
}
uint64_t sb_cs1_co_rx(unsigned block,unsigned sm) {
    struct sim *s=&co;
    CHECK(epio_rx_fifo_depth(co.e,block,sm)>0);
    uint32_t value=epio_pop_rx_fifo(co.e,block,sm);
    if(block==1 && sm==0) {
        struct tag q=pop(&co,co.tags[co.head%16].kind);
        return ((uint64_t)((q.owner<<2)|(q.kind+1))<<32)|value;
    }
    return value;
}
void sb_cs1_co_tx(uint32_t value,unsigned owner) {
    struct sim *s=&co;
    CHECK(epio_tx_fifo_depth(co.e,1,0)==0);
    co.tx_owner=owner; epio_push_tx_fifo(co.e,1,0,value);
}
void sb_cs1_co_cancel(void) { epio_set_block_irq(co.e,1,0); }

void sb_cs1_co_irq_force(uint32_t flags) {
    for(unsigned bit=0;bit<8;++bit) if(flags&(1u<<bit)) epio_set_block_irq(co.e,1,bit);
}
void sb_cs1_co_irq_clear(uint32_t flags) {
    for(unsigned bit=0;bit<8;++bit) if(flags&(1u<<bit)) epio_clear_block_irq(co.e,1,bit);
}
/* Test-only observations/injection; never linked into device firmware. */
uint32_t sb_cs1_co_owner(void) { return co.owner; }
uint32_t sb_cs1_co_selected(void) { return co.selected; }
void sb_cs1_co_inject(unsigned kind) {
    switch(kind) {
    case 0: co.fdebug[0] |= 4; break;
    case 1: co.fdebug[2] |= 2; break;
    case 2: co.fdebug[2] |= 4; break;
    case 3: epio_set_block_irq(co.e,2,5); break;
    default: abort();
    }
}
