#include "rom_wire.h"
#include <string.h>
uint32_t rm_get(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
void rm_put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
/* CRC covers header bytes0..11 and payload, never its own field. */
static uint32_t crc(const uint8_t *b,size_t n){uint8_t p[12+RM_MAX_PAYLOAD];memcpy(p,b,12);memcpy(p+12,b+16,n);return rs_crc(p,12+n);}
size_t rm_encode(uint8_t *b,uint8_t op,uint32_t seq,const uint8_t *p,size_t n){
 if(n>RM_MAX_PAYLOAD)return 0;
 memcpy(b,"RMU1",4);b[4]=1;b[5]=op;b[6]=(uint8_t)n;b[7]=(uint8_t)(n>>8);rm_put(b+8,seq);if(n&&p!=b+16)memcpy(b+16,p,n);rm_put(b+12,crc(b,n));return n+16;
}
int rm_feed(struct rm_frame *f,uint8_t b){
 if(f->used<4){if(b==(uint8_t)"RMU1"[f->used])f->data[f->used++]=b;else {f->used=0;if(b=='R')f->data[f->used++]=b;}return 0;}
 f->data[f->used++]=b;
 if(f->used<16)return 0;
 size_t n=f->data[6]|(size_t)f->data[7]<<8;
 if(f->data[4]!=1||n>RM_MAX_PAYLOAD){f->used=0;return -1;}
 if(f->used<16+n)return 0;
 f->used=0;return rm_get(f->data+12)==crc(f->data,n)?1:-1;
}
size_t rm_dispatch(struct rom_store *s,struct rm_backend b,const uint8_t *request,uint8_t *response){
 const uint8_t *p=request+16;uint8_t *payload=response+16;
 size_t n=request[6]|(size_t)request[7]<<8;uint8_t op=request[5];uint32_t seq=rm_get(request+8);
 enum rs_error e=RS_RANGE;size_t extra=0;
 switch(op){
 case RM_STATUS:if(!n){
  struct rm_state t=b.state(b.ctx);
  uint32_t v[24]={1,t.flash_bytes,t.active,t.stopped,t.maintenance,t.core_held,
   rs_selected(s),s->latest<2?s->records[s->latest].generation:0,t.served_source,t.served_length,t.served_crc32,t.fallback_reason,s->upload,s->received,s->fault,t.owner_fault};
  for(unsigned i=0;i<2;++i){struct rs_image d=s->images[i];unsigned k=16+4*i;v[k]=d.valid;v[k+1]=d.version;v[k+2]=d.length;v[k+3]=d.crc;}
  for(unsigned i=0;i<24;++i){rm_put(payload+4+4*i,v[i]);}
  extra=sizeof v;e=RS_OK;
 }break;
 case RM_QUIESCE:if(!n)e=b.quiesce(b.ctx);break;
 case RM_BEGIN:if(n==20)e=rs_begin(s,rm_get(p),rm_get(p+4),rm_get(p+8),rm_get(p+12),rm_get(p+16));break;
 case RM_CHUNK:if(n>8&&n<=264)e=rs_chunk(s,rm_get(p),rm_get(p+4),p+8,n-8);break;
 case RM_FINISH:if(n==4)e=rs_finish(s,rm_get(p));break;
 case RM_READ:if(n==12&&rm_get(p+8)<=RS_PAGE){e=rs_read(s,rm_get(p),rm_get(p+4),payload+4,rm_get(p+8));if(!e)extra=rm_get(p+8);}break;
 case RM_VERIFY:if(n==4)e=rs_verify(s,rm_get(p));break;
 case RM_ACTIVATE:if(n==4){e=rs_activate(s,rm_get(p));if(!e)b.load(b.ctx);}break;
 case RM_ROLLBACK:if(!n){e=rs_rollback(s);if(!e)b.load(b.ctx);}break;
 case RM_RECOVERY:if(!n){e=rs_activate(s,RS_RECOVERY);if(!e)b.load(b.ctx);}break;
 case RM_RESUME:if(!n)e=b.resume(b.ctx);break;
 case RM_ABORT:if(!n){e=s->io.safe(s->io.ctx)?RS_OK:RS_UNSAFE;if(!e)rs_abort(s);}break;
 default:e=RS_FRAME;break;
 }
 rm_put(payload,e);return rm_encode(response,op|128,seq,payload,4+extra);
}
