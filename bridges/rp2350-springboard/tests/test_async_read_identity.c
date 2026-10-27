/* The revised write rendezvous must not change the shared read-owner engine. */
#include <apio.h>
#include <epio.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
void sb_cs1_program_init(bool);
void sb_async_program_init(bool);
int main(void) {
 unsigned short words[29];epio_t *e;
 sb_cs1_program_init(false);e=epio_init();if(!e)return 1;epio_update_from_apio(e);
 for(unsigned i=0;i<29;++i)words[i]=epio_get_instr(e,1,i);
 epio_free(e);sb_async_program_init(false);e=epio_init();if(!e)return 1;epio_update_from_apio(e);
 for(unsigned i=0;i<29;++i)if(words[i]!=epio_get_instr(e,1,i)){fprintf(stderr,"read engine differs at%u\n",i);return 1;}
 epio_free(e);puts("PASS: all29 read-owner/release instruction words unchanged");return 0;
}
