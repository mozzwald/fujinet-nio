#ifdef SB_ASYNC_CAPTURE
#include "async_program.h"
#define sb_cs1_program_init sb_async_program_init
#else
#include "cs1_program.h"
#endif
#include "cs1_registers.h"
#include "bus_model.h"
#include <apio.h>
#include <epio_priv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATA (UINT64_C(0xffff) << 20)
#define RESET (UINT64_C(1) << 45)
#define IDLE (RESET | (UINT64_C(15) << 16))
#define CS0 (RESET | (UINT64_C(10) << 16))
#define CS1 (RESET | (UINT64_C(9) << 16))
#define WRITE (RESET | (UINT64_C(5) << 16))
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line%d t%u: %s pc%u irq%d\n", \
    __LINE__, s->t, #c, s->e->block[1].sm[0].pc, epio_peek_block_irq_num(s->e,1,0)); exit(1); } } while(0)
struct tag { unsigned kind, owner; };
struct sim {
    epio_t *e; unsigned t, owner, snapshot_owner, loaded_owner, tx_owner;
    unsigned cpu_phase, due, latency, requests, completions, enables, cancels;
    unsigned head, tail, last_end, max_release, sample_owner, sampled_word;
    uint16_t reply;
    bool capture_fault;
    uint32_t fdebug[3];
    struct sb_cs1_registers regs;
    uint64_t pins, pipe[2]; unsigned owners[2]; bool selected, stall;
    struct tag tags[16];
};
static uint16_t rom(unsigned address) {
    if (address == 8) return 0xfeed;
    if (address == 10) return 0xbeef;
    if (address == 0x444) return 0xa174;
    if (address == 0x446) return 0x5c8f;
    return (uint16_t)((address * 40503u) ^ 0xa55a);
}
static bool driven(struct sim *s) { return (s->e->gpio.gpio_direction & DATA) != 0; }
static unsigned address(uint64_t p) { return sb_address((uint32_t)p,(uint32_t)(p>>16)); }
static uint16_t expected(struct sim *s, uint64_t p) {
    if (((p >> 16) & 15) == 10) return address(p)>=65536?0xffff:rom(address(p));
    return sb_cs1_register_read(&s->regs,address(p));
}
static struct tag pop(struct sim *s, unsigned kind) {
    CHECK(s->head != s->tail); struct tag q = s->tags[s->head++ % 16];
    CHECK(q.kind == kind); return q;
}
static void cpu(struct sim *s) {
    if (s->stall) return;
    /* Functional Core1 model, NOT compiled ARM timing. Drains only complete
     * paired records; instruction costs must be established before integration.
     */
    if (epio_rx_fifo_depth(s->e,0,SB_CS1_LOW_SM)>=1 &&
        epio_rx_fifo_depth(s->e,2,SB_CS1_HIGH_SM)>=1 &&
        epio_rx_fifo_depth(s->e,2,SB_CS1_END_SM)>=2) {
        uint32_t faults=(s->capture_fault?SB_CS1_FAULT_FIFO:0) |
            (epio_peek_block_irq_num(s->e,2,5)?SB_CS1_FAULT_CONTROL:0);
        uint32_t lo=epio_pop_rx_fifo(s->e,0,SB_CS1_LOW_SM);
        uint32_t hi=epio_pop_rx_fifo(s->e,2,SB_CS1_HIGH_SM);
        uint32_t epoch=epio_pop_rx_fifo(s->e,2,SB_CS1_END_SM);
        uint32_t outcome=epio_pop_rx_fifo(s->e,2,SB_CS1_END_SM);
        bool handled=sb_cs1_write_record(&s->regs,lo,hi,epoch,outcome,faults);
        CHECK(handled || s->regs.fault);
    }
    if (s->regs.fault) return;
    if (!s->cpu_phase && epio_rx_fifo_depth(s->e,1,0)) {
        struct tag q = pop(s,0); uint32_t high = epio_pop_rx_fifo(s->e,1,0);
        uint64_t sampled = (s->pins & 0xffff) | ((uint64_t)high << 16);
        s->reply = expected(s,sampled); s->tx_owner=q.owner;
        s->due=s->t+s->latency; s->cpu_phase=1; ++s->requests;
    }
    if (s->cpu_phase==1 && s->t>=s->due) {
        CHECK(!epio_tx_fifo_depth(s->e,1,0));
        epio_push_tx_fifo(s->e,1,0,s->reply); s->cpu_phase=2;
    }
    if (s->cpu_phase==2 && epio_rx_fifo_depth(s->e,1,0)) {
        (void)pop(s,1); uint32_t token=epio_pop_rx_fifo(s->e,1,0);
        CHECK(token==0 || token==UINT32_MAX);
        if (!token) ++s->cancels;
        ++s->completions; s->cpu_phase=0;
    }
}
static void step(struct sim *s, unsigned n) {
    while (n--) {
        cpu(s);
        uint64_t external=((UINT64_C(1)<<48)-1)&~DATA;
        if (!(s->pipe[1] & (UINT64_C(1)<<19))) external |= DATA;
        epio_drive_gpios_ext(s->e,external,s->pipe[1]);
        unsigned seen_owner=s->owners[1];
        s->pipe[1]=s->pipe[0]; s->pipe[0]=s->pins;
        s->owners[1]=s->owners[0]; s->owners[0]=s->owner;
        epio_sm_state_t *m=&s->e->block[1].sm[0];
        unsigned pc=m->pc, rx=epio_rx_fifo_depth(s->e,1,0);
        bool execute=m->enabled&&!m->delay, before=driven(s);
        if (execute && pc==SB_CS1_READ_SNAPSHOT) s->snapshot_owner=seen_owner;
        if (execute && pc==SB_CS1_READ_PULL && epio_tx_fifo_depth(s->e,1,0))
            s->loaded_owner=s->tx_owner;
        if (execute && pc==SB_CS1_READ_PRELOAD) CHECK(!before);
        /* Model the hardware's sticky FDEBUG.RXSTALL on the three write
         * streams. A resumed consumer must inspect it before committing.
         * This is not inferred from a software timeout or missing bytes.
         */
        const unsigned blocks[]={0,2,2}, sms[]={SB_CS1_LOW_SM,SB_CS1_HIGH_SM,SB_CS1_END_SM};
        for(unsigned i=0;i<3;++i) {
            epio_sm_state_t *w=&s->e->block[blocks[i]].sm[sms[i]];
            unsigned short op=epio_get_instr(s->e,blocks[i],w->pc);
            bool push=(op==APIO_PUSH_BLOCK) ||
                (blocks[i]==2 && sms[i]==SB_CS1_END_SM && w->pc==SB_CS1_END_START+1);
            if(w->enabled && !w->delay && push && epio_rx_fifo_depth(s->e,blocks[i],sms[i])==4)
                { s->capture_fault=true; s->fdebug[blocks[i]]|=1u<<sms[i]; }
        }
        if (getenv("CS1_TRACE")) fprintf(stderr,"t%u pc%u dirs%llx irq%d gPC=%u/%u/%u WEpc%u CFpc%u RSTpc%u Y%u X%u in%llx OSR%x\n", s->t,pc,(unsigned long long)s->e->gpio.gpio_direction,epio_peek_block_irq_num(s->e,1,0),s->e->block[1].sm[1].pc,s->e->block[1].sm[2].pc,s->e->block[1].sm[3].pc,s->e->block[0].sm[0].pc,s->e->block[0].sm[1].pc,s->e->block[2].sm[0].pc,m->y,m->x,(unsigned long long)s->e->gpio.gpio_input_state,m->osr);
        /* Prove the qualification invariant used by the ARM fast path:
         * an actual low/high start PUSH cannot publish any nibble but5.
         * These checks apply to every native and compiled-code scenario. */
        for(unsigned block=0;block<=2;block+=2) {
            unsigned sm=block?SB_CS1_HIGH_SM:SB_CS1_LOW_SM;
            epio_sm_state_t *w=&s->e->block[block].sm[sm];
            if(w->enabled && !w->delay && epio_get_instr(s->e,block,w->pc)==APIO_PUSH_BLOCK &&
               epio_rx_fifo_depth(s->e,block,sm)<4)
                CHECK(((w->isr>>(block?0:16))&15)==5);
        }
        epio_step_cycles(s->e,1);
        if (epio_rx_fifo_depth(s->e,1,0)>rx) {
            CHECK(pc==SB_CS1_READ_SNAPSHOT || pc==SB_CS1_READ_COMPLETE);
            CHECK(s->tail-s->head<16);
            s->tags[s->tail++%16]=(struct tag){pc==SB_CS1_READ_COMPLETE,s->snapshot_owner};
        }
        CHECK(!(s->e->gpio.gpio_direction & ~DATA));
        CHECK(!driven(s) || (s->e->gpio.gpio_direction&DATA)==DATA);
        if (driven(s) && s->selected) {
            CHECK(s->loaded_owner==s->owner);
            CHECK(((s->e->gpio.gpio_output_state&DATA)>>20)==expected(s,s->pins));
        }
        if (driven(s) && !s->selected) {
            CHECK(s->t-s->last_end<=5);
            if (s->t-s->last_end>s->max_release) s->max_release=s->t-s->last_end;
        }
        if (!before && driven(s)) ++s->enables;
        ++s->t;
    }
}
static void bus(struct sim *s, uint64_t controls, unsigned a, unsigned n) {
    bool select=(controls==CS0 || controls==CS1);
    if (s->selected && (!select || (controls&0xf0000)!=(s->pins&0xf0000)))
        s->last_end=s->t;
    if (select && (!s->selected || (controls&0xf0000)!=(s->pins&0xf0000))) ++s->owner;
    s->pins=controls | (a&0xfffe) | (((uint64_t)(a>>16)&7)<<36) |
            (((uint64_t)(a>>19)&31)<<40);
    s->selected=select; step(s,n);
}
static void init(struct sim *s, unsigned latency) {
    memset(s,0,sizeof(*s)); s->latency=latency; s->regs.scratch=0xa55a;
    sb_cs1_program_init(true); s->e=epio_init(); CHECK(s->e);
    epio_update_from_apio(s->e);
    CHECK(epio_get_gpiobase(s->e,0)==0 && epio_get_gpiobase(s->e,1)==16 && epio_get_gpiobase(s->e,2)==16);
    s->pins=s->pipe[0]=s->pipe[1]=IDLE;
    bus(s,IDLE,0,40); CHECK(!driven(s));
}
static void read_pair(unsigned gap, unsigned phase, uint64_t first, uint64_t second, unsigned late, bool held_oe) {
    struct sim state,*s=&state; init(s,late);
    bus(s,IDLE,0,phase);
    bus(s,first,first==CS0?8:0,64); CHECK(driven(s));
    bus(s,held_oe?(IDLE&~(UINT64_C(1)<<18)):IDLE,0,gap);
    bus(s,second,second==CS0?10:2,64); CHECK(driven(s));
    bus(s,IDLE,0,100); CHECK(!driven(s));
    CHECK(s->requests==2 && s->completions==2 && s->enables==2 && !s->cancels);
    epio_free(s->e);
}
static void write_end_gate(unsigned end_nibble, bool should_commit) {
    struct sim state,*s=&state; init(s,12); s->stall=true;
    bus(s,WRITE | (UINT64_C(0x1234)<<20),4,64);
    bus(s,RESET | ((uint64_t)end_nibble<<16),4,40);
    if (end_nibble==7) bus(s,IDLE,0,20);
    CHECK(epio_rx_fifo_depth(s->e,0,SB_CS1_LOW_SM)==1);
    CHECK(epio_rx_fifo_depth(s->e,2,SB_CS1_HIGH_SM)==1);
    uint32_t lo=epio_pop_rx_fifo(s->e,0,SB_CS1_LOW_SM);
    uint32_t hi=epio_pop_rx_fifo(s->e,2,SB_CS1_HIGH_SM);
    CHECK(epio_rx_fifo_depth(s->e,2,SB_CS1_END_SM)==2);
    uint32_t epoch=epio_pop_rx_fifo(s->e,2,SB_CS1_END_SM);
    uint32_t outcome=epio_pop_rx_fifo(s->e,2,SB_CS1_END_SM);
    CHECK(sb_cs1_write_record(&s->regs,lo,hi,epoch,outcome,0));
    bool committed=s->regs.committed!=0;
    CHECK(committed==should_commit);
    if (should_commit) CHECK(s->regs.scratch==0x1234);
    else CHECK(!s->regs.fault && s->regs.scratch==0xa55a && s->regs.rejected==1);
    printf("write-end=%u start=%08x/%08x epoch=%08x outcome=%08x commit=%u fault=%u\n",
           end_nibble,lo,hi,epoch,outcome,committed,(unsigned)s->regs.fault);
    CHECK(!driven(s)); epio_free(s->e);
}
static void upper_address(void) {
    struct sim state,*s=&state; init(s,12);
    bus(s,CS1,0x10004,64); CHECK(driven(s));
    CHECK(((s->e->gpio.gpio_output_state&DATA)>>20)==0xffff);
    bus(s,IDLE,0,20);
    bus(s,WRITE | (UINT64_C(0x1234)<<20),0x10004,64);
    bus(s,RESET | (UINT64_C(13)<<16),0x10004,20);
    CHECK(s->regs.scratch==0xa55a && !s->regs.committed && s->regs.ignored==1);
    epio_free(s->e);
}
static void capture(void) {
    struct sim state,*s=&state; init(s,12);
    s->stall=true;
    uint64_t pins=WRITE | (UINT64_C(0xa174)<<20);
    bus(s,pins,4,40);
    CHECK(epio_rx_fifo_depth(s->e,0,SB_CS1_LOW_SM)==1);
    CHECK(epio_rx_fifo_depth(s->e,2,SB_CS1_HIGH_SM)==1);
    uint32_t lo=epio_pop_rx_fifo(s->e,0,SB_CS1_LOW_SM);
    uint32_t hi=epio_pop_rx_fifo(s->e,2,SB_CS1_HIGH_SM);
    CHECK((lo&0xfffe)==4 && ((hi>>4)&0xffff)==0xa174);
    CHECK(!driven(s));
    bus(s,RESET | (UINT64_C(13)<<16) | (UINT64_C(0x5c8f)<<20),4,30);
    CHECK(!epio_rx_fifo_depth(s->e,0,SB_CS1_LOW_SM));
    CHECK(!epio_rx_fifo_depth(s->e,2,SB_CS1_HIGH_SM));
    CHECK(epio_rx_fifo_depth(s->e,2,SB_CS1_END_SM)==2);
    CHECK(!driven(s));
    epio_free(s->e);
}
static void mixed(void) {
    struct sim state,*s=&state; init(s,12);
    for(unsigned i=0;i<1000;++i) {
        uint16_t value=(uint16_t)((i*40503u)^0xa55a);
        bus(s,CS0,8,64); bus(s,IDLE,0,6);
        bus(s,WRITE | ((uint64_t)value<<20),4,64);
        bus(s,IDLE,0,6);
        bus(s,CS0,10,64); bus(s,IDLE,0,6);
        CHECK(s->regs.scratch==value && s->regs.committed==i+1);
        bus(s,CS1,4,64); CHECK(driven(s));
        CHECK(((s->e->gpio.gpio_output_state&DATA)>>20)==value);
        bus(s,IDLE,0,6);
    }
    bus(s,IDLE,0,100);
    CHECK(s->requests==3000 && s->completions==3000 && !s->cancels && !s->regs.fault);
    epio_free(s->e);
}
static void edge_case(unsigned phase,unsigned width,int delta) {
    struct sim state,*s=&state; init(s,12); s->stall=true;
    bus(s,IDLE,0,phase);
    bus(s,WRITE | (UINT64_C(0x1234)<<20),4,width);
    if(delta<0) bus(s,RESET | (UINT64_C(7)<<16),4,(unsigned)-delta);
    if(delta>0) bus(s,RESET | (UINT64_C(13)<<16),4,(unsigned)delta);
    bus(s,IDLE,0x444,40); /* address may change after selection ends */
    CHECK(!s->capture_fault);
    CHECK(epio_rx_fifo_depth(s->e,0,SB_CS1_LOW_SM)==1);
    CHECK(epio_rx_fifo_depth(s->e,2,SB_CS1_HIGH_SM)==1);
    CHECK(epio_rx_fifo_depth(s->e,2,SB_CS1_END_SM)==2);
    uint32_t lo=epio_pop_rx_fifo(s->e,0,SB_CS1_LOW_SM);
    uint32_t hi=epio_pop_rx_fifo(s->e,2,SB_CS1_HIGH_SM);
    uint32_t epoch=epio_pop_rx_fifo(s->e,2,SB_CS1_END_SM);
    uint32_t outcome=epio_pop_rx_fifo(s->e,2,SB_CS1_END_SM);
    CHECK(epoch==0 && outcome==(delta<0?UINT32_MAX:0));
    CHECK(sb_cs1_write_record(&s->regs,lo,hi,epoch,outcome,0));
    CHECK(!s->regs.fault && s->regs.committed==(unsigned)(delta>=0));
    CHECK(s->regs.scratch==(delta<0?0xa55a:0x1234));
    epio_free(s->e);
}
static void rapid_writes(unsigned gap,unsigned phase,bool stall,unsigned count) {
    struct sim state,*s=&state; init(s,12); s->stall=stall;
    bus(s,IDLE,0,phase);
    for(unsigned i=0;i<count;++i) {
        bus(s,WRITE | ((uint64_t)(0x1234u+i)<<20),4,64);
        bus(s,IDLE,0,gap);
    }
    bus(s,IDLE,0,40); s->stall=false; bus(s,IDLE,0,40);
    if(stall && count>2) {
        CHECK(s->capture_fault && s->regs.fault==SB_CS1_FIFO_FAULT);
        CHECK(!s->regs.committed && s->regs.scratch==0xa55a);
    } else {
        CHECK(!s->capture_fault && !s->regs.fault && s->regs.committed==count);
        CHECK(s->regs.scratch==0x1234u+count-1);
        CHECK(s->regs.next_epoch==0u-count);
    }
    epio_free(s->e);
}
static void fault_record(void) {
    struct sb_cs1_registers r; sb_cs1_register_reset(&r);
    if (!sb_cs1_write_record(&r,0x50004,0x20012345,0,UINT32_MAX,0) ||
        r.committed || r.fault || r.next_epoch!=UINT32_MAX) exit(2);
    /* Cancelled epoch cannot be replayed or relabelled as a success. */
    if (sb_cs1_write_record(&r,0x50004,0x20012345,0,0,0) ||
        r.fault!=SB_CS1_EPOCH_MISMATCH || r.scratch) exit(2);
    sb_cs1_register_reset(&r);
    if (sb_cs1_write_record(&r,0x50004,0x20012345,0,17,0) ||
        r.fault!=SB_CS1_OUTCOME_MALFORMED) exit(2);
}
static void cancel_then_write(unsigned gap,unsigned phase) {
    struct sim state,*s=&state; init(s,12); s->stall=true;
    bus(s,IDLE,0,phase);
    bus(s,WRITE | (UINT64_C(0x1111)<<20),4,64);
    bus(s,RESET | (UINT64_C(7)<<16),4,1); /* one observed clock before WE */
    bus(s,IDLE,0,gap);
    bus(s,WRITE | (UINT64_C(0x2222)<<20),4,64);
    bus(s,IDLE,0,40);
    CHECK(!s->capture_fault); s->stall=false; bus(s,IDLE,0,40);
    CHECK(!s->regs.fault && s->regs.committed==1 && s->regs.rejected==1);
    CHECK(s->regs.scratch==0x2222 && s->regs.next_epoch==0xfffffffeu);
    epio_free(s->e);
}
static void invalid_history(unsigned which,unsigned phase) {
    struct sim state,*s=&state; init(s,12); s->stall=true;
    bus(s,IDLE,0,phase);
    bus(s,WRITE | (UINT64_C(0x1234)<<20),4,40);
    uint64_t bad=which==0?(WRITE&~RESET):
        which==1?(WRITE&~(UINT64_C(1)<<16)):(WRITE&~(UINT64_C(1)<<18));
    bus(s,bad,4,12); /* reset, both selects, or simultaneous OE/WE */
    bus(s,WRITE,4,20); /* later valid levels must not erase history */
    bus(s,IDLE,0,40); s->stall=false; bus(s,IDLE,0,40);
    CHECK(s->regs.fault==SB_CS1_CONTROL_FAULT && !s->regs.committed);
    CHECK(s->regs.scratch==0xa55a && !driven(s));
    epio_free(s->e);
}
static void late_reply(uint64_t next) {
    struct sim state,*s=&state; init(s,80);
    bus(s,CS0,8,30); CHECK(!driven(s));
    bus(s,IDLE,0,6); s->latency=8;
    bus(s,next,next==CS0?10:2,180); CHECK(driven(s));
    bus(s,IDLE,0,80);
    CHECK(s->requests==2 && s->completions==2 && s->cancels==1 && s->enables==1);
    epio_free(s->e);
}
static void stalled_release(uint64_t ending) {
    struct sim state,*s=&state; init(s,8);
    bus(s,CS0,8,64); CHECK(driven(s)); s->stall=true;
    bus(s,ending,0,80); CHECK(!driven(s));
    epio_free(s->e);
}
static void disarmed(void) {
    struct sim state,*s=&state; memset(s,0,sizeof(*s));
    sb_cs1_program_init(false); s->e=epio_init(); CHECK(s->e);
    epio_update_from_apio(s->e); s->pins=s->pipe[0]=s->pipe[1]=IDLE;
    bus(s,CS0,8,100); CHECK(!driven(s)); CHECK(!s->requests);
    epio_free(s->e);
}
int main(void) {
    unsigned cases=0;
    for(unsigned gap=5;gap<=12;++gap) for(unsigned phase=0;phase<8;++phase)
        for(unsigned a=0;a<2;++a) for(unsigned b=0;b<2;++b)
            for(unsigned delay=8;delay<=20;delay+=12) for(unsigned held=0;held<2;++held) {
                read_pair(gap,phase,a?CS1:CS0,b?CS1:CS0,delay,held); ++cases;
            }
    capture(); mixed(); late_reply(CS0); late_reply(CS1); disarmed();
    upper_address();
    write_end_gate(13,true); /* WE high before observed CS1 release */
    write_end_gate(7,false); /* CS1 ends while WE remains low */
    write_end_gate(15,true); /* legal zero CS1 hold after WE */
    for(unsigned phase=0;phase<12;++phase)
        for(unsigned width=32;width<=128;width*=2)
            for(int delta=-12;delta<=12;++delta) edge_case(phase,width,delta);
    for(unsigned phase=0;phase<8;++phase) for(unsigned gap=5;gap<=12;++gap) {
        rapid_writes(gap,phase,false,4);
        rapid_writes(gap,phase,true,2);
        rapid_writes(gap,phase,true,3);
        cancel_then_write(gap,phase);
    }
    for(unsigned phase=0;phase<8;++phase) for(unsigned which=0;which<3;++which)
        invalid_history(which,phase);
    fault_record(); edge_case(3,65536,0); edge_case(3,65536,-1);
    stalled_release(IDLE);
    stalled_release(RESET | (UINT64_C(14)<<16)); /* OE ends before CS0 */
    stalled_release(RESET | (UINT64_C(2)<<16));  /* WE low during read */
    stalled_release(UINT64_C(10)<<16);         /* RESET held low */
    stalled_release(RESET | (UINT64_C(8)<<16)); /* conflicting selects */
    printf("CS1 prototype: %u mixed read phase/gap cases, late replies, stalled release, disarm and paired write snapshots PASS\n",cases);
    puts("1000 functional mixed iterations: 3000 reads /1000 writes PASS (C CPU model, not ARM timing)");
    puts("Write-ending regression: legal zero-hold commits; early CS1 cancellation retires without commit");
    puts("900 ending phase/order cases;192 rapid-write/pressure cases;64 cancel-next-epoch cases;24 sticky control faults; stale/malformed outcomes PASS");
    return 0;
}
