#include "msc_store.h"
#include <string.h>
#define MAGIC 0x31534d46u /* FMS1 */
#define VOLUME 0x46554a49u
static uint32_t le(const uint8_t *b){return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static void put(uint8_t *b,uint32_t n){for(unsigned i=0;i<4;i++)b[i]=(uint8_t)(n>>(8*i));}
static void p16(uint8_t *b,uint16_t n){b[0]=(uint8_t)n;b[1]=(uint8_t)(n>>8);}
/* Blank, formatted MBR/FAT12 volume synthesized only for never-written sectors.
 * This is not automatic formatting: overlays, including damage, survive reset. */
void ms_seed(uint32_t lba,uint8_t b[512]){
 memset(b,0,512);
 if(lba==0){b[450]=1;put(b+454,1);put(b+458,2047);b[510]=0x55;b[511]=0xaa;}
 if(lba==1){memcpy(b,"\xeb\x3c\x90MSDOS5.0",11);p16(b+11,512);b[13]=1;p16(b+14,1);b[16]=2;p16(b+17,64);p16(b+19,2047);b[21]=0xf8;p16(b+22,6);p16(b+24,32);p16(b+26,64);put(b+28,1);b[36]=0x80;b[38]=0x29;put(b+39,VOLUME);memcpy(b+43,"FUJINET ROM",11);memcpy(b+54,"FAT12   ",8);b[510]=0x55;b[511]=0xaa;}
 if(lba==2||lba==8){b[0]=0xf8;b[1]=b[2]=0xff;}
 if(lba==14){memcpy(b,"FUJINET ROM",11);b[11]=8;}
}
static bool read_at(struct ms_disk *d,uint32_t a,void *b,size_t n){if(!d->io.read(d->io.ctx,a,b,n)){d->fault=true;return false;}return true;}
static bool record(struct ms_disk *d,uint32_t i,uint8_t h[32]){return read_at(d,MS_BASE+i*RS_SECTOR+512,h,32);}
static bool valid(const uint8_t h[32]){return le(h)==MAGIC&&le(h+4)==VOLUME&&le(h+8)<MS_SECTORS&&(le(h+12)||le(h+16))&&le(h+24)==512&&le(h+28)==rs_crc(h,28);}
static uint64_t seq(const uint8_t h[32]){return (uint64_t)le(h+12)|((uint64_t)le(h+16)<<32);}
bool ms_init(struct ms_disk *d,struct rs_io io){
 memset(d,0,sizeof *d);d->io=io;
 for(unsigned i=0;i<MS_SECTORS;i++)d->map[i]=MS_NONE;
 for(unsigned i=0;i<MS_BLOCKS;i++){
  uint8_t h[32];if(!record(d,i,h))return false;
  if(le(h)==MAGIC&&le(h+28)==rs_crc(h,28)&&!valid(h)){d->fault=true;return false;}
  if(!valid(h))continue;
  uint64_t n=seq(h);unsigned l=le(h+8);uint16_t old=d->map[l];
  if(old!=MS_NONE){uint8_t prior[32];if(!record(d,old,prior))return false;
   if(seq(prior)==n){d->fault=true;return false;}
   if(seq(prior)>n)continue;
   d->live[old]=0;
  }
  d->map[l]=(uint16_t)i;d->live[i]=1;
  if(n>d->sequence){d->sequence=n;d->cursor=(i+1)%MS_BLOCKS;}
 }
 /* Verify only current records: a power cut erasing an obsolete block may
  * leave its old header intact but payload damaged. It is not live media. */
 for(unsigned l=0;l<MS_SECTORS;l++)if(d->map[l]!=MS_NONE){
  uint8_t h[32],b[512];unsigned i=d->map[l];
  if(!record(d,i,h)||!read_at(d,MS_BASE+i*RS_SECTOR,b,512))return false;
  if(rs_crc(b,512)!=le(h+20)){d->fault=true;return false;}
 }
 return true;
}
bool ms_read(struct ms_disk *d,uint32_t l,uint8_t b[512]){
 if(d->fault||l>=MS_SECTORS)return false;
 if(d->map[l]==MS_NONE){ms_seed(l,b);return true;}
 return read_at(d,MS_BASE+d->map[l]*RS_SECTOR,b,512);
}
bool ms_write(struct ms_disk *d,uint32_t l,const uint8_t b[512]){
 uint8_t old[512];if(d->fault||l>=MS_SECTORS||d->sequence==UINT64_MAX||!d->io.safe(d->io.ctx))return false;
 if(!ms_read(d,l,old))return false;
 if(!memcmp(old,b,512))return true;
 unsigned i=d->cursor,tries=0;while(d->live[i]&&tries++<MS_BLOCKS)i=(i+1)%MS_BLOCKS;
 if(tries>=MS_BLOCKS){d->fault=true;return false;}
 uint32_t a=MS_BASE+i*RS_SECTOR;uint8_t page[256],check[512];
 if(!d->io.erase(d->io.ctx,a))goto fault;
 for(unsigned p=0;p<2;p++)if(!d->io.program(d->io.ctx,a+p*256,b+p*256))goto fault;
 if(!read_at(d,a,check,512)||memcmp(check,b,512))goto fault;
 memset(page,255,sizeof page);put(page,MAGIC);put(page+4,VOLUME);put(page+8,l);
 uint64_t next=d->sequence+1;put(page+12,(uint32_t)next);put(page+16,(uint32_t)(next>>32));put(page+20,rs_crc(b,512));put(page+24,512);put(page+28,rs_crc(page,28));
 if(!d->io.program(d->io.ctx,a+512,page)||!read_at(d,a+512,check,256)||memcmp(check,page,256)||!d->io.safe(d->io.ctx))goto fault;
 if(d->map[l]!=MS_NONE)d->live[d->map[l]]=0;
 d->map[l]=(uint16_t)i;d->live[i]=1;d->sequence=next;d->cursor=(i+1)%MS_BLOCKS;d->writes++;return true;
fault:d->fault=true;return false;
}
bool ms_transfer(struct ms_disk *d,bool write,uint32_t lba,uint32_t offset,void *buffer,size_t n){
 uint64_t at=(uint64_t)lba*512+offset;
 if(offset>=512||at>MS_BYTES||n>MS_BYTES-at||d->fault)return false;
 uint8_t *p=buffer,b[512];
 while(n){unsigned l=(unsigned)(at/512),off=(unsigned)(at%512);size_t k=512-off;if(k>n)k=n;
  if(!ms_read(d,l,b))return false;
  if(write){memcpy(b+off,p,k);if(!ms_write(d,l,b))return false;}else memcpy(p,b+off,k);
  p+=k;n-=k;at+=k;
 }
 return true;
}
