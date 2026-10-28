#include "write_capture_runtime.h"
#include "write_capture.h"
#include "bus_model.h"
#include <apio.h>
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/sio.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>

/* Core0 owns all this state. No ISR, polling loop, Core1 scheduling, mailbox,
 * GPIO output, IRQ enable, dynamic allocation or write-side effect is added. */
static struct wc_frame saved;
static unsigned session,phase,result;
static uint64_t armed_at;
enum { DISARMED,ARMED,FINISHED };
static bool edge(unsigned pin) {
    return !!(io_bank0_hw->intr[pin/8] & (GPIO_IRQ_EDGE_FALL << ((pin%8)*4)));
}
static bool idle(void) {
    return (sio_hw->gpio_in & (15u<<16)) == (15u<<16) &&
           (sio_hw->gpio_hi_in & (1u<<13));
}
static void disable(void) {
    pio_sm_set_enabled(pio0,3,false);pio_set_sm_mask_enabled(pio2,15,false);
}
static bool arm_capture(void) {
    disable();pio_sm_clear_fifos(pio0,3);pio_sm_restart(pio0,3);
    for(unsigned sm=0;sm<4;++sm){pio_sm_clear_fifos(pio2,sm);pio_sm_restart(pio2,sm);}
    pio0->fdebug=(1u<<3)|(1u<<11)|(1u<<19)|(1u<<27);
    pio2->fdebug=0x0f0f0f0f;pio2->irq=255;
    /* Preserve baseline-owned PIO0 IRQ bits; only the unused local halt7 is
     * cleared. Nothing writes PIO1 IRQ, SM, FIFO, GPIOBASE or instruction RAM. */
    pio0->irq=1u<<7;
    sb_write_capture_program_init(false);
    gpio_acknowledge_irq(16,GPIO_IRQ_EDGE_FALL);
    gpio_acknowledge_irq(18,GPIO_IRQ_EDGE_FALL);
    gpio_acknowledge_irq(45,GPIO_IRQ_EDGE_FALL);
    /* Initialization is outside the live read path and may take time. Recheck
     * idle after it, before opening a window or replacing the saved record. */
    if(!idle())return false;
    memset(&saved,0,sizeof saved);result=WC_INCOMPLETE;
    __dmb();pio_sm_set_enabled(pio0,3,true);pio_enable_sm_mask_in_sync(pio2,15);
    armed_at=time_us_64();++session;phase=ARMED;return true;
}
static void finish_capture(bool ready) {
    disable();__dmb();
    saved.reset_edge=edge(45)||!(sio_hw->gpio_hi_in&(1u<<13));
    saved.cs0_edge=edge(16);saved.oe_edge=edge(18);
    saved.fifo_fault=!!((pio0->fdebug&((1u<<3)|(1u<<11)))||(pio2->fdebug&0x0f0f));
    saved.low_depth=pio_sm_get_rx_fifo_level(pio2,0);
    saved.high_depth=pio_sm_get_rx_fifo_level(pio0,3);
    saved.end_depth=pio_sm_get_rx_fifo_level(pio2,1);
    if(saved.low_depth==2){saved.low=pio_sm_get(pio2,0);saved.low_end=pio_sm_get(pio2,0);}
    if(saved.high_depth==2){saved.high=pio_sm_get(pio0,3);saved.high_end=pio_sm_get(pio0,3);}
    if(saved.end_depth==1)saved.outcome=pio_sm_get(pio2,1);
    /* Counter SM has no regular FIFO writes. Snapshot only after stopping it;
     * this cannot perturb the timing-critical read consumer or output owner. */
    pio_sm_clear_fifos(pio2,3);
    pio_sm_exec(pio2,3,APIO_MOV_ISR_X);pio_sm_exec(pio2,3,APIO_PUSH_BLOCK);
    if(pio_sm_get_rx_fifo_level(pio2,3)==1)saved.pulses=0u-pio_sm_get(pio2,3);
    else saved.fifo_fault=true;
    /* Each counted pulse needs at least five150MHz PIO clocks; one minute
     * stays below2^32. Expiry invalidates the record; no periodic timer/work. */
    result=!ready?WC_NOT_READY:time_us_64()-armed_at>60000000u?WC_EXPIRED:wc_validate(&saved);
    phase=FINISHED;
}
bool sb_write_capture_console(const char *command,char *out,size_t n,bool ready) {
    bool is_arm=!strcmp(command,"write-arm"),is_finish=!strcmp(command,"write-finish");
    if(!is_arm&&!is_finish&&strcmp(command,"write-status"))return false;
    const char *error="";
    if(is_arm){
        if(phase==ARMED)error="already-armed";
        else if(!ready||!idle())error="bus-not-ready";
        else if(!arm_capture())error="bus-not-ready";
    }else if(is_finish){
        if(phase!=ARMED)error="not-armed";
        else finish_capture(ready);
    }
    int count=snprintf(out,n,
        "{\"capture_abi\":1,\"session\":%u,\"phase\":%u,\"result\":%u,\"qualified\":%u,"
        "\"error\":\"%s\",\"pulses\":%lu,\"low\":%lu,\"low_end\":%lu,\"high\":%lu,\"high_end\":%lu,"
        "\"outcome\":%lu,\"low_depth\":%lu,\"high_depth\":%lu,\"end_depth\":%lu,"
        "\"reset_edge\":%u,\"cs0_edge\":%u,\"oe_edge\":%u,\"fifo_fault\":%u,\"address\":%lu,\"value\":%u}\r\n",
        session,phase,result,phase==FINISHED&&result==WC_OK,error,
        (unsigned long)saved.pulses,(unsigned long)saved.low,(unsigned long)saved.low_end,
        (unsigned long)saved.high,(unsigned long)saved.high_end,(unsigned long)saved.outcome,
        (unsigned long)saved.low_depth,(unsigned long)saved.high_depth,(unsigned long)saved.end_depth,
        saved.reset_edge,saved.cs0_edge,saved.oe_edge,saved.fifo_fault,
        (unsigned long)sb_address(saved.low,saved.high),(unsigned)((saved.high>>4)&0xffff));
    if(count<0||(size_t)count>=n){
        /* Never return a partial JSON success record. Capture remains stopped
         * or armed as recorded; report formatter failure explicitly. */
        snprintf(out,n,"{\"capture_abi\":1,\"error\":\"status-overflow\"}\r\n");
    }
    return true;
}
