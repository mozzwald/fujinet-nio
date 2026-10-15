#include "bus_model.h"
#include "capture_program.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "rom_image.h"

/* SWD symbols. Stop capture via sb_debug_stop, then wait for sb_debug_stopped
 * before reading records. No stdio/UART uses Springboard GPIOs. */
volatile bool sb_debug_stop, sb_debug_stopped;
volatile uint32_t sb_debug_rom_ready, sb_debug_fifo_stalls[2];
volatile uint32_t sb_debug_gpio_low, sb_debug_gpio_high;
struct sb_trace sb_debug_trace;
uint8_t sb_rom_sram[SB_ROM_CAPACITY];

static void __not_in_flash_func(capture_core)(void) {
    uint32_t sequence[2][2] = {{0}};
    sb_capture_program_init();
    while (!sb_debug_stop) {
        for (unsigned block = 0; block < 2; ++block) {
            PIO pio = block ? pio1 : pio0;
            sb_debug_fifo_stalls[block] |= pio->fdebug & 0xfu;
            for (unsigned sm = 0; sm < 2; ++sm) {
                if (!pio_sm_is_rx_fifo_empty(pio, sm)) {
                    struct sb_capture record = {.sample = pio_sm_get(pio, sm),
                                                .sequence =
                                                    sequence[block][sm]++,
                                                .window = (uint8_t)(block * 16),
                                                .cs = (uint8_t)sm};
                    sb_trace_push(&sb_debug_trace, record);
                }
            }
        }
    }
    pio_set_sm_mask_enabled(pio0, 3, false);
    pio_set_sm_mask_enabled(pio1, 3, false);
    __dmb();
    sb_debug_stopped = true;
    for (;;)
        tight_loop_contents();
}

int main(void) {
    /* Disable all bus output enables before clock changes or copying the ROM.
     * Leave GP39 and GP47 entirely alone (LED and B1/B2 PSRAM CS). */
    const uint64_t mask = ((UINT64_C(1) << 47) - 1) & ~(UINT64_C(1) << 39);
    gpio_init_mask64(mask);
    gpio_set_dir_in_masked64(mask);
    for (unsigned pin = 0; pin <= 46; ++pin)
        if (pin != 39)
            gpio_disable_pulls(pin);
    set_sys_clock_khz(150000, true);
    sb_debug_rom_ready =
        sb_load_rom(sb_rom_sram, sizeof(sb_rom_sram), sb_rom_image,
                    SB_ROM_IMAGE_SIZE, SB_ROM_IMAGE_CRC);
    multicore_launch_core1(capture_core);
    for (;;) {
        /* These two live samples are intentionally not claimed atomic. */
        sb_debug_gpio_low = sio_hw->gpio_in;
        sb_debug_gpio_high = sio_hw->gpio_hi_in;
        tight_loop_contents();
    }
}
