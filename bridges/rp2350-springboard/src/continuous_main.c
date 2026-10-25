/* Experimental continuous owner. Boot disarmed; no automatic recognition. */
#include "bus_model.h"
#include "continuous_program.h"
#include "continuous_console.h"
#include "continuous_rearm.h"
#include "cpu_reply.h"
#include "rom_image.h"
#include <apio.h>
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include <stdio.h>

static uint8_t bytes[SB_ROM_CAPACITY];
static uint16_t words[SB_ROM_CAPACITY/2];
static volatile bool rom_ready, ready, arm, stop, active, stopped=true;
static volatile unsigned mode;
static volatile uint32_t runs, requests, posted, completed, attempted, cancelled, empty_ends;
/* Only core1 publishes TX or clears FIFOs. Core0 may only force the sticky
 * pad overrides, SM3 clamp and IRQ7; it never acknowledges an epoch or publishes a reply. */
static void clamp(void) {
    /* Sticky pad override survives an owner racing through epoch restart.
     * Only explicit next arm clears it, with all SMs already stopped/Hi-Z. */
    for(unsigned pin=20;pin<=35;++pin)gpio_set_oeover(pin,GPIO_OVERRIDE_LOW);
    pio_sm_exec(pio1,3,APIO_JMP(SB_CONT_CLAMP));
    pio1->irq_force=1u<<7;
}
static void park(void) {
    /* Retain live guards until main has stopped and its direction latch is low. */
    pio_sm_set_enabled(pio1,0,false);
    pio_sm_exec(pio1,0,APIO_MOV_PINDIRS_NULL);
    pio_set_sm_mask_enabled(pio1,15,false);
    for(unsigned sm=0;sm<4;++sm) {
        pio_sm_exec(pio1,sm,APIO_MOV_PINDIRS_NULL);
        pio_sm_clear_fifos(pio1,sm);
        pio_sm_restart(pio1,sm);
    }
    pio_sm_exec(pio1,0,APIO_SET_PINS(0));
}
/* Caller has parked every SM and drained the previous epoch. */
static void epoch(void) {
    pio1->irq=0xff;
    pio_sm_exec(pio1,0,APIO_SET_Y(10));
    pio_sm_exec(pio1,0,APIO_JMP(SB_CONT_MAIN));
    pio_sm_exec(pio1,1,APIO_JMP(SB_CONT_WATCH));
    pio_sm_exec(pio1,2,APIO_JMP(SB_CONT_WATCH));
    pio_sm_exec(pio1,3,APIO_JMP(SB_CONT_RESET));
    pio_enable_sm_mask_in_sync(pio1,15);
}
/* Normal completion only: no FIFO writes/flushes and no divider changes.
 * Stop main and force release before freezing its live guards. Restart ALL
 * waiting states before clearing IRQ0: another guard may still be about to
 * publish the old ending. Clear only IRQ0/1, preserving concurrent host stop.
 * CLKDIV=1 on every SM, so enabling together needs no divider restart. */
static inline void fast_epoch(void) {
    hw_clear_bits(&pio1->ctrl,1u);
    pio_sm_exec(pio1,0,APIO_MOV_PINDIRS_NULL);
    hw_clear_bits(&pio1->ctrl,15u);
    pio_restart_sm_mask(pio1,15u);
    pio1->irq=3u;
    pio_sm_exec(pio1,0,APIO_JMP(SB_CONT_MAIN));
    pio_sm_exec(pio1,1,APIO_JMP(SB_CONT_WATCH));
    pio_sm_exec(pio1,2,APIO_JMP(SB_CONT_WATCH));
    pio_sm_exec(pio1,3,APIO_JMP(SB_CONT_RESET));
    hw_set_bits(&pio1->ctrl,15u);
}
static void service(void) {
    (void)save_and_disable_interrupts();
    ready=true;
    for(;;) {
        while(!arm) tight_loop_contents();
        requests=posted=completed=attempted=cancelled=empty_ends=0;
        for(unsigned i=0;i<SB_ROM_CAPACITY/2;++i)
            words[i]=mode==1?0:mode==2?0xffff:sb_rom_word(bytes,SB_ROM_IMAGE_SIZE,i*2);
        sb_continuous_program_init(false);
        park();
        for(unsigned pin=20;pin<=35;++pin)gpio_set_oeover(pin,GPIO_OVERRIDE_NORMAL);
        ++runs;
        /* Startup requires inactive CS0 and OE and released RESET. No inherited
         * selected cycle is accepted. Stop remains effective while waiting. */
        stopped=false;
        __dmb(); arm=false;
        while(!stop && ((sio_hw->gpio_in & ((1u<<16)|(1u<<18))) != ((1u<<16)|(1u<<18)) ||
                       !(sio_hw->gpio_hi_in & (1u<<13)))) tight_loop_contents();
        if(!stop) {epoch();__dmb();active=true;}
        while(!stop) {
            struct sb_cpu_request request;
            /* Same instruction bytes as baseline; empty-wait reads PIO IRQ
             * instead of SRAM. Physical latency must be requalified. */
            const struct sb_cpu_bus bus={&pio1->ctrl,&sio_hw->gpio_in,
                (const volatile bool *)&pio1->irq};
            bool have=sb_rx_first_reply(&request,words,&bus);
            while(!(pio1->irq & 0x81)) tight_loop_contents();
            uint32_t irq=pio1->irq;
            if((irq&0x80)&&!have)break; /* idle host stop is not a bus cycle */
            if(have) {++requests;if(request.valid)++posted;}
            else ++empty_ends;
            if(irq&2)++attempted;else ++cancelled;
            ++completed;
            if(!stop && (sio_hw->gpio_hi_in & (1u<<13)) &&
               sb_cont_fast_rearm_allowed(have,have && request.valid,irq,pio1->fstat)) {
                fast_epoch();
                continue;
            }
            /* This core has finished every possible publication. Drain old
             * replies before releasing any guard for the next epoch. */
            park();
            if(stop || (irq&0x80))break;
            /* RESET held low must not repeatedly create cancellation epochs. */
            while(!stop && !(sio_hw->gpio_hi_in & (1u<<13))) tight_loop_contents();
            if(!stop) epoch();
        }
        park();
        active=false;__dmb();stopped=true;
    }
}
int main(void) {
    const uint64_t mask=((UINT64_C(1)<<47)-1)&~(UINT64_C(1)<<39);
    gpio_init_mask64(mask);gpio_set_dir_in_masked64(mask);
    for(unsigned pin=0;pin<=46;++pin)if(pin!=39)gpio_disable_pulls(pin);
    gpio_init(39);gpio_put(39,false);gpio_set_dir(39,GPIO_OUT);
    set_sys_clock_khz(150000,true);
    rom_ready=sb_load_rom(bytes,sizeof(bytes),sb_rom_image,SB_ROM_IMAGE_SIZE,SB_ROM_IMAGE_CRC);
    multicore_launch_core1(service);stdio_usb_init();
    uint64_t next=0;
    unsigned status_page=3;
    struct sb_continuous_status snapshot={0};
    for(;;) {
        const uint64_t now=time_us_64();
        if(now<next){sleep_ms(1);continue;}
        next=now+10000; /* same10ms console cadence as bounded baseline */
        if(status_page<3) {
            char line[128];
            int length=sb_continuous_status_line(line,sizeof(line),status_page++,&snapshot);
            if(length>0 && (size_t)length<sizeof(line))fputs(line,stdout);
            sleep_ms(1);continue;
        }
        int c=getchar_timeout_us(0);
        if(c=='q') printf("CONTINUOUS version=1 response=%s marker=drive continuous=1 words=%u crc=%08lx clock_khz=%lu\r\n",
            mode==1?"cpu-zero":mode==2?"cpu-ones":"cpu-rom",SB_ROM_IMAGE_SIZE/2,
            (unsigned long)SB_ROM_IMAGE_CRC,(unsigned long)(clock_get_hz(clk_sys)/1000));
        if(c=='s') {
            snapshot=(struct sb_continuous_status){
                .rom_ready=rom_ready,.ready=ready,.stopped=stopped,.arm_pending=arm,
                .active=active,.run=runs,.requests=requests,.posted=posted,
                .completed=completed,.attempted=attempted,.cancelled=cancelled,.empty_ends=empty_ends};
            status_page=0;
        }
        if(c=='R'||c=='Z'||c=='F') {
            if(stopped&&!arm)mode=c=='Z'?1:c=='F'?2:0;
            else puts("config refused: require stopped");
        }
        if(c=='b') {
            if(rom_ready&&ready&&stopped&&!arm) {stop=false;__dmb();arm=true;puts("arm requested; wait for active status");}
            else puts("arm refused");
        }
        if(c=='x') {
            if(!arm) {stop=true;__dmb();if(!stopped)clamp();puts("stop requested; confirm stopped=1");}
            else puts("wait for arm_pending=0");
        }
        sleep_ms(1);
    }
}
