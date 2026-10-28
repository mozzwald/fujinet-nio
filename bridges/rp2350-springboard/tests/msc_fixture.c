/* Native integration ABI for actual storage/parser, with injected NOR failures. */
#include "msc_image.h"
#include <string.h>
static uint8_t media[RS_FLASH_BYTES];
static struct ms_disk disk;
static struct rom_store store;
static int fail=-1,ops;static unsigned cut;
static bool safe(void *x){(void)x;return true;}
static bool rd(void *x,uint32_t a,void *b,size_t n){(void)x;if(a>sizeof media||n>sizeof media-a)return false;memcpy(b,media+a,n);return true;}
static bool er(void *x,uint32_t a){(void)x;if(a<MS_BASE||a%4096||a+4096>RS_META0+8192)return false;bool f=ops++==fail;memset(media+a,255,f?cut%4097:4096);return !f;}
static bool pg(void *x,uint32_t a,const uint8_t *b){(void)x;if(a<MS_BASE||a%256||a+256>RS_META0+8192)return false;bool f=ops++==fail;unsigned n=f?cut%257:256;for(unsigned i=0;i<n;i++)media[a+i]&=b[i];return !f;}
static struct rs_io io(void){return (struct rs_io){NULL,rd,er,pg,safe};}
void fixture_blank(void){memset(media,255,sizeof media);fail=-1;ops=0;ms_init(&disk,io());rs_init(&store,io());}
int fixture_restart(void){fail=-1;bool ok=ms_init(&disk,io());rs_init(&store,io());return ok;}
int fixture_write(unsigned lba,const uint8_t *b){return ms_write(&disk,lba,b);}
int fixture_read(unsigned lba,uint8_t *b){return ms_read(&disk,lba,b);}
int fixture_load(uint8_t *b,struct mi_image *i){return mi_load(&disk,b,i);}
int fixture_promote(const uint8_t *b,struct mi_image i){return mi_promote(&store,b,i);}
int fixture_active(uint8_t *b){return rs_copy_active(&store,b,65536);}
unsigned fixture_slot(void){return rs_selected(&store);}
unsigned fixture_generation(void){return store.latest<2?store.records[store.latest].generation:0;}
void fixture_fail(int op,unsigned bytes){ops=0;fail=op;cut=bytes;}
uint8_t *fixture_media(void){return media;}
void fixture_restore(const uint8_t *b){memcpy(media,b,sizeof media);fixture_restart();}
unsigned fixture_operations(void){return (unsigned)ops;}
