/* SRAM ROM response kernel. No calls, logs, counters or completion-IRQ reads
 * between RX-ready detection and TX publication. All saved registers and
 * constants are prepared before waiting for a request. PIO owns bus safety. */
.syntax unified
.thumb
.section .text.sb_rx_first_reply,"ax",%progbits
.p2align 2
.global sb_rx_first_reply
.type sb_rx_first_reply,%function
.thumb_func
sb_rx_first_reply:
    push {r4-r11}
    ldr r3, [r2, #0]             /* PIO register base */
    ldr r4, [r2, #4]             /* SIO GPIO_IN address */
    ldr r2, [r2, #8]             /* core-0 stop request */
    ldr r5, =0x1f700000          /* A16..A23 in the high-window snapshot */
    ldr r6, =0x2000000f          /* RESET and four bus controls */
    ldr r7, =0x2000000a          /* RESET high, CS0 low, CS1 high, OE low, WE high */
.Lwait_rx:
    ldr r11, [r3, #4]           /* FSTAT */
    tst r11, #0x100             /* RXEMPTY SM0 */
    beq .Lreply
    ldrb r11, [r2]
    cmp r11, #0
    beq .Lwait_rx
    movs r0, #0
    pop {r4-r11}
    bx lr
.Lreply:
    ldr r8, [r3, #0x20]         /* RXF0: accepted request snapshot */
    ldr r9, [r4]                /* low address, sampled before lookup */
    tst r8, r5
    itee ne
    movwne r10, #0xffff         /* do not alias an address above 64 KiB */
    ubfxeq r11, r9, #1, #15     /* A1..A15 -> native word index */
    ldrheq r10, [r1, r11, lsl #1]
    and r11, r8, r6
    cmp r11, r7
    it eq
    streq r10, [r3, #0x10]      /* TXF0; requalification/drive stay in PIO */
    /* Everything below happens after publication. STR instructions preserve
     * the CMP flags, so valid records the same predicate as the FIFO write. */
    str r8, [r0, #0]
    str r9, [r0, #4]
    strh r10, [r0, #8]
    ite eq
    moveq r11, #1
    movne r11, #0
    strb r11, [r0, #10]
    movs r0, #1
    pop {r4-r11}
    bx lr
.size sb_rx_first_reply, .-sb_rx_first_reply
.ltorg
.section .note.GNU-stack,"",%progbits
