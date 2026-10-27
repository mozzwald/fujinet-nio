/* Experimental combined service; frozen persistent_reply.S is untouched.
 * Experimental scheduling checkpoint: NOT a firmware target. Timing gate
 * remains closed. R10 caches the write epoch; R12 packs the low 30 completion
 * bits above the active-worker flag. SRAM holds the upper two count bits.
 * Every completion and write outcome is consumed once on Core1.
 */
#include "cs1_engine.h"

.syntax unified
.thumb
.section .text.sb_cs1_reply,"ax",%progbits
.p2align 2
.global sb_cs1_reply
.type sb_cs1_reply,%function
.thumb_func
sb_cs1_reply:
    push {r4-r11,lr}
    sub sp,sp,#12
    add r0,r0,#32
    ldr r3,[r2]
    ldr r4,[r2,#4]
    ldr r11,[r2,#8]
    ldr r5,=0x1f700000
    ldr r6,[r0,#-8]
    ldr r7,[r0,#-4]
    adr r2,.Lnext_request+1
    str r2,[sp,#4]
    ldr r10,[r0,#28]
    ldr r12,[r0,#-24]
    and r2,r12,#0xc0000000
    str r2,[r0,#60]
    lsl r12,r12,#2
.Lnext_request:
    ldrb r2,[r11]
    cbz r2,.Lzero_1
    b .Lexit_idle
.Lzero_1:
.Lwait_request:
    ldr r2,[r3,#0x16c]
    tst r2,#1
    bne .Lrequest
.Lhint_return_0:
    tst r2,#0x1a00
    bne .Lnew_work
.Lidle_poll_1:
    ldr r2,[r3,#0x16c]
    tst r2,#1
    bne .Lrequest
.Lhint_return_1:
    tst r2,#0x1a00
    bne .Lnew_work
.Lidle_poll_2:
    ldr r2,[r3,#0x16c]
    tst r2,#1
    bne .Lrequest
.Lhint_return_2:
    tst r2,#0x1a00
    bne .Lnew_work
.Lidle_poll_3:
    ldr r2,[r3,#0x16c]
    tst r2,#1
    bne .Lrequest
.Lhint_return_3:
    tst r2,#0x1a00
    bne .Lnew_work
    ldrb r2,[r11]
    cmp r2,#0
    beq .Lwait_request
    b .Lexit_idle
.Lcancel_hint_0:
    ldr lr,[r4]
    ubfx lr,lr,#16,#4
    cmp lr,#5
    beq .Lnew_work
    tst lr,#4
    beq .Ltight_read
    b .Lhint_return_0
.Lcancel_hint_1:
    ldr lr,[r4]
    ubfx lr,lr,#16,#4
    cmp lr,#5
    beq .Lnew_work
    tst lr,#4
    beq .Ltight_read
    b .Lhint_return_1
.Lcancel_hint_2:
    ldr lr,[r4]
    ubfx lr,lr,#16,#4
    cmp lr,#5
    beq .Lnew_work
    tst lr,#4
    beq .Ltight_read
    b .Lhint_return_2
.Lcancel_hint_3:
    ldr lr,[r4]
    ubfx lr,lr,#16,#4
    cmp lr,#5
    beq .Lnew_work
    tst lr,#4
    beq .Ltight_read
    b .Lhint_return_3
.Ltight_read:
    ldr r2,[r3,#0x16c]
    tst r2,#1
    bne .Lrequest
    ldr r2,[r3,#0x16c]
    tst r2,#1
    bne .Lrequest
    ldr r2,[r3,#0x16c]
    tst r2,#1
    bne .Lrequest
    ldr r2,[r3,#0x16c]
    tst r2,#1
    bne .Lrequest
    ldrb r2,[r11]
    cbz r2,.Ltight_continue
    b .Lexit_idle
.Ltight_continue:
    ldr r2,[r3,#0x16c]
    tst r2,#0x1a00
    bne .Lnew_work
    b .Lwait_request
.Lnew_work:
    tst r2,#0x800
    bne .Lnew_fault
    adr lr,.Lrequest+1
    b .Lstart_notified
.Lnew_fault:
    b .Lcontrol_fault
.Lresume_work:
    ldrb r2,[r11]
    cbz r2,.Lzero_2
    b .Lexit_idle
.Lzero_2:
    ldr r2,[r3,#0x16c]
    tst r2,#1
    bne .Lrequest
    adr lr,.Lrequest+1
    adr r2,.Lnext_request+1
    str r2,[sp,#4]
    b sb_cs1_write_step
.Lrequest:
    ldr r8,[r3,#0x20]
.global sb_cs1_sample_address
sb_cs1_sample_address:
    ldr r9,[r4]              /* adjacent FIFO/SIO capture is preserved */
    and r2,r8,#15
    tst r8,#0x20000000
    beq .Lreject
    cmp r2,#10
    bne .Lcs1
    ubfx r2,r9,#1,#15
    /* Useful decode above spans the modeled autonomous guard latency.
     * The sticky ended-owner flag is checked before any SRAM lookup. */
    ldr lr,[r3,#0x30]
    tst lr,#9
    bne .Lreject
    tst r8,r5
    ite ne
    movwne lr,#0xffff
    ldrheq lr,[r1,r2,lsl #1]
    str lr,[r3,#0x10]
    b .Lposted
.Lcs1:
    cmp r2,#9
    bne .Lreject
    /* Positive packed address: any upper address or offset>6 saturates
     * to RESERVED(FFFF). This cannot alias an invalid address to SCRATCH. */
    and lr,r8,r5
    bfi lr,r9,#0,#16
    ubfx r2,lr,#1,#28
    usat r2,#2,r2
    and r8,r12,#1
    ldr lr,[r3,#0x30]
    orr lr,lr,r8,lsl #1
    tst lr,#27
    bne .Lcs1_pending
.Ltable:
    ldrh lr,[r0,r2,lsl #1]
    str lr,[r3,#0x10]
    b .Lposted
.Lcs1_pending:
    tst lr,#9
    bne .Lreject
.Ldrain:
    str r2,[sp]             /* immutable captured index, never reread SIO */
    movs lr,#32
    str lr,[r0,#52]
    adr lr,.Lunexpected_rx+1
    adr r2,.Ldrained+1
    str r2,[sp,#4]
    b sb_cs1_write_step
.Lunexpected_rx:
    movs lr,#1
    str lr,[r0,#-12]
    strb lr,[r11]
    b .Lreject
.Ldrained:
    adr r2,.Lnext_request+1
    str r2,[sp,#4]
    ldrb lr,[r11]
    cmp lr,#0
    bne .Lreject
    ldr r2,[sp]
    ldr lr,[r3,#0x30]
    tst lr,#9
    bne .Lreject
    b .Ltable
.Lffff:
    eor r2,r8,r9,lsr #16
    tst r2,#15
    bne .Lreject
    ldr lr,[r3,#0x30]
    tst lr,#9
    bne .Lreject
    movw lr,#0xffff
    str lr,[r3,#0x10]
    b .Lposted
.Lreject:
    movs lr,#1
    str lr,[r3,#0x34]
    movs lr,#0
    str lr,[r3,#0x10]        /* one explicitly cancelled old-owned dummy */
    ldr r8,[r0,#56]
    adds r8,#1
    str r8,[r0,#56]
.Lposted:
.Lwait_completion:
    ldr r2,[r3,#4]
    tst r2,#0x100
    beq .Ltake_completion
    ldrb r2,[r11]
    cmp r2,#0
    beq .Lwait_completion
    b .Lexit_waiting
.Lwork_empty:
    adr r2,.Lnext_request+1
    str r2,[sp,#4]
    b .Lwait_completion
.Lwork_completion:
    ldr r2,[r3,#4]
    tst r2,#0x100
    beq .Ltake_work_completion
    ldrb r2,[r11]
    cbz r2,.Lzero_3
    b .Lexit_waiting
.Lzero_3:
    adr lr,.Ltake_work_completion+1
    adr r2,.Lwork_empty+1
    str r2,[sp,#4]
    b sb_cs1_write_step
.macro completed target,tag
    adds r12,r12,#4
    bcc \target
.Lcarry\tag:
    ldr r9,[r0,#60]
    add r9,r9,#0x40000000
    str r9,[r0,#60]
    b \target
.endm
.Ltake_work_completion:
    ldr r2,[r3,#0x20]
    cmn r2,#1
    bne .Lwork_other
    completed .Lresume_work,work
.Lwork_other:
    cbz r2,.Lzero_4
    b .Lbad_completion
.Lzero_4:
    ldr r9,[r0,#-16]
    adds r9,#1
    str r9,[r0,#-16]
    completed .Lresume_work,workcancel
.Ltake_completion:
    ldr r2,[r3,#0x20]
    cmn r2,#1
    bne .Lother
    tst r12,#1
    bne .Ltake_done_work
    completed .Lnext_request,normal
.Ltake_done_work:
    completed .Lresume_work,normalwork
.Lother:
    cbz r2,.Lzero_5
    b .Lbad_completion
.Lzero_5:
    ldr r9,[r0,#-16]
    adds r9,#1
    str r9,[r0,#-16]
    completed .Lnext_request,normalcancel
.Lbad_completion:
    movs r2,#1
    str r2,[r0,#-12]
.Lexit_waiting:
    movs r2,#1
    b .Lexit
.Lexit_idle:
    movs r2,#0
.Lexit:
    str r10,[r0,#28]
    movs lr,#1
    str lr,[r3,#0x34]
    lsr r9,r12,#2
    ldr lr,[r0,#60]
    orr r9,r9,lr
    str r9,[r0,#-24]
    add r2,r2,r9
    str r2,[r0,#-32]
    str r2,[r0,#-28]
    ldr lr,[r0,#-16]
    sub r9,r9,lr
    str r9,[r0,#-20]
    rsb r9,r10,#0
    str r9,[r0,#12]
    ldr lr,[r0,#20]
    sub r9,r9,lr
    ldr lr,[r0,#24]
    sub r9,r9,lr
    str r9,[r0,#16]
    ldr r9,[r0,#8]
    cmp r9,#0
    beq .Lreturn
    ldr r9,[r0,#24]
    adds r9,#1
    str r9,[r0,#24]
.Lreturn:
    add sp,sp,#12
    pop {r4-r11,pc}
.size sb_cs1_reply,.-sb_cs1_reply
.ltorg


/* r8=data, r9=decoded index plus backlog flag, r10=cached next epoch,
 * r2=scratch, LR=read-ready consumer. Preparation spills only on preemption.
 * IRQ1 is acknowledged before the look-ahead; later producers cannot be lost.
 */
.macro yield_read phase
    ldr r2,[r3,#0x16c]
    tst r2,#1
    beq .Lcontinue\@
    .if \phase>=2 && \phase<=4
    str r8,[r0,#40]
    str r9,[r0,#36]
    .endif
    movs r2,#\phase
    str r2,[r0,#32]
    bx lr
.Lcontinue\@:
.endm
.macro faults
    ldr r2,[r6,#8]
    tst r2,#4
    bne .Lfifo_fault
    ldr r2,[r7,#8]
    tst r2,#6
    bne .Lfifo_fault
    ldr r2,[r7,#0x30]
    tst r2,#32
    bne .Lcontrol_fault
.endm
.p2align 2
.global sb_cs1_write_step
.type sb_cs1_write_step,%function
.thumb_func
sb_cs1_write_step:
    ldr r2,[r0,#32]
    cmp r2,#4
    bhi .Lepoch_fault
    tbh [pc,r2,lsl #1]
.Ldispatch:
    .hword (.Lstart-.Ldispatch)/2
    .hword (.Lstart-.Ldispatch)/2
    .hword (.Lepoch_fault-.Ldispatch)/2
    .hword (.Lrestore_outcome-.Ldispatch)/2
    .hword (.Lrestore_commit-.Ldispatch)/2
.Lrestore_outcome:
    ldr r8,[r0,#40]
    ldr r9,[r0,#36]
    b .Loutcome
.Lrestore_commit:
    ldr r8,[r0,#40]
    ldr r9,[r0,#36]
    b .Lcommit
.Lstart:
    orr r12,r12,#1
    movs r2,#2
    str r2,[r3,#0x30]
    ldr r2,[r6,#0xc]
    ubfx r2,r2,#20,#4
    cbnz r2,.Llow_ready_1
    b .Lempty_low
.Llow_ready_1:
    ldr r8,[r7,#0xc]
    ubfx r8,r8,#12,#4
    cmp r8,#0
    beq .Lpartial
    b .Lpop_starts
.Lstart_notified:
    orr r12,r12,#1
    movs r2,#2
    str r2,[r3,#0x30]
    ldr r2,[r6,#0xc]
    ubfx r2,r2,#20,#4
    cbnz r2,.Llow_ready_2
    b .Lempty_low
.Llow_ready_2:
    ldr r8,[r7,#0xc]
    ubfx r8,r8,#12,#4
    cmp r8,#0
    beq .Lpartial
.Lpop_starts:
    movs r2,#18
    str r2,[r3,#0x30]
    ldr r9,[r6,#0x28]
    ldr r8,[r7,#0x24]
    ldr r2,[r7,#0x28]
    cmp r2,r10
    bne .Lepoch_fault
    tst r8,r5
    ite ne
    movwne r9,#0xffff
    ubfxeq r9,r9,#1,#15
    ubfx r8,r8,#4,#16
    yield_read 3
    b .Loutcome
.Loutcome:
    ldr r2,[r7,#4]
    tst r2,#0x400
    beq .Lhave_outcome
    yield_read 3
    ldr r2,[r7,#4]
    tst r2,#0x400
    beq .Lhave_outcome
    yield_read 3
    ldr r2,[r7,#4]
    tst r2,#0x400
    beq .Lhave_outcome
    yield_read 3
    ldr r2,[r7,#4]
    tst r2,#0x400
    beq .Lhave_outcome
    yield_read 3
    ldr r2,[r7,#4]
    tst r2,#0x400
    beq .Lhave_outcome
    yield_read 3
    ldr r2,[r7,#4]
    tst r2,#0x400
    beq .Lhave_outcome
    yield_read 3
    ldr r2,[r7,#4]
    tst r2,#0x400
    beq .Lhave_outcome
    yield_read 3
    ldr r2,[r7,#4]
    tst r2,#0x400
    beq .Lhave_outcome
    yield_read 3
.Lwait_outcome:
    yield_read 3
    ldrb r2,[r11]
    cbz r2,.Loutcome_stop_ok
    b .Labort_work
.Loutcome_stop_ok:
    ldr r2,[r3,#0x30]
    tst r2,#8
    bne .Lcontrol_fault
    adr r2,.Lunexpected_rx+1
    cmp lr,r2
    bne .Loutcome
    ldr r2,[r0,#52]
    subs r2,#1
    str r2,[r0,#52]
    bne .Loutcome
    b .Lepoch_fault
.Lhave_outcome:
    ldr r2,[r7,#0x28]
    cbz r2,.Loutcome_valid
    cmn r2,#1
    bne .Loutcome_fault
    orr r9,r9,#0x20000000
.Loutcome_valid:
    ldr r2,[r4]
    tst r2,#0x10000
    bne .Lcommit
.Lprecommit_yield:
    yield_read 4
.Lcommit:
    faults
.Lvalidated:
    tst r9,#0x20000000
    bne .Lcancel_write
    lsl r2,r9,#3
    cmp r2,#16
    bne .Lignore_write
    strh r8,[r0,#4]
    b .Laccepted
.Lcancel_write:
    ldr r2,[r0,#24]
    adds r2,#1
    str r2,[r0,#24]
    b .Laccepted
.Lignore_write:
    ldr r2,[r0,#20]
    adds r2,#1
    str r2,[r0,#20]
.Laccepted:
    sub r10,r10,#1
    /* Doorbells are hints, not proof that all FIFO streams were drained.
     * Acknowledge before the empty look-ahead, so later producers retain
     * their notification. Check every stream even after a CPU stall. */
    movs r2,#18
    str r2,[r3,#0x30]
    ldr r2,[r6,#4]
    tst r2,#0x400
    beq .Laccepted_pending
    ldr r2,[r7,#4]
    and r2,r2,#0x600
    cmp r2,#0x600
    bne .Laccepted_pending
    ldr r2,[r3,#0x30]
    tst r2,#18
    bne .Laccepted_pending
.Laccept_clean:
    movs r2,#0
    str r2,[r0,#32]
    bic r12,r12,#1
    ldr r2,[r3,#0x16c]
    tst r2,#1
    beq .Lclean_no_read
    bx lr
.Lclean_no_read:
    ldr pc,[sp,#4]
.Laccepted_pending:
    movs r2,#1
    str r2,[r0,#32]
    yield_read 1
    b .Lstart
.Lempty_low:
    ldr r2,[r7,#4]
    and r2,r2,#0x600
    cmp r2,#0x600
    bne .Lpartial
    faults
    ldr r2,[r3,#0x30]
    tst r2,#16
    beq .Lempty_recheck
    ldr r2,[r4]
    tst r2,#0x80000
    beq .Lpartial
.Lempty_recheck:
    movs r2,#18
    str r2,[r3,#0x30]
    ldr r8,[r6,#4]
    ldr r2,[r7,#4]
    lsr r8,r8,#10
    lsr r2,r2,#9
    and r8,r8,r2
    tst r8,#1
    beq .Lpartial
    /* Metadata was also empty before acknowledgement; recheck it last. */
    ldr r2,[r7,#4]
    tst r2,#0x400
    beq .Lpartial
    movs r2,#0
    str r2,[r0,#32]
    bic r12,r12,#1
    ldr pc,[sp,#4]
.Lpartial:
    ldr r2,[r7,#0x30]
    tst r2,#32
    bne .Lcontrol_fault
    orr r12,r12,#1
    yield_read 1
    ldrb r2,[r11]
    cbz r2,.Lzero_8
    b .Labort_work
.Lzero_8:
    b .Lstart
.Lepoch_fault:
    faults
    movs r2,#SB_CS1_ASM_EPOCH_FAULT
    b .Lfault
.Loutcome_fault:
    faults
    movs r2,#SB_CS1_ASM_OUTCOME_FAULT
    b .Lfault
.Lfifo_fault:
    movs r2,#SB_CS1_ASM_FIFO_FAULT
    b .Lfault
.Lcontrol_fault:
    movs r2,#SB_CS1_ASM_CONTROL_FAULT
.Lfault:
    str r2,[r0,#8]
    movs r2,#1
    str r2,[r0,#-12]
    strb r2,[r11]
.Labort_work:
    movs r2,#1
    str r2,[r3,#0x34]
    ldr pc,[sp,#4]
.size sb_cs1_write_step,.-sb_cs1_write_step
.section .note.GNU-stack,"",%progbits
