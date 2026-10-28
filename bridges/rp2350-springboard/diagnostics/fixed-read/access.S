.text
.even
.globl df_read_word
df_read_word:
 move.l 4(%sp),%a0
 moveq #0,%d0
 move.w (%a0),%d0
 rts
.globl df_read_pair
df_read_pair:
 move.l 4(%sp),%a0
 move.l (%a0),%d0
 rts
.globl df_pc
df_pc:
 lea df_pc(%pc),%a0
 move.l %a0,%d0
 rts
