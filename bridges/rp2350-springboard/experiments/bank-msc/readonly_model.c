/* Test-only adapter. Candidate contains NO write worker or retirement loop. */
#define sb_async_co_init sb_bank_fixture_init
#define sb_async_co_retire sb_bank_fixture_retire
#include "../../tests/async_cosim.c"
#undef sb_async_co_init
#undef sb_async_co_retire
void sb_async_co_init(void) {
    sb_bank_fixture_init();
    table[VMB_CAPABILITIES/2]=0;table[VMB_STATUS/2]=VMB_ST_ERROR;
    table[VMB_ERROR/2]=VMB_ERR_UNSUPPORTED;
}
void sb_async_co_retire(void) { /* intentionally absent in candidate */ }
unsigned sb_bank_model_write_depth(void) {
    return epio_rx_fifo_depth(co.e,0,2)+epio_rx_fifo_depth(co.e,2,1)+epio_rx_fifo_depth(co.e,2,2);
}
