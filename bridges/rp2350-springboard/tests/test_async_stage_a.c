#include "async_stage_a.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static uint16_t table[VMB_TABLE_BYTES/2];
static uint32_t high(unsigned value){return 0x20000005u|(value<<4);}
int main(void) {
 struct sb_stage_a s;sb_stage_a_init(&s,table);
 CHECK(table[VMB_ID/2]==VMB_ID_VALUE&&table[VMB_STATUS/2]==VMB_ST_CAN_STAGE);
 CHECK(sb_stage_a_retire(&s,0x50000|VMB_PROBE,high(1),0,UINT32_MAX,0));
 CHECK(!s.accepted&&table[VMB_PROBE_ACK/2]==0);
 CHECK(sb_stage_a_retire(&s,0x50000|VMB_PROBE,high(1),UINT32_MAX,0,0));
 CHECK(s.accepted==1&&table[VMB_PROBE_ACK/2]==1);
 CHECK(sb_stage_a_retire(&s,0x50000|VMB_PROBE,high(1),UINT32_MAX-1,0,0));
 CHECK(s.accepted==1);
 CHECK(sb_stage_a_retire(&s,0x50000|VMB_PROBE,high(3),UINT32_MAX-2,0,0));
 CHECK(s.accepted==1&&table[VMB_ERROR/2]==VMB_ERR_SEQUENCE);
 CHECK(sb_stage_a_retire(&s,0x50000|VMB_PROBE,high(2),UINT32_MAX-3,0,0));
 CHECK(s.accepted==2&&table[VMB_ERROR/2]==0);
 CHECK(!sb_stage_a_retire(&s,0x50000|VMB_PROBE,high(3),1,0,0));
 CHECK(s.accepted==2&&(table[VMB_STATUS/2]&VMB_ST_FAULT));
 for(unsigned fault=1;fault<=2;++fault){sb_stage_a_init(&s,table);
  CHECK(!sb_stage_a_retire(&s,0x50000|VMB_PROBE,high(1),0,0,fault));CHECK(!s.accepted);}
 sb_stage_a_init(&s,table);CHECK(!sb_stage_a_retire(&s,0x50000|VMB_PROBE,high(1)&~0x20000000u,0,0,0));
 CHECK(!s.accepted);puts("PASS async Stage A: cancellation, ordered acceptance, duplicate, sequence, epoch, FIFO/control/reset quarantine");
}
