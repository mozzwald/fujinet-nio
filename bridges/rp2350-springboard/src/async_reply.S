/* Phase4 isolated read service. Core1 never drains write FIFOs.
 * Core0 owns completed writes and publishes atomic CS1 table halfwords. */
/* r12 counts validated completions; LR counts TX publications. */
.syntax unified
.thumb
.section .text.sb_async_reply,"ax",%progbits
.p2align 2
.global sb_async_reply
.global sb_cs1_reply
.global sb_cs1_sample_address
.type sb_async_reply,%function
.thumb_func
sb_cs1_reply:
sb_async_reply:
    push {r4-r11, lr}
    ldr r3, [r2, #0]
    ldr r4, [r2, #4]
    ldr r2, [r2, #8]
    ldr r5, =0x1f700000
    ldr r6, [r0, #24]       /* immutable CS1 read table pointer */
    mov r12, #0
    mov lr, #0                 /* request total; return address is on stack */
.Lnext_request:
    ldrb r11, [r2]
    cmp r11, #0
    bne .Lexit
.Lwait_request:
    ldr r11, [r3, #4]
    tst r11, #0x100
    beq .Lrequest
    ldrb r11, [r2]
    cmp r11, #0
    beq .Lwait_request
    b .Lexit
.Lrequest:
    /* Preserve the baseline's adjacent high-FIFO/low-SIO captures and lookup. */
    ldr r8, [r3, #0x20]
sb_cs1_sample_address:
    ldr r9, [r4]
    /* Reject expired ownership before either table lookup. IRQ0 records
     * deselection/cancellation. IRQ3 is fault history, not ownership:
     * a completed old fault must not suppress future qualified ROM reads. */
    tst r8, #0x20000000
    beq .Linvalid_snapshot
    and r7, r8, #15
    tbb [pc,r7]
.Lselect_table:
    .byte (.Linvalid_snapshot-.Lselect_table)/2
    .byte (.Linvalid_snapshot-.Lselect_table)/2
    .byte (.Linvalid_snapshot-.Lselect_table)/2
    .byte (.Linvalid_snapshot-.Lselect_table)/2
    .byte (.Linvalid_snapshot-.Lselect_table)/2
    .byte (.Linvalid_snapshot-.Lselect_table)/2
    .byte (.Linvalid_snapshot-.Lselect_table)/2
    .byte (.Linvalid_snapshot-.Lselect_table)/2
    .byte (.Linvalid_snapshot-.Lselect_table)/2
    .byte (.Lcs1-.Lselect_table)/2
    .byte (.Lrom-.Lselect_table)/2
    .byte (.Linvalid_snapshot-.Lselect_table)/2
    .byte (.Linvalid_snapshot-.Lselect_table)/2
    .byte (.Linvalid_snapshot-.Lselect_table)/2
    .byte (.Linvalid_snapshot-.Lselect_table)/2
    .byte (.Linvalid_snapshot-.Lselect_table)/2
.Lcs1:
    ldr r10, [r3, #0x30]
    tst r10, #1
    bne .Linvalid_snapshot
    tst r8, r5
    itee ne
    movwne r10, #0xffff
    ubfxeq r11, r9, #1, #15
    ldrheq r10, [r6, r11, lsl #1]
    str r10, [r3, #0x10]
    b .Lposted
.Lrom:
    ldr r10, [r3, #0x30]
    tst r10, #1
    bne .Linvalid_snapshot
    /* The established native-word ROM lookup sequence is retained. */
    tst r8, r5
    itee ne
    movwne r10, #0xffff
    ubfxeq r11, r9, #1, #15
    ldrheq r10, [r1, r11, lsl #1]
    str r10, [r3, #0x10]
.Lposted:
    add lr,lr,#1
    ldrb r11,[r2]
    cmp r11,#0
    bne .Lexit
.Lwait_completion:
    ldr r11,[r3,#4]
    tst r11,#0x100
    beq .Ltake_completion
    ldrb r11,[r2]
    cmp r11,#0
    beq .Lwait_completion
    b .Lexit
.Ltake_completion:
    ldr r10,[r3,#0x20]
    cmn r10,#1
    bne .Lother_completion
    add r12,r12,#1
    b .Lwait_request
.Lother_completion:
    cmp r10,#0
    bne .Lframing_fault
    ldr r9,[r0,#16]
    adds r9,#1
    str r9,[r0,#16]
    add r12,r12,#1
    b .Lwait_request
.Linvalid_snapshot:
    /* Exactly one old-owned dummy unblocks PULL, with sticky cancellation. */
    movs r10, #1
    str r10, [r3, #0x34]
    movs r10, #0
    str r10, [r3, #0x10]
    ldr r8, [r0, #4]
    adds r8, #1
    str r8, [r0, #4]
    b .Lposted
.Lframing_fault:
    movs r8,#1
    str r8,[r0,#20]
.Lexit:
    str lr,[r0]
    str lr,[r0,#4]
    str r12,[r0,#8]
    ldr r8,[r0,#16]
    sub r8,r12,r8
    str r8,[r0,#12]
    pop {r4-r11,pc}
.size sb_async_reply, .-sb_async_reply
.ltorg
.section .note.GNU-stack,"",%progbits
