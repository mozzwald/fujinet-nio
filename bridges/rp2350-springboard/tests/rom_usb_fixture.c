/* Integration fixture: real store, framing and dispatcher; fake bounded flash
 * and physical isolation. This does not simulate RP2350 XIP/PIO hardware. */
#include "rom_wire.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *memory;static struct rom_store store;static struct rm_state state;
static bool reset_low,lost;static int budget=-1;
static bool safe(void *c){(void)c;return state.maintenance&&state.core_held&&reset_low&&!lost;}
static bool rd(void *c,uint32_t a,void *b,size_t n){(void)c;if(a>RS_FLASH_BYTES||n>RS_FLASH_BYTES-a)return false;memcpy(b,memory+a,n);return true;}
static bool step(void){assert(safe(NULL));if(!budget)return false;if(budget>0)--budget;return true;}
static bool er(void *c,uint32_t a){(void)c;assert(a>=RS_SLOT0&&a<RS_META0+8192&&!(a%4096));if(!step())return false;memset(memory+a,255,4096);return true;}
static bool pg(void *c,uint32_t a,const uint8_t *b){(void)c;assert(a>=RS_SLOT0&&a<RS_META0+8192&&!(a%256));bool ok=step();for(unsigned i=0;i<(ok?256u:15u);++i){assert((memory[a+i]&b[i])==b[i]);memory[a+i]&=b[i];}return ok;}
static struct rs_io io(void){return (struct rs_io){NULL,rd,er,pg,safe};}
static void load(void *c){(void)c;const struct rs_image *d=rs_active(&store);uint8_t image[RS_CAPACITY];bool valid=d&&rs_copy_active(&store,image,sizeof image);state.served_source=valid?rs_selected(&store):2;state.served_length=valid?d->length:512;state.served_crc32=valid?d->crc:0;}
static enum rs_error quiesce(void *c){(void)c;if(!reset_low)return RS_UNSAFE;lost=false;state.active=0;state.stopped=1;state.maintenance=1;state.core_held=1;rs_init(&store,io());load(NULL);return RS_OK;}
static enum rs_error resume(void *c){(void)c;if(!safe(NULL)||store.fault||store.upload<2)return RS_UNSAFE;state.maintenance=0;state.core_held=0;state.stopped=0;state.active=1;return RS_OK;}
static struct rm_state status(void *c){(void)c;return state;}
void rom_test_reset(int fresh){if(!memory)memory=malloc(RS_FLASH_BYTES);assert(memory);if(fresh)memset(memory,255,RS_FLASH_BYTES);budget=-1;reset_low=false;lost=false;state=(struct rm_state){.flash_bytes=RS_FLASH_BYTES,.active=1};rs_init(&store,io());load(NULL);}
void rom_test_bus(int low){if(!low&&state.maintenance)lost=true;reset_low=!!low;}
void rom_test_budget(int n){budget=n;}
void rom_test_corrupt(uint32_t a){assert(a<RS_FLASH_BYTES);memory[a]^=1;}
size_t rom_test_exchange(const uint8_t *data,size_t n,uint8_t *response){struct rm_frame f={0};int result=0;for(size_t i=0;i<n;++i)result=rm_feed(&f,data[i]);if(result!=1)return 0;return rm_dispatch(&store,(struct rm_backend){NULL,quiesce,resume,load,status},f.data,response);}
void rom_test_metadata(uint32_t field,uint32_t value){assert(store.latest<2&&field%4==0&&field<=24);uint8_t *p=memory+RS_META0+store.latest*RS_SECTOR;rm_put(p+field,value);rm_put(p+28,rs_crc(p,28));}
