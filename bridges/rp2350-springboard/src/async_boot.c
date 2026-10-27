#include "async_boot.h"
bool sb_async_boot_request(bool *requested,bool rom_ok,bool ready,bool stopped,
                           bool pending,bool active,bool fault) {
 if(*requested||!rom_ok||!ready||!stopped||pending||active||fault)return false;
 *requested=true;return true;
}
