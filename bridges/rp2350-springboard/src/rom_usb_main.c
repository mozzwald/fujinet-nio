/* Separate USB ROM-storage candidate. PIO and ARM kernel sources unchanged. */
#include "bus_model.h"
#include "pio_rearm_program.h"
#include "continuous_console.h"
#include "cpu_reply.h"
#if SB_PERSISTENT_REPLY
#include "persistent_reply.h"
#endif
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

static uint32_t image_length;
static uint8_t bytes[SB_ROM_CAPACITY];
/* Preserve the frozen table's modulo16=8 SRAM bank alignment despite new
 * management code/data. Only the exposed65536-byte table is served. */
static uint8_t sb_rom_words_backing[SB_ROM_CAPACITY+16] __attribute__((aligned(16),used));
extern uint16_t words[SB_ROM_CAPACITY/2];
__asm__(".global words\n.set words,sb_rom_words_backing+8\n.size words,65536\n");
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
#if !SB_PERSISTENT_REPLY
    struct sb_cpu_request request;
#endif
    ready=true;
    for(;;) {
        while(!arm)tight_loop_contents();
        requests=posted=completed=attempted=cancelled=empty_ends=0;
        for(unsigned i=0;i<SB_ROM_CAPACITY/2;++i)
            words[i]=mode==1?0:mode==2?0xffff:sb_rom_word(bytes,image_length,i*2);
        sb_pio_rearm_program_init(false);park();
        for(unsigned pin=20;pin<=35;++pin)gpio_set_oeover(pin,GPIO_OVERRIDE_NORMAL);
        ++runs;stopped=false;__dmb();arm=false;
        while(!stop && ((sio_hw->gpio_in & ((1u<<16)|(1u<<18))) != ((1u<<16)|(1u<<18)) ||
                       !(sio_hw->gpio_hi_in & (1u<<13))))tight_loop_contents();
        if(!stop){start();__dmb();active=true;}
#if SB_PERSISTENT_REPLY
        struct sb_persistent_totals totals={0};
        if(!stop)sb_persistent_reply(&totals,words,&bus);
        if(totals.fault){owner_fault=true;ready=false;}
        requests=totals.requests;posted=totals.posted;completed=totals.completed;
        attempted=totals.attempted;cancelled=totals.cancelled;
#else
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
#endif
        park();active=false;__dmb();stopped=true;
    }
}
/* Read-only entry-shim metadata. The shim runs once before service(), requests
 * arm only after verified ROM load, and never re-enters after stop/fault. */
void sb_boot_autoarm_entry(void);
const struct {
    const volatile bool *rom_valid;
    volatile bool *arm_request;
    void (*service_entry)(void);
    uint32_t reserved;
} sb_boot_policy={&rom_ready,&arm,service,0};
/* Management code never runs on Core1 or during an individual bus reply. */
#include "rom_store.h"
#include "rom_wire.h"
#include "hardware/flash.h"
#include "hardware/structs/io_bank0.h"
#include "pico/stdio.h"
#include <string.h>
_Static_assert(PICO_FLASH_SIZE_BYTES==RS_FLASH_BYTES,"16MiB board required");
extern const char __rom_slot0,__rom_slot1,__rom_metadata0,__rom_metadata1,__rom_storage_end,__flash_binary_end;
static struct rom_store storage;
static bool maintenance,core_held,gate_lost,flash_size_ok;
static uint32_t served_source=RS_RECOVERY,served_crc,fallback_reason;
static struct rm_frame rx;
static uint8_t response[RM_HEADER+RM_MAX_PAYLOAD];
/* OE/WE may toggle for other selects. Only selected module cycles matter. */
static bool quiet(void){return !(sio_hw->gpio_hi_in&(1u<<13))&&(sio_hw->gpio_in&0x30000u)==0x30000u;}
static bool edge(unsigned pin,uint32_t events){return !!(io_bank0_hw->intr[pin/8]&(events<<(4*(pin%8))));}
static bool bus_safe(void *ctx){
 (void)ctx;
 if(!maintenance||!core_held||!flash_size_ok||gate_lost)return false;
 if(!quiet()||edge(16,GPIO_IRQ_EDGE_FALL)||edge(17,GPIO_IRQ_EDGE_FALL)||edge(45,GPIO_IRQ_EDGE_RISE)){gate_lost=true;return false;}
 return true;
}
static bool rd(void *ctx,uint32_t a,void *b,size_t n){(void)ctx;if(a>RS_FLASH_BYTES||n>RS_FLASH_BYTES-a)return false;memcpy(b,(const void *)(XIP_BASE+a),n);return true;}
static bool er(void *ctx,uint32_t a){
 if(a<RS_SLOT0||a>=RS_META0+2*RS_SECTOR||a%RS_SECTOR||!bus_safe(ctx))return false;
 uint32_t irq=save_and_disable_interrupts();flash_range_erase(a,RS_SECTOR);restore_interrupts(irq);return bus_safe(ctx);
}
static bool pg(void *ctx,uint32_t a,const uint8_t *b){
 if(a<RS_SLOT0||a>RS_META0+2*RS_SECTOR-RS_PAGE||a%RS_PAGE||!bus_safe(ctx))return false;
 uint32_t irq=save_and_disable_interrupts();flash_range_program(a,b,RS_PAGE);restore_interrupts(irq);return bus_safe(ctx);
}
static struct rs_io io(void){return (struct rs_io){NULL,rd,er,pg,bus_safe};}
static void load_active(void *ctx){
 (void)ctx;
 const struct rs_image *d=rs_active(&storage);
 served_source=RS_RECOVERY;fallback_reason=!flash_size_ok?4:storage.latest==2?1:rs_selected(&storage)==2?2:3;
 rom_ready=false;
 if(d&&rs_copy_active(&storage,bytes,sizeof bytes)){
  image_length=d->length;served_crc=d->crc;served_source=rs_selected(&storage);fallback_reason=0;
  memset(bytes+image_length,255,sizeof bytes-image_length);rom_ready=true;
 }else{
  image_length=SB_ROM_IMAGE_SIZE;served_crc=SB_ROM_IMAGE_CRC;
  rom_ready=sb_load_rom(bytes,sizeof bytes,sb_rom_image,image_length,served_crc);
 }
 if(rom_ready)for(unsigned i=0;i<SB_ROM_CAPACITY/2;++i)words[i]=sb_rom_word(bytes,image_length,2*i);
}
static enum rs_error quiesce(void *ctx){
 (void)ctx;
 if(!flash_size_ok||!quiet())return RS_UNSAFE;
 /* Edge flags latch activity even while CPU IRQs are disabled for flash. */
 gpio_acknowledge_irq(16,GPIO_IRQ_EDGE_FALL);gpio_acknowledge_irq(17,GPIO_IRQ_EDGE_FALL);gpio_acknowledge_irq(45,GPIO_IRQ_EDGE_RISE);
 for(unsigned i=0;i<100;++i){if(!quiet()||edge(16,GPIO_IRQ_EDGE_FALL)||edge(17,GPIO_IRQ_EDGE_FALL)||edge(45,GPIO_IRQ_EDGE_RISE))return RS_UNSAFE;sleep_us(10);}
 stop=true;__dmb();clamp();
 uint64_t deadline=time_us_64()+100000;
 while(!stopped){if(time_us_64()>deadline)return RS_STATE;tight_loop_contents();}
 /* No Core1 flash lockout IRQ is needed or assumed: stop acknowledged, then
  * hardware-reset Core1 into its boot ROM wait before any erase/program. */
 multicore_reset_core1();core_held=true;park();maintenance=true;gate_lost=false;
 if(!bus_safe(NULL))return RS_UNSAFE;
 rs_init(&storage,io());load_active(NULL);return bus_safe(NULL)?RS_OK:RS_UNSAFE;
}
static enum rs_error resume(void *ctx){
 (void)ctx;
 if(!bus_safe(NULL)||!rom_ready||owner_fault||storage.fault||storage.upload<2)return RS_UNSAFE;
 stop=false;arm=false;stopped=true;ready=false;__dmb();maintenance=false;core_held=false;
 multicore_launch_core1(sb_boot_autoarm_entry);return RS_OK;
}
static struct rm_state state(void *ctx){
 (void)ctx;return (struct rm_state){flash_size_ok?RS_FLASH_BYTES:0,active,stopped,maintenance&&!gate_lost,core_held,served_source,image_length,served_crc,fallback_reason,owner_fault};
}
static void command(void){
 size_t length=rm_dispatch(&storage,(struct rm_backend){NULL,quiesce,resume,load_active,state},rx.data,response);
 /* Complete bounded binary response; no text/debug parser shares this CDC. */
 stdio_put_string((const char *)response,(int)length,false,false);
}
int main(void){
 const uint64_t mask=((UINT64_C(1)<<47)-1)&~(UINT64_C(1)<<39);
 gpio_init_mask64(mask);gpio_set_dir_in_masked64(mask);
 for(unsigned pin=0;pin<=46;++pin)if(pin!=39)gpio_disable_pulls(pin);
 gpio_init(39);gpio_put(39,false);gpio_set_dir(39,GPIO_OUT);set_sys_clock_khz(150000,true);clamp();
 /* Before USB/Core1 launch, check actual JEDEC capacity. Unknown parts refuse
  * management writes and boot recovery without reading out-of-range slots. */
 uint8_t tx[4]={0x9f,0,0,0},id[4];uint32_t irq=save_and_disable_interrupts();
 flash_do_cmd(tx,id,sizeof tx);restore_interrupts(irq);
 flash_size_ok=id[1]!=0&&id[1]!=255&&id[3]==24&&
  (uintptr_t)&__rom_slot0==XIP_BASE+RS_SLOT0&&
  (uintptr_t)&__rom_slot1==XIP_BASE+RS_SLOT0+RS_STRIDE&&
  (uintptr_t)&__rom_metadata0==XIP_BASE+RS_META0&&
  (uintptr_t)&__rom_metadata1==XIP_BASE+RS_META0+RS_SECTOR&&
  (uintptr_t)&__rom_storage_end==XIP_BASE+RS_META0+2*RS_SECTOR&&
  (uintptr_t)&__flash_binary_end<=XIP_BASE+RS_FIRMWARE_LIMIT;
 if(flash_size_ok)rs_init(&storage,io());else {memset(&storage,0,sizeof storage);storage.io=io();storage.latest=2;storage.upload=2;}
 load_active(NULL);multicore_launch_core1(sb_boot_autoarm_entry);stdio_usb_init();
 uint64_t last=0;
 for(;;){if(maintenance)(void)bus_safe(NULL);int c=getchar_timeout_us(0);uint64_t now=time_us_64();
  if(rx.used&&now-last>1000000)rx.used=0;
  if(c>=0){last=now;if(rm_feed(&rx,(uint8_t)c)==1)command();}
  else sleep_us(50);
 }
}
