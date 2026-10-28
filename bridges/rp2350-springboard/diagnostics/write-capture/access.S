.text
.even
.globl fw_write_word
fw_write_word:
 move.l 4(%sp),%a0
 move.w 8(%sp),(%a0)
 rts
.globl fw_pc
fw_pc:
 lea fw_pc(%pc),%a0
 move.l %a0,%d0
 rts
