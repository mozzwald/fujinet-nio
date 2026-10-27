#include "async_boot.h"
#include <stdio.h>
#include <stdlib.h>
int main(void){
 for(unsigned bits=0;bits<128;++bits){bool used=bits&1;
  bool expected=!used&&(bits&2)&&(bits&4)&&(bits&8)&&!(bits&16)&&!(bits&32)&&!(bits&64);
  bool actual=sb_async_boot_request(&used,bits&2,bits&4,bits&8,bits&16,bits&32,bits&64);
  if(actual!=expected||used!=((bits&1)!=0||expected))return 1;
 }
 bool used=false;if(!sb_async_boot_request(&used,true,true,true,false,false,false))return 1;
 if(sb_async_boot_request(&used,true,true,true,false,false,false))return 1;
 puts("PASS one-shot boot:128 readiness/fault states and no retry after stop");return 0;
}
