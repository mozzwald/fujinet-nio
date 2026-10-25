#include "continuous_console.h"
#include <stdio.h>
int sb_continuous_status_line(char *buffer, size_t size, unsigned page,
                              const struct sb_continuous_status *s) {
    if(page==0)return snprintf(buffer,size,
        "rom_ready=%u ready=%u stopped=%u arm_pending=%u active=%u run=%lu limit=0 continuous=1\r\n",
        s->rom_ready,s->ready,s->stopped,s->arm_pending,s->active,(unsigned long)s->run);
    if(page==1)return snprintf(buffer,size,
        "requests=%lu posted=%lu completed=%lu attempted=%lu\r\n",
        (unsigned long)s->requests,(unsigned long)s->posted,
        (unsigned long)s->completed,(unsigned long)s->attempted);
    if(page==2)return snprintf(buffer,size,"cancelled=%lu empty_ends=%lu%s\r\n",
        (unsigned long)s->cancelled,(unsigned long)s->empty_ends,
        s->counters_final?" counters_final=1":"");
    return -1;
}
