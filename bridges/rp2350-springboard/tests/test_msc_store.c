#ifdef NDEBUG
#undef NDEBUG
#endif
#include "msc_image.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t flash[RS_FLASH_BYTES],snapshot[MS_END-MS_BASE];
static int fail_op=-1,ops;static size_t partial;
static bool safe(void *x){(void)x;return true;}
static bool rd(void *x,uint32_t a,void *b,size_t n){(void)x;if(a>sizeof flash||n>sizeof flash-a)return false;memcpy(b,flash+a,n);return true;}
static bool er(void *x,uint32_t a){(void)x;assert(a>=MS_BASE&&a<MS_END&&!(a%4096));bool fail=ops++==fail_op;memset(flash+a,255,fail?partial%4097:4096);return !fail;}
static bool pg(void *x,uint32_t a,const uint8_t *b){(void)x;assert(a>=MS_BASE&&a+256<=MS_END&&!(a%256));bool fail=ops++==fail_op;size_t n=fail?partial%257:256;for(size_t i=0;i<n;i++){assert((flash[a+i]&b[i])==b[i]);flash[a+i]&=b[i];}return !fail;}
static struct rs_io io(void){return (struct rs_io){NULL,rd,er,pg,safe};}
static struct ms_disk d,reboot;
static void blank(void){memset(flash,255,sizeof flash);ops=0;fail_op=-1;assert(ms_init(&d,io()));}
int main(void){
 uint8_t a[512],b[512],out[1024];memset(a,0x5a,512);memset(b,0xa5,512);
 blank();assert(ms_read(&d,0,out)&&out[510]==0x55&&out[450]==1);
 assert(ms_write(&d,100,a));assert(ms_read(&d,100,out)&&!memcmp(a,out,512));
 assert(ms_init(&reboot,io())&&ms_read(&reboot,100,out)&&!memcmp(a,out,512));
 assert(!ms_transfer(&d,false,MS_SECTORS,0,out,1));assert(!ms_transfer(&d,true,0,512,out,1));
 assert(!ms_transfer(&d,true,UINT32_MAX,UINT32_MAX,out,SIZE_MAX));
 assert(ms_transfer(&d,true,100,500,b,512));assert(ms_transfer(&d,false,100,500,out,512)&&!memcmp(out,b,512));
 assert(ms_read(&d,100,out)&&!memcmp(out,a,500));
 puts("PASS seed, readback, restart, bounds, partial/cross-sector RMW");
 /* Full media + wrapping allocation exercises obsolete erase and live ownership. */
 blank();for(unsigned l=0;l<MS_SECTORS;l++){memset(a,(int)(l%251+1),512);assert(ms_write(&d,l,a));}
 for(unsigned n=0;n<MS_BLOCKS+200;n++){memset(a,(int)(n%251+1),512);assert(ms_write(&d,n%MS_SECTORS,a));}
 assert(ms_init(&reboot,io()));
 for(unsigned l=0;l<MS_SECTORS;l++){assert(ms_read(&reboot,l,a)&&ms_read(&d,l,b)&&!memcmp(a,b,512));}
 assert(!memcmp(flash,(uint8_t[16]){[0 ... 15]=255},16));
 puts("PASS full disk, GC wrap, live-sector preservation, firmware protection");
 /* Fill/reuse obsolete blocks. Erase may tear leaving an obsolete header. */
 blank();memset(a,0x55,512);
 /* Identical writes are skipped: explicitly create many versions. */
 for(unsigned i=0;i<MS_BLOCKS;i++){a[0]=(uint8_t)i;assert(ms_write(&d,100,a));}
 memcpy(snapshot,flash+MS_BASE,sizeof snapshot);
 unsigned cuts=0;
 for(int op=0;op<4;op++)for(unsigned cut=0;cut<=4096;cut+=(cut<32?1:127)){
  memcpy(flash+MS_BASE,snapshot,sizeof snapshot);fail_op=-1;assert(ms_init(&d,io()));assert(ms_read(&d,100,a));
  memset(b,0xe7,512);ops=0;fail_op=op;partial=cut;assert(!ms_write(&d,100,b));
  fail_op=-1;assert(ms_init(&reboot,io()));assert(ms_read(&reboot,100,out));
  assert(!memcmp(out,a,512)||!memcmp(out,b,512));cuts++;
 }
 printf("PASS %u interrupted erase/program/commit cases\n",cuts);
 blank();assert(ms_write(&d,7,a));flash[MS_BASE+d.map[7]*4096]^=1;assert(!ms_init(&reboot,io())&&reboot.fault);
 blank();assert(ms_write(&d,7,a));
 uint8_t *h=flash+MS_BASE+d.map[7]*4096+512;
 h[8]=0xff;h[9]=0xff;h[10]=h[11]=0;uint32_t crc=rs_crc(h,28);
 for(unsigned k=0;k<4;k++)h[28+k]=(uint8_t)(crc>>(8*k));
 assert(!ms_init(&reboot,io())&&reboot.fault);
 blank();d.sequence=UINT64_MAX;assert(!ms_write(&d,7,a));
 puts("PASS committed corruption and invalid metadata fail closed; sequence exhaustion");
 return 0;
}
