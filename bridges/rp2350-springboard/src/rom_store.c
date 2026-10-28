/* Portable durable storage. Headers are committed last; no firmware offsets
 * are accepted from USB. All destructive callbacks are independently gated. */
#include "rom_store.h"
#include <string.h>
#define IMAGE_MAGIC 0x31494d52u
#define META_MAGIC 0x31414d52u
static uint32_t get(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
uint32_t rs_crc(const void *data,size_t n){const uint8_t *p=data;uint32_t c=~0u;while(n--){c^=*p++;for(unsigned b=0;b<8;++b)c=(c>>1)^((0u-(c&1))&0xedb88320u);}return ~c;}
static bool legal(uint32_t version,uint32_t n){return version&&n>=512&&n<=RS_CAPACITY&&!(n&1);}
static bool header(struct rom_store *s,uint32_t off,uint8_t *b,uint32_t magic){
 if(!s->io.read(s->io.ctx,off,b,RS_PAGE)||get(b)!=magic||get(b+4)!=RS_SCHEMA||get(b+28)!=rs_crc(b,28))return false;
 for(unsigned i=32;i<RS_PAGE;++i)if(b[i]!=255)return false;
 return true;
}
static struct rs_image image(struct rom_store *s,uint32_t slot){
 struct rs_image d={0};uint8_t b[RS_PAGE];
 if(slot>1||!header(s,RS_SLOT0+slot*RS_STRIDE,b,IMAGE_MAGIC)||get(b+8)!=slot||get(b+24)!=0||!legal(get(b+12),get(b+16)))return d;
 d=(struct rs_image){1,get(b+12),get(b+16),get(b+20)};
 uint32_t c=~0u;
 for(uint32_t off=0;off<d.length;){size_t n=d.length-off;if(n>RS_PAGE)n=RS_PAGE;
  if(!s->io.read(s->io.ctx,RS_SLOT0+slot*RS_STRIDE+RS_SECTOR+off,b,n))return (struct rs_image){0};
  for(size_t i=0;i<n;++i){c^=b[i];for(unsigned k=0;k<8;++k)c=(c>>1)^((0u-(c&1))&0xedb88320u);}off+=(uint32_t)n;
 }
 if(~c!=d.crc)d.valid=0;
 return d;
}
static bool matches(struct rs_image d,struct rs_record r){return d.valid&&d.version==r.version&&d.length==r.length&&d.crc==r.crc;}
void rs_init(struct rom_store *s,struct rs_io io){
 memset(s,0,sizeof(*s));s->io=io;s->latest=2;s->upload=2;
 for(unsigned i=0;i<2;++i){uint8_t b[RS_PAGE];s->images[i]=image(s,i);
  if(!header(s,RS_META0+i*RS_SECTOR,b,META_MAGIC))continue;
  struct rs_record r={1,get(b+8),get(b+12),get(b+16),get(b+20),get(b+24)};
  if(!r.generation||r.slot>2||(r.slot<2&&!legal(r.version,r.length))||(r.slot==2&&(r.version||r.length||r.crc)))continue;
  s->records[i]=r;
  if(s->latest==2||r.generation>s->records[s->latest].generation)s->latest=i;
 }
 /* Equal, conflicting generations are ambiguous: choose recovery, never guess. */
 if(s->records[0].valid&&s->records[1].valid&&s->records[0].generation==s->records[1].generation&&memcmp(&s->records[0],&s->records[1],sizeof(struct rs_record)))s->latest=2;
}
uint32_t rs_selected(const struct rom_store *s){return s->latest<2?s->records[s->latest].slot:2;}
const struct rs_image *rs_active(const struct rom_store *s){
 if(s->latest>=2)return NULL;
 struct rs_record r=s->records[s->latest];return r.slot<2&&matches(s->images[r.slot],r)?&s->images[r.slot]:NULL;
}
static bool safe(struct rom_store *s){return !s->fault&&s->io.safe(s->io.ctx);}
static bool erase(struct rom_store *s,uint32_t a){bool ok=safe(s)&&s->io.erase(s->io.ctx,a)&&safe(s);if(!ok)s->fault=true;return ok;}
static bool program(struct rom_store *s,uint32_t a,const uint8_t *b){uint8_t r[RS_PAGE];bool ok=safe(s)&&s->io.program(s->io.ctx,a,b)&&safe(s)&&s->io.read(s->io.ctx,a,r,sizeof r)&&!memcmp(b,r,sizeof r);if(!ok)s->fault=true;return ok;}
void rs_abort(struct rom_store *s){s->upload=2;s->received=0;s->nonce=0;}
enum rs_error rs_begin(struct rom_store *s,uint32_t slot,uint32_t version,uint32_t n,uint32_t crc,uint32_t nonce){
 if(!safe(s))return RS_UNSAFE;
 if(s->upload<2)return RS_STATE;
 if(s->latest==2&&(s->records[0].valid||s->records[1].valid))return RS_CONFLICT;
 if(slot>1||!legal(version,n)||!nonce)return RS_RANGE;
 /* Protect the selected slot even when its content failed validation. */
 if(slot==rs_selected(s))return RS_CONFLICT;
 s->images[slot]=(struct rs_image){0};
 for(uint32_t off=0;off<RS_STRIDE;off+=RS_SECTOR)if(!erase(s,RS_SLOT0+slot*RS_STRIDE+off))return RS_IO;
 s->pending=(struct rs_image){0,version,n,crc};s->upload=slot;s->nonce=nonce;s->received=0;return RS_OK;
}
enum rs_error rs_chunk(struct rom_store *s,uint32_t nonce,uint32_t off,const uint8_t *data,size_t n){
 if(!safe(s))return RS_UNSAFE;
 if(s->upload>1||nonce!=s->nonce||off!=s->received)return RS_STATE;
 size_t expected=s->pending.length-off;if(expected>RS_PAGE)expected=RS_PAGE;
 if(!n||n!=expected||off%RS_PAGE)return RS_RANGE;
 uint8_t b[RS_PAGE];memset(b,255,sizeof b);memcpy(b,data,n);
 if(!program(s,RS_SLOT0+s->upload*RS_STRIDE+RS_SECTOR+off,b)){rs_abort(s);return RS_IO;}
 s->received+=(uint32_t)n;return RS_OK;
}
static void make_header(uint8_t *b,uint32_t magic,uint32_t a,uint32_t v,uint32_t n,uint32_t crc,uint32_t extra){
 memset(b,255,RS_PAGE);put(b,magic);put(b+4,1);put(b+8,a);put(b+12,v);put(b+16,n);put(b+20,crc);put(b+24,extra);put(b+28,rs_crc(b,28));
}
enum rs_error rs_finish(struct rom_store *s,uint32_t nonce){
 if(!safe(s))return RS_UNSAFE;
 if(s->upload>1||nonce!=s->nonce||s->received!=s->pending.length)return RS_STATE;
 /* Verify payload before publishing the only valid header. */
 uint8_t b[RS_PAGE];uint32_t c=~0u;
 for(uint32_t off=0;off<s->received;){size_t n=s->received-off;if(n>RS_PAGE)n=RS_PAGE;
  if(!s->io.read(s->io.ctx,RS_SLOT0+s->upload*RS_STRIDE+RS_SECTOR+off,b,n)){rs_abort(s);return RS_IO;}
  for(size_t i=0;i<n;++i){c^=b[i];for(unsigned k=0;k<8;++k)c=(c>>1)^((0u-(c&1))&0xedb88320u);}off+=(uint32_t)n;
 }
 if(~c!=s->pending.crc){rs_abort(s);return RS_CRC;}
 uint32_t slot=s->upload;
 make_header(b,IMAGE_MAGIC,slot,s->pending.version,s->pending.length,s->pending.crc,0);
 if(!program(s,RS_SLOT0+slot*RS_STRIDE,b)){rs_abort(s);return RS_IO;}
 s->images[slot]=image(s,slot);rs_abort(s);return s->images[slot].valid?RS_OK:RS_CRC;
}
enum rs_error rs_verify(struct rom_store *s,uint32_t slot){if(!safe(s))return RS_UNSAFE;if(slot>1)return RS_RANGE;s->images[slot]=image(s,slot);return s->images[slot].valid?RS_OK:RS_CRC;}
enum rs_error rs_activate(struct rom_store *s,uint32_t slot){
 if(!safe(s))return RS_UNSAFE;
 if(s->upload<2)return RS_STATE;
 if(s->latest==2&&(s->records[0].valid||s->records[1].valid))return RS_CONFLICT;
 if(slot>2)return RS_RANGE;
 struct rs_image d={0};if(slot<2){enum rs_error e=rs_verify(s,slot);if(e)return e;d=s->images[slot];}
 uint32_t generation=s->latest<2?s->records[s->latest].generation:0;
 if(generation==UINT32_MAX)return RS_STATE;
 uint32_t index=s->latest<2?1-s->latest:0;uint8_t b[RS_PAGE];
 make_header(b,META_MAGIC,generation+1,slot,d.version,d.length,d.crc);
 if(!erase(s,RS_META0+index*RS_SECTOR)||!program(s,RS_META0+index*RS_SECTOR,b))return RS_IO;
 s->records[index]=(struct rs_record){1,generation+1,slot,d.version,d.length,d.crc};s->latest=index;return RS_OK;
}
enum rs_error rs_rollback(struct rom_store *s){
 if(!safe(s))return RS_UNSAFE;
 if(s->latest>=2)return RS_STATE;
 struct rs_record r=s->records[1-s->latest];
 if(!r.valid)return rs_activate(s,2);
 if(r.slot<2){if(rs_verify(s,r.slot)||!matches(s->images[r.slot],r))return RS_CONFLICT;}
 return rs_activate(s,r.slot);
}
enum rs_error rs_read(struct rom_store *s,uint32_t slot,uint32_t off,void *buf,size_t n){
 if(!safe(s))return RS_UNSAFE;
 if(slot>1||!n||n>RS_PAGE||off>RS_CAPACITY||n>RS_CAPACITY-off)return RS_RANGE;
 return s->io.read(s->io.ctx,RS_SLOT0+slot*RS_STRIDE+RS_SECTOR+off,buf,n)?RS_OK:RS_IO;
}
bool rs_copy_active(struct rom_store *s,uint8_t *dest,size_t cap){
 const struct rs_image *d=rs_active(s);if(!d||cap<d->length)return false;
 return s->io.read(s->io.ctx,RS_SLOT0+rs_selected(s)*RS_STRIDE+RS_SECTOR,dest,d->length)&&rs_crc(dest,d->length)==d->crc;
}
