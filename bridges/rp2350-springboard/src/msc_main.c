/* MSC loader retained; isolated dual-CS fixed-read bus experiment. */
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
    pio_set_sm_mask_enabled(pio0,7,false);
    for(unsigned sm=0;sm<4;++sm) {
        pio_sm_clear_fifos(pio1,sm);pio_sm_restart(pio1,sm);
    }
    for(unsigned sm=0;sm<3;++sm) {
        pio_sm_clear_fifos(pio0,sm);pio_sm_restart(pio0,sm);
    }
    pio_sm_exec(pio1,0,APIO_SET_PINS(0));
}
static void start(void) {
    pio0->irq=pio1->irq=0xff;
    pio_sm_exec(pio1,0,APIO_SET_Y(10));
    pio_sm_exec(pio1,0,APIO_JMP(SB_PR_MAIN));
    pio_sm_exec(pio1,1,APIO_JMP(SB_PR_WATCH));
    pio_sm_exec(pio1,2,APIO_JMP(SB_PR_OE));
    pio_sm_exec(pio1,3,APIO_JMP(SB_PR_RESET));
    pio_sm_exec(pio0,0,APIO_JMP(0));pio_sm_exec(pio0,1,APIO_JMP(3));
    pio_sm_exec(pio0,2,APIO_JMP(3));
    pio_enable_sm_mask_in_sync(pio0,7);
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
#include "msc_image.h"
#include "hardware/flash.h"
#include "hardware/irq.h"
#include "hardware/structs/scb.h"
#include "pico/unique_id.h"
#include "tusb.h"
#include <string.h>
_Static_assert(PICO_FLASH_SIZE_BYTES==RS_FLASH_BYTES,"16MiB flash required");
extern const char __rom_slot0,__rom_slot1,__rom_metadata0,__rom_metadata1,__rom_storage_end,__flash_binary_end,__msc_start,__msc_end;
static struct ms_disk disk;
static struct rom_store storage;
static uint8_t import_buffer[MI_FILE_CAP];
static bool flash_size_ok,ram_island_ok,boot_phase=true,ejected;
static volatile bool exception_fault;
static uint32_t served_source=RS_RECOVERY,served_crc,served_version,import_result=MI_ABSENT;
static uint32_t flash_operations,max_flash_us;
/* This path and all its callees/literals must be SRAM-resident. No printf,
 * locks, USB or flash reads. PIO release remains CPU-independent. */
__attribute__((noreturn)) void sb_msc_fault_park(void){
 __asm volatile("cpsid i":::"memory");
 clamp();exception_fault=true;__dmb();for(;;)__wfe();
}
void isr_hardfault(void) __attribute__((noreturn,alias("sb_msc_fault_park")));
void isr_nmi(void) __attribute__((noreturn,alias("sb_msc_fault_park")));
static bool internal(uintptr_t a){return a>=0x20000000u&&a<0x20082000u;}
static void install_ram_exceptions(void){
 /* Keep installed RAM IRQ handlers, replace every default XIP target. Core1
  * inherits this VTOR. Reset vector is used only after hardware restart. */
 volatile uintptr_t *v=(volatile uintptr_t *)scb_hw->vtor;
 if(!internal((uintptr_t)v))return;
 for(unsigned i=2;i<16+NUM_IRQS;i++)if(!internal(v[i]))v[i]=(uintptr_t)sb_msc_fault_park;
 __dmb();__isb();
 ram_island_ok=true;
 for(unsigned i=2;i<16+NUM_IRQS;i++)if(!internal(v[i]))ram_island_ok=false;
}
/* IRQ APIs identify an unclaimed vector by the SDK's exact default address.
 * RAM repair intentionally replaced that address. Temporarily restore only the
 * USB slot with the public SDK API, while Core0 IRQs are masked and flash writes
 * are disabled. Core1 keeps IRQs masked throughout service. TinyUSB then owns a
 * real SRAM handler chain; repair/audit completes before Core0 IRQ restore. */
static bool __attribute__((noinline)) initialize_usb(void){
 uint32_t irq=save_and_disable_interrupts();
 ram_island_ok=false;
 irq_remove_handler(USBCTRL_IRQ,sb_msc_fault_park);
 bool initialized=tusb_init();
 install_ram_exceptions();
 bool ok=initialized&&ram_island_ok;
 restore_interrupts(irq);
 return ok;
}
static bool safe(void *ctx){(void)ctx;return flash_size_ok&&ram_island_ok&&!exception_fault&&get_core_num()==0;}
static bool range(uint32_t a,size_t n){
 bool staging=a>=MS_BASE&&a<MS_END&&n<=MS_END-a;
 bool private=boot_phase&&a>=RS_SLOT0&&a<RS_META0+2*RS_SECTOR&&n<=RS_META0+2*RS_SECTOR-a;
 return staging||private;
}
static bool rd(void *ctx,uint32_t a,void *b,size_t n){
 (void)ctx;if(a>RS_FLASH_BYTES||n>RS_FLASH_BYTES-a)return false;
 memcpy(b,(const void *)(XIP_BASE+a),n);return true;
}
static void elapsed(uint64_t before){uint64_t d=time_us_64()-before;if(d>max_flash_us)max_flash_us=d>UINT32_MAX?UINT32_MAX:(uint32_t)d;flash_operations++;}
static bool er(void *ctx,uint32_t a){
 if(!safe(ctx)||a%RS_SECTOR||!range(a,RS_SECTOR))return false;
 uint64_t before=time_us_64();uint32_t irq=save_and_disable_interrupts();
 /* Core1 never touches XIP: intentionally no multicore lockout. */
 flash_range_erase(a,RS_SECTOR);restore_interrupts(irq);elapsed(before);return safe(ctx);
}
static bool pg(void *ctx,uint32_t a,const uint8_t *b){
 if(!safe(ctx)||a%RS_PAGE||!range(a,RS_PAGE)||!internal((uintptr_t)b)||!internal((uintptr_t)b+RS_PAGE-1))return false;
 uint64_t before=time_us_64();uint32_t irq=save_and_disable_interrupts();
 flash_range_program(a,b,RS_PAGE);restore_interrupts(irq);elapsed(before);return safe(ctx);
}
static struct rs_io io(void){return (struct rs_io){NULL,rd,er,pg,safe};}
static void load_active(void){
 const struct rs_image *i=rs_active(&storage);rom_ready=false;
 served_source=RS_RECOVERY;served_version=0;served_crc=SB_ROM_IMAGE_CRC;image_length=SB_ROM_IMAGE_SIZE;
 if(i&&rs_copy_active(&storage,bytes,sizeof bytes)){
  served_source=rs_selected(&storage);served_version=i->version;served_crc=i->crc;image_length=i->length;rom_ready=true;
 }else rom_ready=sb_load_rom(bytes,sizeof bytes,sb_rom_image,image_length,served_crc);
 if(rom_ready){memset(bytes+image_length,255,sizeof bytes-image_length);for(unsigned i=0;i<SB_ROM_CAPACITY/2;i++)words[i]=sb_rom_word(bytes,image_length,i*2);}
}
/* CDC is read-only diagnostics. Neither DTR, baud rate, a text command nor SCSI
 * eject can change the live ROM, reboot the RP or reset the Prism. */
static char line[32],out[512];static unsigned used,out_used,out_at;static bool discard;
static void status(void){
 out_used=(unsigned)snprintf(out,sizeof out,"{\"product\":\"FujiNet ROM Drive v1\",\"served_slot\":%lu,\"served_version\":%lu,\"served_length\":%lu,\"served_crc32\":\"%08lx\",\"import_result\":%lu,\"disk_fault\":%u,\"ram_island\":%u,\"flash_size_ok\":%u,\"exception_fault\":%u,\"active\":%u,\"owner_fault\":%u,\"writes\":%lu,\"sequence\":%llu,\"flash_ops\":%lu,\"max_flash_us\":%lu,\"ejected\":%u}\r\n",
 (unsigned long)served_source,(unsigned long)served_version,(unsigned long)image_length,(unsigned long)served_crc,(unsigned long)import_result,disk.fault,ram_island_ok,flash_size_ok,exception_fault,active,owner_fault,(unsigned long)disk.writes,(unsigned long long)disk.sequence,(unsigned long)flash_operations,(unsigned long)max_flash_us,ejected);
 if(out_used>=sizeof out)out_used=sizeof out-1;
 out_at=0;
}
static void console(void){
 if(!tud_cdc_connected()){used=out_used=out_at=0;discard=false;return;}
 if(out_at<out_used){out_at+=tud_cdc_write(out+out_at,out_used-out_at);tud_cdc_write_flush();return;}
 while(tud_cdc_available()){
  char c=(char)tud_cdc_read_char();
  if(c=='\r')continue;
  if(c=='\n'){
   line[used]=0;if(!discard&&(!strcmp(line,"status")||!strcmp(line,"help")))status();
   used=0;discard=false;if(out_used>out_at)break;
  }else if(c>=32&&c<=126&&used<sizeof line-1&&!discard)line[used++]=c;
  else discard=true;
 }
}
/* TinyUSB expects LUN count; it subtracts one for the USB GET_MAX_LUN reply. */
uint8_t tud_msc_get_maxlun_cb(void){return 1;}
void tud_msc_inquiry_cb(uint8_t lun,uint8_t vendor[8],uint8_t product[16],uint8_t revision[4]){
 (void)lun;memcpy(vendor,"FujiNet ",8);memcpy(product,"ROM staging disk",16);memcpy(revision,"1.00",4);
}
static bool media(uint8_t lun){
 if(lun||ejected){tud_msc_set_sense(lun,SCSI_SENSE_NOT_READY,0x3a,0);return false;}
 if(disk.fault||!safe(NULL)){tud_msc_set_sense(lun,SCSI_SENSE_MEDIUM_ERROR,0x11,0);return false;}
 return true;
}
bool tud_msc_test_unit_ready_cb(uint8_t lun){return media(lun);}
void tud_msc_capacity_cb(uint8_t lun,uint32_t *count,uint16_t *size){(void)lun;*count=MS_SECTORS;*size=512;}
bool tud_msc_is_writable_cb(uint8_t lun){return media(lun);}
int32_t tud_msc_read10_cb(uint8_t lun,uint32_t lba,uint32_t offset,void *buffer,uint32_t n){
 if(!media(lun))return -1;
 if(!ms_transfer(&disk,false,lba,offset,buffer,n)){tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x21,0);return -1;}return (int32_t)n;
}
int32_t tud_msc_write10_cb(uint8_t lun,uint32_t lba,uint32_t offset,uint8_t *buffer,uint32_t n){
 if(!media(lun))return -1;
 if(!ms_transfer(&disk,true,lba,offset,buffer,n)){tud_msc_set_sense(lun,disk.fault?SCSI_SENSE_MEDIUM_ERROR:SCSI_SENSE_ILLEGAL_REQUEST,disk.fault?0x0c:0x21,0);return -1;}return (int32_t)n;
}
bool tud_msc_start_stop_cb(uint8_t lun,uint8_t power,bool start,bool load_eject){
 (void)power;if(lun)return false;
 if(load_eject){if(start)ejected=false;else if(media(lun))ejected=true;else return false;}return true;
}
int32_t tud_msc_scsi_cb(uint8_t lun,const uint8_t cmd[16],void *buffer,uint16_t size){
 (void)buffer;(void)size;
 if(cmd[0]==0x35){/* No volatile write-back cache: all preceding writes durable. */return media(lun)?0:-1;}
 tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x20,0);return -1;
}
void tud_mount_cb(void){ejected=false;}
void tud_umount_cb(void){/* USB detach is not a filesystem handoff. */}
int main(void){
 const uint64_t mask=((UINT64_C(1)<<47)-1)&~(UINT64_C(1)<<39);
 gpio_init_mask64(mask);gpio_set_dir_in_masked64(mask);
 for(unsigned p=0;p<=46;p++)if(p!=39)gpio_disable_pulls(p);
 gpio_init(39);gpio_put(39,false);gpio_set_dir(39,GPIO_OUT);set_sys_clock_khz(150000,true);clamp();
 install_ram_exceptions();
 uint8_t tx[4]={0x9f,0,0,0},id[4];uint32_t irq=save_and_disable_interrupts();flash_do_cmd(tx,id,4);restore_interrupts(irq);
 flash_size_ok=id[1]!=0&&id[1]!=255&&id[3]==24&&
  (uintptr_t)&__rom_slot0==XIP_BASE+RS_SLOT0&&(uintptr_t)&__rom_slot1==XIP_BASE+RS_SLOT0+RS_STRIDE&&
  (uintptr_t)&__rom_metadata0==XIP_BASE+RS_META0&&(uintptr_t)&__rom_metadata1==XIP_BASE+RS_META0+RS_SECTOR&&
  (uintptr_t)&__rom_storage_end==XIP_BASE+RS_META0+2*RS_SECTOR&&
  (uintptr_t)&__msc_start==XIP_BASE+MS_BASE&&(uintptr_t)&__msc_end==XIP_BASE+MS_END&&
  (uintptr_t)&__flash_binary_end<=XIP_BASE+RS_FIRMWARE_LIMIT;
 if(flash_size_ok){
  rs_init(&storage,io());
  if(ms_init(&disk,io())){struct mi_image i;import_result=mi_load(&disk,import_buffer,&i);if(import_result==MI_OK)import_result=mi_promote(&storage,import_buffer,i);}
  else import_result=MI_MEDIA;
 }else{storage.latest=storage.upload=RS_RECOVERY;disk.fault=true;import_result=MI_MEDIA;}
 load_active();boot_phase=false;
 multicore_launch_core1(sb_boot_autoarm_entry);
 if(!initialize_usb())sb_msc_fault_park();
 for(;;){tud_task();console();tight_loop_contents();}
}
