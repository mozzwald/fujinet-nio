/* One-shot Core1 entry; no USB dependency or bus I/O. */
.syntax unified
.thumb
.section .text.sb_boot_autoarm_entry,"ax",%progbits
.p2align 2
.global sb_boot_autoarm_entry
.type sb_boot_autoarm_entry,%function
.thumb_func
sb_boot_autoarm_entry:
    ldr r0, =sb_boot_policy
    ldr r1, [r0, #0]
    ldrb r1, [r1]
    cbz r1, .Lservice
    ldr r1, [r0, #4]
    movs r2, #1
    dmb sy
    strb r2, [r1]
.Lservice:
    ldr r0, [r0, #8]
    bx r0
.size sb_boot_autoarm_entry, .-sb_boot_autoarm_entry
.ltorg
