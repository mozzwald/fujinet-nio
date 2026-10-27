#include "async_stage_a.h"
#include "bus_model.h"
#include <stdatomic.h>
/* Core0 is the sole writer. Aligned halfword publication is atomic on M33.
 * Release/acquire fences also order future immutable response publication.
 * Status is observational; a matching token is the acceptance authority.
 */
static void publish(volatile uint16_t *p,uint16_t v) {
    atomic_thread_fence(memory_order_release); *p=v;
}
void sb_stage_a_init(struct sb_stage_a *s,volatile uint16_t *table) {
    sb_cs1_register_reset(&s->capture);s->table=table;s->accepted=0;
    for(unsigned i=0;i<VMB_TABLE_BYTES/2;++i)table[i]=0xffff;
    table[VMB_ID/2]=VMB_ID_VALUE;table[VMB_VERSION/2]=VMB_ABI_VERSION;
    table[VMB_CAPABILITIES/2]=VMB_CAP_STAGE_A;table[VMB_STATUS/2]=VMB_ST_CAN_STAGE;
    table[VMB_GENERATION/2]=1;table[VMB_ERROR/2]=0;table[VMB_PROBE_ACK/2]=0;
}
void sb_stage_a_fault(struct sb_stage_a *s,uint16_t error) {
    publish(s->table+VMB_ERROR/2,error);
    publish(s->table+VMB_STATUS/2,VMB_ST_ERROR|VMB_ST_FAULT);
}
bool sb_stage_a_retire(struct sb_stage_a *s,uint32_t low,uint32_t high,
                       uint32_t epoch,uint32_t outcome,uint32_t faults) {
    /* Reuse the audited qualification/epoch validator. Its private legacy
     * scratch field is never published. Mailbox effects follow validation. */
    if(!sb_cs1_write_record(&s->capture,low,high,epoch,outcome,faults)) {
        sb_stage_a_fault(s,VMB_ERR_CAPTURE);return false;
    }
    if(outcome)return true;
    if(s->table[VMB_STATUS/2]&VMB_ST_FAULT)return false;
    uint32_t a=sb_address(low,high);uint16_t v=(uint16_t)(high>>4);
    if(a!=VMB_PROBE) {publish(s->table+VMB_ERROR/2,VMB_ERR_UNSUPPORTED);publish(s->table+VMB_STATUS/2,VMB_ST_CAN_STAGE|VMB_ST_ERROR);return true;}
    uint16_t old=s->table[VMB_PROBE_ACK/2];
    if(v==old)return true;
    if(old==0xffff || v!=(uint16_t)(old+1)) {
        publish(s->table+VMB_ERROR/2,VMB_ERR_SEQUENCE);
        publish(s->table+VMB_STATUS/2,VMB_ST_CAN_STAGE|VMB_ST_ERROR);return true;
    }
    ++s->accepted;publish(s->table+VMB_ERROR/2,VMB_ERR_NONE);
    publish(s->table+VMB_STATUS/2,VMB_ST_CAN_STAGE);
    publish(s->table+VMB_PROBE_ACK/2,v);return true;
}
