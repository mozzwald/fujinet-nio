/* Bounded read-only parser for our fixed FAT12 geometry. PC is sole FAT writer.
 * This accepts fragmented files; no pointers or sizes from media are trusted. */
#include "msc_image.h"
#include <string.h>
static uint32_t le(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t l16(const uint8_t *p){return (uint16_t)(p[0]|p[1]<<8);}
static uint16_t b16(const uint8_t *p){return (uint16_t)(p[0]<<8|p[1]);}
static uint32_t be(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint16_t crc16(uint16_t c,const uint8_t *p,size_t n){while(n--){c^=(uint16_t)*p++<<8;for(unsigned k=0;k<8;k++)c=(uint16_t)((c<<1)^((c&0x8000)?0x1021:0));}return c;}
static bool ptr(uint32_t v,size_t n,size_t size,uint32_t *off){if(v<0x08000000u)return false;*off=v-0x08000000u;return *off<=size&&n<=size-*off;}
bool mi_rom(const uint8_t *b,size_t n){
 if(n<512||n>RS_CAPACITY||(n&1)||be(b+8)!=0xfeedbeef||b16(b+12)!=4||be(b+256)!=0xfeedface||be(b+108)!=n)return false;
 uint16_t c=crc16(0,b,112);if(crc16(c,b+114,n-114)!=b16(b+112))return false;
 uint32_t token,dir;
 if(!ptr(be(b+100),14,n,&token)||memcmp(b+token,"HsAT\0\3",6)||memcmp(b+token+10,"\xff\xff\xff\xff",4))return false;
 if(!ptr(be(b+316),6,n,&dir)||be(b+dir))return false;
 unsigned dbs=b16(b+dir+4);if(!dbs||dbs>32||dbs>(n-dir-6)/4)return false;
 for(unsigned d=0;d<dbs;d++){
  uint32_t db;if(!ptr(be(b+dir+6+d*4),78,n,&db))return false;
  if(!memchr(b+db,0,32)||(b16(b+db+32)&3)!=3||be(b+db+72))return false;
  unsigned count=b16(b+db+76);if(count>(n-db-78)/10)return false;
  for(unsigned r=0;r<count;r++){
   uint32_t data;if(!ptr(be(b+db+78+r*10+6),0,n,&data)||data<8)return false;
   uint32_t h=be(b+data-8),total=h&0xffffff,pad=(h>>24)&15;
   if((h&0x80000000)||total<8+pad||total-8-pad>n-data)return false;
  }
 }
 return true;
}
enum mi_result mi_package(uint8_t *b,size_t size,struct mi_image *out){
 if(size<32||memcmp(b,"FNRM",4)||le(b+4)!=1||!le(b+8)||le(b+20)||le(b+24)||rs_crc(b,28)!=le(b+28))return MI_PACKAGE;
 struct mi_image i={le(b+8),le(b+12),le(b+16)};
 if(i.length<512||i.length>RS_CAPACITY||(i.length&1)||size!=32+i.length||rs_crc(b+32,i.length)!=i.crc)return MI_PACKAGE;
 if(!mi_rom(b+32,i.length))return MI_ROM;
 memmove(b,b+32,i.length);*out=i;return MI_OK;
}
enum mi_result mi_load(struct ms_disk *d,uint8_t out[MI_FILE_CAP],struct mi_image *info){
 /* Boot-only, single Core0 reader. Keep the scratch out of 4KiB stack. */
 static uint8_t fat[3072],seen[256];uint8_t b[512];memset(seen,0,sizeof seen);
 if(!ms_read(d,0,b))return MI_MEDIA;
 if(b[510]!=0x55||b[511]!=0xaa||b[450]!=1||le(b+454)!=1||le(b+458)!=2047)return MI_FAT;
 /* Exactly one partition; otherwise reject rather than resolve ambiguity. */
 for(unsigned i=462;i<510;i++)if(b[i])return MI_FAT;
 if(!ms_read(d,1,b))return MI_MEDIA;
 if(l16(b+11)!=512||b[13]!=1||l16(b+14)!=1||b[16]!=2||l16(b+17)!=64||l16(b+19)!=2047||b[21]!=0xf8||l16(b+22)!=6||le(b+28)!=1||le(b+32)||b[510]!=0x55||b[511]!=0xaa)return MI_FAT;
 for(unsigned i=0;i<6;i++){
  if(!ms_read(d,2+i,fat+i*512)||!ms_read(d,8+i,b))return MI_MEDIA;
  if(memcmp(fat+i*512,b,512))return MI_FAT;
 }
 if(fat[0]!=0xf8||fat[1]!=255||fat[2]!=255)return MI_FAT;
 uint32_t length=0;unsigned cluster=0,found=0;
 for(unsigned i=0;i<4;i++){
  if(!ms_read(d,14+i,b))return MI_MEDIA;
  for(unsigned pos=0;pos<512;pos+=32){
   if(b[pos]==0)goto directory_done;
   if(b[pos]==0xe5||b[pos+11]==15)continue;
   if(memcmp(b+pos,"SPRINGBDROM",11))continue;
   if(++found>1||(b[pos+11]&0xd8)||l16(b+pos+20))return MI_FILE;
   cluster=l16(b+pos+26);length=le(b+pos+28);
  }
 }
directory_done:
 if(!found)return MI_ABSENT;
 if(length<544||length>MI_FILE_CAP||(length&1))return MI_FILE;
 for(uint32_t off=0;off<length;off+=512){
  if(cluster<2||cluster>2031||seen[cluster/8]&(1u<<(cluster%8)))return MI_FILE;
  seen[cluster/8]|=(uint8_t)(1u<<(cluster%8));
  if(!ms_read(d,18+cluster-2,b))return MI_MEDIA;
  size_t k=length-off;if(k>512)k=512;memcpy(out+off,b,k);
  unsigned at=cluster+cluster/2,next=l16(fat+at);next=cluster&1?next>>4:next&0xfff;
  if(off+k==length){if(next<0xff8)return MI_FILE;}else cluster=next;
 }
 return mi_package(out,length,info);
}
enum mi_result mi_promote(struct rom_store *s,const uint8_t *bytes,struct mi_image i){
 const struct rs_image *a=rs_active(s);
 if(a&&a->version==i.version&&a->length==i.length&&a->crc==i.crc){
  uint8_t b[256];uint32_t slot=rs_selected(s);
  for(uint32_t off=0;off<i.length;off+=256){size_t n=i.length-off;if(n>256)n=256;
   if(rs_read(s,slot,off,b,n)||memcmp(b,bytes+off,n))return MI_STORAGE;
  }
  return MI_OK;
 }
 uint32_t slot=rs_selected(s)==0?1:0;
 if(rs_begin(s,slot,i.version,i.length,i.crc,1))return MI_STORAGE;
 for(uint32_t off=0;off<i.length;off+=256){size_t n=i.length-off;if(n>256)n=256;
  if(rs_chunk(s,1,off,bytes+off,n))return MI_STORAGE;
 }
 if(rs_finish(s,1)||rs_activate(s,slot))return MI_STORAGE;
 return MI_OK;
}
