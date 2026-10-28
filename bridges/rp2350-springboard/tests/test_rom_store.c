#include "rom_store.h"
#include "rom_wire.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *flash_mem;static bool isolated=true;static int budget=-1;static unsigned mutations;
static bool read_mem(void *c,uint32_t a,void *b,size_t n){(void)c;if(a>RS_FLASH_BYTES||n>RS_FLASH_BYTES-a)return false;memcpy(b,flash_mem+a,n);return true;}
static bool step(void){assert(isolated);if(budget==0)return false;if(budget>0)--budget;++mutations;return true;}
static bool erase_mem(void *c,uint32_t a){(void)c;assert(a>=RS_SLOT0&&a<RS_META0+8192&&a%4096==0);if(!step())return false;memset(flash_mem+a,255,4096);return true;}
static bool program_mem(void *c,uint32_t a,const uint8_t *b){(void)c;assert(a>=RS_SLOT0&&a<=RS_META0+8192-256&&a%256==0);if(!step()){
 /* Power cut during page programming: partial1->0 writes. */
 for(unsigned i=0;i<15;++i){flash_mem[a+i]&=b[i];}
 return false;}
 for(unsigned i=0;i<256;++i){assert((flash_mem[a+i]&b[i])==b[i]);flash_mem[a+i]&=b[i];}return true;}
static bool gate(void *c){(void)c;return isolated;}
static struct rs_io io(void){return (struct rs_io){NULL,read_mem,erase_mem,program_mem,gate};}
static void upload(struct rom_store *s,unsigned slot,uint32_t version,const uint8_t *b,size_t n){
 assert(!rs_begin(s,slot,version,n,rs_crc(b,n),41));
 for(size_t p=0;p<n;p+=256){size_t m=n-p;if(m>256)m=256;assert(!rs_chunk(s,41,p,b+p,m));}
 assert(!rs_finish(s,41));assert(s->images[slot].valid);
}
int main(void){
 flash_mem=malloc(RS_FLASH_BYTES);assert(flash_mem);memset(flash_mem,255,RS_FLASH_BYTES);
 struct rom_store s;rs_init(&s,io());assert(!rs_active(&s));assert(rs_selected(&s)==2);
 uint8_t a[514],b[512],dest[RS_CAPACITY];for(unsigned i=0;i<sizeof a;++i)a[i]=(uint8_t)(i*37);memset(b,0x5a,sizeof b);
 unsigned m=mutations;isolated=false;assert(rs_begin(&s,0,1,512,0,1)==RS_UNSAFE);assert(rs_activate(&s,0)==RS_UNSAFE);assert(rs_rollback(&s)==RS_UNSAFE);assert(m==mutations);isolated=true;
 assert(rs_begin(&s,0,0,512,0,1)==RS_RANGE);assert(rs_begin(&s,0,1,65538,0,1)==RS_RANGE);assert(rs_begin(&s,0,1,513,0,1)==RS_RANGE);assert(rs_begin(&s,2,1,512,0,1)==RS_RANGE);assert(m==mutations);
 upload(&s,0,7,a,sizeof a);assert(rs_selected(&s)==2);assert(!rs_active(&s));assert(!rs_activate(&s,0));assert(rs_selected(&s)==0);assert(rs_copy_active(&s,dest,sizeof dest));assert(!memcmp(a,dest,sizeof a));
 assert(rs_begin(&s,0,8,512,0,1)==RS_CONFLICT);
 /* Reboot at every incomplete page: old selected image remains exact. */
 for(unsigned pages=0;pages<2;++pages){assert(!rs_begin(&s,1,8,sizeof b,rs_crc(b,sizeof b),9));if(pages)assert(!rs_chunk(&s,9,0,b,256));rs_init(&s,io());assert(rs_selected(&s)==0);assert(!s.images[1].valid);assert(rs_chunk(&s,9,256,b,256)==RS_STATE);assert(rs_copy_active(&s,dest,sizeof dest));assert(!memcmp(a,dest,sizeof a));}
 assert(!rs_begin(&s,1,8,sizeof b,123,9));assert(rs_chunk(&s,8,0,b,256)==RS_STATE);assert(rs_chunk(&s,9,256,b,256)==RS_STATE);assert(!rs_chunk(&s,9,0,b,256));assert(rs_chunk(&s,9,0,b,256)==RS_STATE);assert(!rs_chunk(&s,9,256,b+256,256));assert(rs_finish(&s,9)==RS_CRC);rs_init(&s,io());assert(!s.images[1].valid);assert(rs_selected(&s)==0);
 /* Cut at every erase/page/header publication boundary of a full upload. */
 uint8_t *snapshot=malloc(0x24000);assert(snapshot);memcpy(snapshot,flash_mem+RS_SLOT0,0x24000);
 for(int cut=0;cut<=20;++cut){
  memcpy(flash_mem+RS_SLOT0,snapshot,0x24000);rs_init(&s,io());budget=cut;
  enum rs_error e=rs_begin(&s,1,8,512,rs_crc(b,512),13);
  if(!e)e=rs_chunk(&s,13,0,b,256);
  if(!e)e=rs_chunk(&s,13,256,b+256,256);
  if(!e)e=rs_finish(&s,13);
  budget=-1;rs_init(&s,io());assert(rs_selected(&s)==0);assert(rs_copy_active(&s,dest,sizeof dest));assert(!memcmp(a,dest,sizeof a));assert(!!s.images[1].valid==(e==RS_OK));
 }
 memcpy(flash_mem+RS_SLOT0,snapshot,0x24000);free(snapshot);rs_init(&s,io());
 upload(&s,1,8,b,sizeof b);
 /* Metadata erase/program cut: reset chooses old complete selection. */
 for(int cut=0;cut<2;++cut){budget=cut;assert(rs_activate(&s,1)==RS_IO);budget=-1;rs_init(&s,io());assert(rs_selected(&s)==0);assert(rs_copy_active(&s,dest,sizeof dest));assert(!memcmp(a,dest,sizeof a));}
 assert(!rs_activate(&s,1));rs_init(&s,io());assert(rs_selected(&s)==1);assert(rs_copy_active(&s,dest,sizeof dest));assert(!memcmp(b,dest,sizeof b));assert(!rs_rollback(&s));assert(rs_selected(&s)==0);assert(!rs_rollback(&s));assert(rs_selected(&s)==1);
 /* CRC-invalid selected slot falls back; never silently picks another ROM. */
 flash_mem[RS_SLOT0+RS_STRIDE+RS_SECTOR]^=1;rs_init(&s,io());assert(rs_selected(&s)==1);assert(!rs_active(&s));assert(!rs_copy_active(&s,dest,sizeof dest));assert(!rs_activate(&s,2));assert(rs_selected(&s)==2);rs_init(&s,io());assert(rs_selected(&s)==2);
 /* Wrong slot schema/header checksum cannot publish an image. */
 flash_mem[RS_SLOT0+4]^=2;rs_init(&s,io());assert(!s.images[0].valid);assert(rs_verify(&s,0)==RS_CRC);
 /* Full-size boundary and flash failure leave firmware region untouched. */
 memset(dest,0xc3,sizeof dest);upload(&s,0,99,dest,sizeof dest);assert(!rs_activate(&s,0));assert(!rs_activate(&s,2));
 budget=18;assert(!rs_begin(&s,1,100,512,rs_crc(b,512),2));assert(!rs_chunk(&s,2,0,b,256));assert(rs_chunk(&s,2,256,b+256,256)==RS_IO);budget=-1;rs_init(&s,io());assert(!s.images[1].valid);
 for(unsigned i=0;i<RS_SLOT0;++i)assert(flash_mem[i]==255);
 /* Framing resynchronizes, checks length/version/CRC and ignores ASCII x. */
 uint8_t frame[528];size_t n=rm_encode(frame,3,0x1234,b,256);struct rm_frame f={0};assert(!rm_feed(&f,'x'));int result=0;for(size_t i=0;i<n;++i)result=rm_feed(&f,frame[i]);assert(result==1);frame[20]^=1;for(size_t i=0;i<n;++i)result=rm_feed(&f,frame[i]);assert(result==-1);frame[4]=2;for(size_t i=0;i<16;++i)result=rm_feed(&f,frame[i]);assert(result==-1);assert(rm_encode(frame,1,0,b,513)==0);
 free(flash_mem);puts("ROM store PASS: power cuts, bad CRC/schema, bounds, isolation, nonce/order, activation, rollback, recovery, framing");return 0;
}
