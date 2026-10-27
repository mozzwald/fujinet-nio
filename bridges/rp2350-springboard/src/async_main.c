/* Separate Stage A candidate. Starts disarmed. Never installs/launches Palm code.
 * Core1 owns read RX/TX; Core0 owns write FIFOs and table publication.
 */
#include "async_reply.h"
#include "async_stage_a.h"
#include "async_program.h"
#include "bus_model.h"
#include "rom_image.h"
#include "async_build.h"
#include "async_boot.h"
#ifndef SB_ASYNC_BOOT_AUTOARM
#define SB_ASYNC_BOOT_AUTOARM 0
#endif
const char sb_async_build_id[]=SB_ASYNC_BUILD_ID;
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
static uint16_t words[SB_ROM_CAPACITY/2],table[VMB_TABLE_BYTES/2];
static struct sb_stage_a worker;
static struct sb_async_engine engine;
static volatile bool ready,arm,stop,active,stopped=true;
static bool rom_ok;
static uint32_t runs;
static void clamp(void) {
 pio1->irq_force=1;
 for(unsigned p=20;p<=35;++p)gpio_set_oeover(p,GPIO_OVERRIDE_LOW);
}
static void park(void) {
 clamp();
 for(unsigned b=0;b<3;++b){PIO p=pio_get_instance(b);pio_set_sm_mask_enabled(p,15,false);
  for(unsigned sm=0;sm<4;++sm){pio_sm_clear_fifos(p,sm);pio_sm_restart(p,sm);}}
 pio_sm_exec(pio1,0,APIO_MOV_PINDIRS_NULL);
}
static bool idle(void) {
 return (sio_hw->gpio_in&0xf0000u)==0xf0000u && (sio_hw->gpio_hi_in&(1u<<13));
}
static void core1(void) {
 (void)save_and_disable_interrupts();
 const struct sb_cpu_bus bus={&pio1->ctrl,&sio_hw->gpio_in,&stop};
 ready=true;
 for(;;){while(!arm)tight_loop_contents();
  stopped=false;__dmb();
  engine=(struct sb_async_engine){.table=table};
  sb_async_program_init(false);park();
  while(!stop&&!idle())tight_loop_contents();
  if(!stop){pio0->irq=pio1->irq=pio2->irq=0xff;
   for(unsigned pin=20;pin<=35;++pin)gpio_set_oeover(pin,GPIO_OVERRIDE_NORMAL);
   for(unsigned b=0;b<3;++b)pio_enable_sm_mask_in_sync(pio_get_instance(b),15);
   __dmb();active=true;arm=false;sb_async_reply(&engine,words,&bus);
  }
  park();active=false;arm=false;__dmb();stopped=true;
 }
}
static uint32_t faults(void) {
 return ((pio0->fdebug&4u)||(pio2->fdebug&6u)?SB_CS1_FAULT_FIFO:0)|
        (pio2->irq&32u?SB_CS1_FAULT_CONTROL:0);
}
static void writes(void) {
 uint32_t f=faults();
 if(f){sb_stage_a_fault(&worker,VMB_ERR_CAPTURE);return;}
 if(worker.capture.fault || (table[VMB_STATUS/2]&VMB_ST_FAULT))return;
 if(pio_sm_is_rx_fifo_empty(pio0,2)||pio_sm_is_rx_fifo_empty(pio2,1)||
    pio_sm_get_rx_fifo_level(pio2,2)<2)return;
 uint32_t low=pio_sm_get(pio0,2),high=pio_sm_get(pio2,1);
 uint32_t epoch=pio_sm_get(pio2,2),outcome=pio_sm_get(pio2,2);
 /* Epoch/outcome are latched hardware history; current GPIO is not validation. */
 (void)sb_stage_a_retire(&worker,low,high,epoch,outcome,faults());
}
int main(void) {
 const uint64_t mask=((UINT64_C(1)<<47)-1)&~(UINT64_C(1)<<39);
 gpio_init_mask64(mask);gpio_set_dir_in_masked64(mask);
 for(unsigned p=0;p<=46;++p)if(p!=39)gpio_disable_pulls(p);
 gpio_init(39);gpio_put(39,false);gpio_set_dir(39,GPIO_OUT);
 set_sys_clock_khz(150000,true);clamp();
 rom_ok=sb_load_rom(bytes,sizeof(bytes),sb_rom_image,SB_ROM_IMAGE_SIZE,SB_ROM_IMAGE_CRC);
 for(unsigned i=0;i<SB_ROM_CAPACITY/2;++i)words[i]=sb_rom_word(bytes,SB_ROM_IMAGE_SIZE,2*i);
 sb_stage_a_init(&worker,table);multicore_launch_core1(core1);stdio_usb_init();
 bool boot_requested=false;
 for(;;){
#if SB_ASYNC_BOOT_AUTOARM
  if(!boot_requested && sb_async_boot_request(&boot_requested,rom_ok,ready,stopped,arm,active,worker.capture.fault!=0)){
   stop=false;++runs;__dmb();arm=true;
  }
#else
  (void)boot_requested;
#endif
  if(active&&!stop)writes();
  int c=getchar_timeout_us(0);
  /* Debug replies are explicit and nonblocking. Never log individual accesses. */
  if(c=='q')printf("CS1_STAGE_A abi=%04x words=%u crc=%08lx clock_khz=%lu starts_disarmed=%u marker=none boot_autoarm=%u\n",VMB_ABI_VERSION,SB_ROM_IMAGE_SIZE/2,(unsigned long)SB_ROM_IMAGE_CRC,(unsigned long)(clock_get_hz(clk_sys)/1000),!SB_ASYNC_BOOT_AUTOARM,SB_ASYNC_BOOT_AUTOARM);
  if(c=='i')printf("BUILD %s\n",sb_async_build_id);
  if(c=='s')printf("active=%u stopped=%u arm_pending=%u run=%lu accepted=%lu capture_fault=%lu status=%04x ack=%04x counters_final=%u\n",active,stopped,arm,(unsigned long)runs,(unsigned long)worker.accepted,(unsigned long)worker.capture.fault,table[VMB_STATUS/2],table[VMB_PROBE_ACK/2],stopped);
  if(c=='g')printf("G f0=%08lx i1=%08lx f2=%08lx i2=%08lx\n",(unsigned long)pio0->fdebug,(unsigned long)pio1->irq,(unsigned long)pio2->fdebug,(unsigned long)pio2->irq);
  if(c=='r')printf("final=%u requests=%lu posted=%lu completed=%lu\n",stopped,(unsigned long)engine.totals.requests,(unsigned long)engine.totals.posted,(unsigned long)engine.totals.completed);
  if(c=='o')printf("final=%u enabled=%lu cancelled=%lu read_fault=%lu\n",stopped,(unsigned long)engine.totals.attempted,(unsigned long)engine.totals.cancelled,(unsigned long)engine.totals.fault);
  if(c=='b'&&rom_ok&&ready&&stopped&&!arm){boot_requested=true;sb_stage_a_init(&worker,table);stop=false;++runs;__dmb();arm=true;puts("arm requested");}
  if(c=='x'){boot_requested=true;stop=true;__dmb();clamp();puts("stop requested");}
  if(!active)sleep_us(50);
 }
}
