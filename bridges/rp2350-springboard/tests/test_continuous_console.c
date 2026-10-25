#include "continuous_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"console check failed: %s\n",#x);exit(1);}}while(0)
int main(void) {
    struct sb_continuous_status s={.rom_ready=true,.ready=true,.stopped=true,
        .run=UINT32_MAX,.requests=UINT32_MAX,.posted=UINT32_MAX,.completed=UINT32_MAX,
        .attempted=UINT32_MAX,.cancelled=UINT32_MAX,.empty_ends=UINT32_MAX};
    char line[128],whole[384]={0};
    for(unsigned page=0;page<3;++page) {
        int n=sb_continuous_status_line(line,sizeof(line),page,&s);
        CHECK(n>0 && n<128 && (size_t)n==strlen(line));strcat(whole,line);
    }
    CHECK(strstr(whole,"run=4294967295 limit=0 continuous=1"));
    CHECK(strstr(whole,"attempted=4294967295\r\n"));
    CHECK(strstr(whole,"empty_ends=4294967295\r\n"));
    s.counters_final=true;
    CHECK(sb_continuous_status_line(line,sizeof(line),2,&s)>0 && strlen(line)<128);
    CHECK(strstr(line,"counters_final=1\r\n"));
    char tiny[8];CHECK(sb_continuous_status_line(tiny,sizeof(tiny),2,&s)>=(int)sizeof(tiny));
    CHECK(tiny[sizeof(tiny)-1]==0);
    CHECK(sb_continuous_status_line(line,sizeof(line),3,&s)==-1);
    puts("Continuous status: all fields preserved, UINT32_MAX pages under128bytes, truncation detectable PASS");
    return 0;
}
