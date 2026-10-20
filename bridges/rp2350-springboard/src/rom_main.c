#include "bus_model.h"
#include "rom_program.h"
#include "rom_image.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#if SB_USB_DEBUG
#include "pico/stdio_usb.h"
#include <stdarg.h>
#include <stdio.h>
#endif

_Static_assert(PICO_DEFAULT_LED_PIN == 39, "Keep LED outside bus");
enum { READ_LIMIT = 8 };
/* SRAM table is big-endian ROM bytes converted once to native bus words.
 * Every unused entry is FFFF; no allocation, swapping or CRC in hot path. */
static uint8_t rom_bytes[SB_ROM_CAPACITY];
static uint16_t rom_words[SB_ROM_CAPACITY / 2];
struct reply_record { uint32_t address, high; uint16_t word; bool posted; };
static struct reply_record records[READ_LIMIT];
static volatile bool rom_ready, ready, arm, stop, stopped = true;
static volatile uint32_t requested, posted, completed, runs;

static void data_input_only(void) {
    /* Only used while the SM has parked after MOV PINDIRS,NULL. */
    for (unsigned pin = 20; pin <= 35; ++pin) {
        gpio_set_dir(pin, GPIO_IN);
        gpio_set_function(pin, GPIO_FUNC_SIO);
    }
}

static void rom_core(void) {
    /* USB and all SDK/interrupt work stay on core 0. Independent PIO polling
     * remains live if this core stalls before or after a FIFO publication. */
    (void)save_and_disable_interrupts();
    ready = true;
    for (;;) {
        while (!arm) tight_loop_contents();
        if (!rom_ready) { arm = false; continue; }
        requested = posted = completed = 0;
        stop = false;
        pio_sm_set_enabled(pio1, 0, false);
        pio_sm_clear_fifos(pio1, 0);
        pio_sm_restart(pio1, 0);
        pio_interrupt_clear(pio1, 0);
        sb_rom_program_init(false);
        ++runs;
        stopped = false;
        __dmb();
        arm = false;
        pio_sm_set_enabled(pio1, 0, true);
        for (;;) {
            if (pio1->irq & 1) {
                /* PIO sets this only AFTER output release. The same core
                 * publishes replies and acknowledges: no late concurrent
                 * publisher can put a word after this FIFO flush. */
                pio_sm_clear_fifos(pio1, 0);
                ++completed;
                if (stop || completed >= READ_LIMIT) break;
                pio_interrupt_clear(pio1, 0);
            } else if (!(pio1->fstat & (1u << PIO_FSTAT_RXEMPTY_LSB))) {
                const uint32_t high = pio1->rxf[0];
                const uint32_t low = sio_hw->gpio_in;
                /* Address is held through a read. If it ended before this
                 * low-window sample, PIO's sticky park rejects the reply.
                 * That guarantee requires a visible deselect interval; see
                 * ROM_TEST.md for the sampled-guard timing restriction. */
                const uint16_t word = (high & 0x1f700000u) ? 0xffff :
                                      rom_words[(low & 0xfffeu) >> 1];
                const bool valid = (high & 0x2000000fu) == 0x2000000au;
                if (valid) {
                    pio1->txf[0] = word;
                    ++posted;
                }
                const uint32_t address = sb_address(low, high);
                const uint32_t index = requested++;
                if (index < READ_LIMIT)
                    records[index] = (struct reply_record){address, high, word, valid};
            } else if (stop) {
                /* No request yet: disabling cannot strand driven pins.
                 * Once requested>completed we instead wait for PIO release. */
                if (requested == completed) break;
            }
        }
        pio_sm_set_enabled(pio1, 0, false);
        data_input_only();
        __dmb();
        stopped = true;
    }
}

#if SB_USB_DEBUG
static void emit(const char *format, ...) {
    char line[192];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length > 0)
        stdio_put_string(line, length < (int)sizeof(line) ? length : (int)sizeof(line) - 1,
                         false, false);
}
static void console(uint64_t now) {
    static uint64_t next, announce;
    static bool connected, dumping;
    static unsigned index;
    if (now < next) return;
    next = now + 10000;
    if (!stdio_usb_connected()) { connected = false; dumping = false; return; }
    if (!connected) { connected = true; announce = now + 250000; return; }
    if (announce) {
        if (now < announce) return;
        announce = 0;
        emit("FujiSpring EXPERIMENTAL CS0 ROM / 8 reads / 150MHz\r\n"
             "ROM=%u CRC32=%08lx; starts DISARMED\r\n"
             "h=help a=arm s=status x=stop d=replies\r\n",
             SB_ROM_IMAGE_SIZE, (unsigned long)SB_ROM_IMAGE_CRC);
        return;
    }
    switch (getchar_timeout_us(0)) {
    case 'h':
        emit("a=arm for ONE app Read (8 cycles); no Pulse/reset/reinsert\r\n"); break;
    case 'a':
        if (rom_ready && ready && stopped && !arm) {
            dumping = false;
            __dmb(); arm = true;
            emit("arm requested; s must show stopped=0 before app Read\r\n");
        } else emit("arm refused: require ROM ready, stopped=1, arm_pending=0\r\n");
        break;
    case 's':
        emit("rom_ready=%u ready=%u stopped=%u arm_pending=%u run=%lu "
             "requests=%lu posted=%lu completed=%lu\r\n",
             rom_ready, ready, stopped, arm, (unsigned long)runs,
             (unsigned long)requested, (unsigned long)posted, (unsigned long)completed);
        break;
    case 'x':
        if (!arm) { stop = true; emit("stop requested; s must confirm stopped=1\r\n"); }
        else emit("wait for arm_pending=0\r\n");
        break;
    case 'd':
        if (stopped && !arm) {
            __dmb(); index = 0; dumping = true;
            emit("CPU replies only: posted does NOT prove timely data at Prism\r\n");
        } else emit("dump requires stopped=1 arm_pending=0\r\n");
        break;
    default: break;
    }
    if (dumping) {
        if (index >= requested || index >= READ_LIMIT) {
            dumping = false; emit("replies end\r\n");
        } else {
            const struct reply_record r = records[index];
            emit("%u address=%06lx high=%08lx word=%04x posted=%u\r\n",
                 index++, (unsigned long)r.address, (unsigned long)r.high, r.word, r.posted);
        }
    }
}
#endif

int main(void) {
    const uint64_t mask = ((UINT64_C(1) << 47) - 1) & ~(UINT64_C(1) << 39);
    gpio_init_mask64(mask);
    gpio_set_dir_in_masked64(mask);
    for (unsigned pin = 0; pin <= 46; ++pin)
        if (pin != 39) gpio_disable_pulls(pin);
    gpio_init(39); gpio_put(39, true); gpio_set_dir(39, GPIO_OUT);
    set_sys_clock_khz(150000, true);
    rom_ready = sb_load_rom(rom_bytes, sizeof(rom_bytes), sb_rom_image,
                            SB_ROM_IMAGE_SIZE, SB_ROM_IMAGE_CRC);
    if (rom_ready)
        for (unsigned i = 0; i < SB_ROM_CAPACITY / 2; ++i)
            rom_words[i] = sb_rom_word(rom_bytes, SB_ROM_IMAGE_SIZE, i * 2);
    multicore_launch_core1(rom_core);
#if SB_USB_DEBUG
    stdio_usb_init();
#endif
    for (;;) {
        const uint64_t now = time_us_64();
#if SB_USB_DEBUG
        console(now);
#endif
        gpio_put(39, stopped || now % 1000000 < 100000);
        tight_loop_contents();
    }
}
