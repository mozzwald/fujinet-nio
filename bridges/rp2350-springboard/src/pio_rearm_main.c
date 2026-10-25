/* PIO rearm candidate. Boot disarmed; no automatic recognition or launch. */
#include "bus_model.h"
#include "pio_rearm_program.h"
#include "continuous_console.h"
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
static volatile bool rom_ready, ready, arm, stop, active, owner_fault, stopped=true;
static volatile unsigned mode;
static volatile uint32_t runs, requests, posted, completed, attempted, cancelled, empty_ends;
/* Core1 owns the alternating RX request/completion stream and TX publisher.
 * Core0 only requests stop and asserts persistent pad OE overrides. */
static void clamp(void) {
    pio1->irq_force=1; /* wake autonomous main release even while CPU1 stalls */
    for(unsigned pin=20;pin<=35;++pin)gpio_set_oeover(pin,GPIO_OVERRIDE_LOW);
}
static void park(void) {
    pio_sm_set_enabled(pio1,0,false);
    pio_sm_exec(pio1,0,APIO_MOV_PINDIRS_NULL);
    pio_set_sm_mask_enabled(pio1,15,false);
    pio_set_sm_mask_enabled(pio0,3,false);
    for(unsigned sm=0;sm<4;++sm) {
        pio_sm_clear_fifos(pio1,sm);pio_sm_restart(pio1,sm);
    }
    for(unsigned sm=0;sm<2;++sm) {
        pio_sm_clear_fifos(pio0,sm);pio_sm_restart(pio0,sm);
    }
    pio_sm_exec(pio1,0,APIO_SET_PINS(0));
}
static void start(void) {
    pio0->irq=pio1->irq=0xff;
    pio_sm_exec(pio1,0,APIO_SET_Y(10));
    pio_sm_exec(pio1,0,APIO_JMP(SB_PR_MAIN));
    pio_sm_exec(pio1,1,APIO_JMP(SB_PR_WATCH));
    pio_sm_exec(pio1,2,APIO_JMP(SB_PR_WATCH));
    pio_sm_exec(pio1,3,APIO_JMP(SB_PR_RESET));
    pio_sm_exec(pio0,0,APIO_JMP(0));pio_sm_exec(pio0,1,APIO_JMP(0));
    pio_enable_sm_mask_in_sync(pio0,3);
    pio_enable_sm_mask_in_sync(pio1,15);
}
static void service(void) {
    (void)save_and_disable_interrupts();
    const struct sb_cpu_bus bus={&pio1->ctrl,&sio_hw->gpio_in,&stop};
    struct sb_cpu_request request;
    ready=true;
    for(;;) {
        while(!arm)tight_loop_contents();
        requests=posted=completed=attempted=cancelled=empty_ends=0;
        for(unsigned i=0;i<SB_ROM_CAPACITY/2;++i)
            words[i]=mode==1?0:mode==2?0xffff:sb_rom_word(bytes,SB_ROM_IMAGE_SIZE,i*2);
        sb_pio_rearm_program_init(false);park();
        for(unsigned pin=20;pin<=35;++pin)gpio_set_oeover(pin,GPIO_OVERRIDE_NORMAL);
        ++runs;stopped=false;__dmb();arm=false;
        while(!stop && ((sio_hw->gpio_in & ((1u<<16)|(1u<<18))) != ((1u<<16)|(1u<<18)) ||
                       !(sio_hw->gpio_hi_in & (1u<<13))))tight_loop_contents();
        if(!stop){start();__dmb();active=true;}
        uint32_t nr=0,np=0,nc=0,ne=0,nd=0;
        while(!stop) {
            /* Unchanged ARM instructions. SRAM stop byte is not the PIO
             * cancellation flag: the PIO owns that flag across a late reply. */
            if(!sb_rx_first_reply(&request,words,&bus))break;
            ++nr;
            if(request.valid)++np;
            else {
                /* The kernel intentionally publishes no invalid snapshot.
                 * Close that one outstanding transaction with a cancelled
                 * dummy; no future request can consume this reply. */
                pio1->irq_force=1;
                pio1->txf[0]=0;
            }
            while(pio_sm_is_rx_fifo_empty(pio1,0) && !stop)tight_loop_contents();
            if(stop)break;
            uint32_t token=pio_sm_get(pio1,0);
            if(token!=0 && token!=UINT32_MAX){owner_fault=true;ready=false;break;}
            ++nc;
            if(token)++ne;else ++nd;
            /* No IRQ acknowledgment, PC write, divider reset or FIFO flush.
             * PIO may already have queued the next request behind this token. */
        }
        /* Publish exact totals only on stop. Keeping diagnostics local avoids
         * shared-SRAM read/modify/write traffic between paired replies. */
        requests=nr;posted=np;completed=nc;attempted=ne;cancelled=nd;
        park();active=false;__dmb();stopped=true;
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
    bool fault_reported=false;
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
        if(owner_fault&&!fault_reported){puts("owner completion framing fault; reboot required");fault_reported=true;}
        int c=getchar_timeout_us(0);
        if(c=='q') printf("CONTINUOUS version=2 response=%s marker="
#if SB_PIO_REPLY_MARKER
            "preload"
#else
            "drive"
#endif
            " continuous=1 words=%u crc=%08lx clock_khz=%lu\r\n",
            mode==1?"cpu-zero":mode==2?"cpu-ones":"cpu-rom",SB_ROM_IMAGE_SIZE/2,
            (unsigned long)SB_ROM_IMAGE_CRC,(unsigned long)(clock_get_hz(clk_sys)/1000));
        if(c=='s') {
            snapshot=(struct sb_continuous_status){
                .rom_ready=rom_ready,.ready=ready,.stopped=stopped,.arm_pending=arm,
                .active=active,.counters_final=true,.run=runs,.requests=requests,.posted=posted,
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
