/* Isolated latency/constant-response experiment; baseline rom_main.c unchanged. */
#include "bus_model.h"
#include "latency_program.h"
#include <apio.h>
#include "rom_image.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#ifndef SB_LATENCY_RX_FIRST
#define SB_LATENCY_RX_FIRST 0
#endif
#if SB_LATENCY_RX_FIRST
#include "cpu_reply.h"
#endif
#if SB_USB_DEBUG
#include "pico/stdio_usb.h"
#include <stdarg.h>
#include <stdio.h>
#endif

_Static_assert(PICO_DEFAULT_LED_PIN == 39, "Keep LED outside bus");
enum { TRACE_LIMIT = 8 };
static volatile enum sb_latency_response selected_response = SB_LAT_CPU_ROM;
static volatile enum sb_latency_marker selected_marker = SB_LAT_DRIVE;
static enum sb_latency_response active_response;
static enum sb_latency_marker active_marker;
static const char *const response_names[] = {"cpu-rom", "cpu-zero", "cpu-ones", "pio-zero", "pio-ones"};
static const char *const marker_names[] = {"drive", "request", "reply", "preload"};
/* SRAM table is big-endian ROM bytes converted once to native bus words.
 * Every unused entry is FFFF; no allocation, swapping or CRC in hot path. */
static uint8_t rom_bytes[SB_ROM_CAPACITY];
static uint16_t rom_words[SB_ROM_CAPACITY / 2];
struct reply_record { uint32_t address, high; uint16_t word; bool posted; };
static struct reply_record records[TRACE_LIMIT];
struct anomaly_record { uint32_t index, address, high; bool enabled; };
static struct anomaly_record anomalies[TRACE_LIMIT];
static volatile bool rom_ready, ready, arm, stop, stopped = true;
static volatile uint32_t requested, posted, completed, runs;
static volatile uint32_t enabled, not_enabled, address_errors, anomaly_count;
/* Written before arm publication, immutable during a run. Full-image mode is
 * still bounded and intended for the RAM checker's tick-spaced reads only. */
static volatile uint32_t arm_limit = SB_LAT_WORDS, active_limit;

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
        enabled = not_enabled = address_errors = anomaly_count = 0;
        active_limit = arm_limit;
        stop = false;
        pio_sm_set_enabled(pio1, 0, false);
        pio_sm_clear_fifos(pio1, 0);
        pio_sm_restart(pio1, 0);
        pio_interrupt_clear(pio1, 0);
        pio_interrupt_clear(pio1, 1);
        active_response = selected_response;
        active_marker = selected_marker;
        /* Fill the same SRAM table before arm. CPU ROM/constant modes use
         * the identical lookup path; only table contents differ. */
        for (unsigned i = 0; i < SB_ROM_CAPACITY / 2; ++i)
            rom_words[i] = (active_response == SB_LAT_CPU_ZERO || active_response == SB_LAT_PIO_ZERO) ? 0 :
                (active_response == SB_LAT_CPU_ONES || active_response == SB_LAT_PIO_ONES) ? 0xffff :
                sb_rom_word(rom_bytes, SB_ROM_IMAGE_SIZE, i * 2);
        sb_latency_program_init(false, active_response, active_marker);
        ++runs;
        stopped = false;
        __dmb();
        arm = false;
        pio_sm_set_enabled(pio1, 0, true);
#if SB_LATENCY_RX_FIRST
        if (active_response <= SB_LAT_CPU_ONES) for (;;) {
            /* One request per released IRQ park. An accepted request PUSHes
             * before it can cancel/park, so RX alone is the wake-up source.
             * Do not read the PIO IRQ register on every request-wait iteration.
             * In particular, RX+IRQ together means a cancelled request, not a
             * new cycle: a late TX stays parked until flushed below. */
            struct sb_cpu_request request;
            const struct sb_cpu_bus bus = {&pio1->ctrl, &sio_hw->gpio_in, &stop};
            if (!sb_rx_first_reply(&request, rom_words, &bus)) goto service_finished;
            const uint32_t high = request.high, low = request.low;
            const uint16_t word = request.word;
            const bool valid = request.valid;

            /* Publication is complete. PIO alone qualifies, preloads, enables
             * and releases the bus. No counter, address reconstruction or
             * trace write is needed until its released park. */
            while (!(pio1->irq & 1)) tight_loop_contents();
            const bool did_enable = (pio1->irq & 2) != 0;
            const uint32_t address = sb_address(low, high);
            const uint32_t index = requested++;
            if (valid) ++posted;
            if (index < TRACE_LIMIT)
                records[index] = (struct reply_record){address, high, word, valid};
            if (did_enable) ++enabled; else ++not_enabled;
            const bool wrong_address = address != completed * 2;
            if (wrong_address) ++address_errors;
            if (!did_enable || wrong_address) {
                if (anomaly_count < TRACE_LIMIT)
                    anomalies[anomaly_count] = (struct anomaly_record){
                        completed, address, high, did_enable};
                ++anomaly_count;
            }
            /* Same-core publisher, SM already Hi-Z. Flush even a late TX
             * before acknowledging IRQ0; it cannot leak into a later read. */
            pio_sm_clear_fifos(pio1, 0);
            pio_interrupt_clear(pio1, 1);
            ++completed;
            if (stop || completed >= active_limit) break;
            pio_interrupt_clear(pio1, 0);
        }
#else
        struct reply_record current = {0};
        bool have_current = false;
        if (active_response <= SB_LAT_CPU_ONES) for (;;) {
            if (pio1->irq & 1) {
                /* PIO sets this only AFTER output release. The same core
                 * publishes replies and acknowledges: no late concurrent
                 * publisher can put a word after this FIFO flush. */
                pio_sm_clear_fifos(pio1, 0);
                const bool did_enable = (pio1->irq & 2) != 0;
                const bool wrong_address = have_current && current.address != completed * 2;
                if (did_enable) ++enabled; else ++not_enabled;
                if (wrong_address) ++address_errors;
                if (!did_enable || wrong_address || !have_current) {
                    if (anomaly_count < TRACE_LIMIT)
                        anomalies[anomaly_count] = (struct anomaly_record){
                            completed, have_current ? current.address : UINT32_MAX,
                            have_current ? current.high : 0, did_enable};
                    ++anomaly_count;
                }
                have_current = false;
                /* Clear the per-cycle marker only after observing completion,
                 * while the SM is parked after output release. */
                pio_interrupt_clear(pio1, 1);
                ++completed;
                if (stop || completed >= active_limit) break;
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
                current = (struct reply_record){address, high, word, valid};
                have_current = true;
                const uint32_t index = requested++;
                if (index < TRACE_LIMIT)
                    records[index] = (struct reply_record){address, high, word, valid};
            } else if (stop) {
                /* No request yet: disabling cannot strand driven pins.
                 * Once requested>completed we instead wait for PIO release. */
                if (requested == completed) break;
            }
        }
#endif
        else for (;;) {
            if (pio1->irq & 1) {
                /* PIO already released. Drain the snapshot here: CPU may
                 * have been stalled for the complete constant response. */
                unsigned count = 0;
                uint32_t high = 0;
                while (!(pio1->fstat & (1u << PIO_FSTAT_RXEMPTY_LSB))) {
                    high = pio1->rxf[0];
                    ++count;
                }
                requested += count;
                const bool did_enable = (pio1->irq & 2) != 0;
                if (did_enable) ++enabled; else ++not_enabled;
                if (count != 1 || (high & 0x2000000fu) != 0x2000000au) ++anomaly_count;
                /* No CPU address sample or TX publication exists in this
                 * path. posted/address_errors remain zero by definition. */
                pio_sm_clear_fifos(pio1, 0);
                pio_interrupt_clear(pio1, 1);
                ++completed;
                if (stop || completed >= active_limit) break;
                pio_interrupt_clear(pio1, 0);
            } else if (stop) {
                /* Unlike the CPU responder, an empty RX FIFO cannot prove
                 * constant PIO is idle. Force every data pad OE low BEFORE
                 * disabling SM. If CPU stalls during this loop, the still-
                 * running PIO retains autonomous control/reset release. */
                for (unsigned pin = 20; pin <= 35; ++pin)
                    gpio_set_oeover(pin, GPIO_OVERRIDE_LOW);
                __dmb();
                break;
            }
        }
#if SB_LATENCY_RX_FIRST
service_finished:
#endif
        pio_sm_set_enabled(pio1, 0, false);
        /* Abort may have disabled a driven SM, with all pad OEs forced low.
         * Clear its retained directions before a later mux/re-arm. */
        pio_sm_exec(pio1, 0, APIO_MOV_PINDIRS_NULL);
        data_input_only();
        /* GP39 can otherwise remain high if an explicit stop aborted a stage
         * marker. Re-arm reconstructs its low latch/direction before PIO mux. */
        gpio_put(39, false);
        gpio_set_function(39, GPIO_FUNC_SIO);
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
    static bool connected, dumping, dumping_anomalies, status_detail;
    static bool marker_banner;
    static unsigned index;
    if (now < next) return;
    next = now + 10000;
    if (!stdio_usb_connected()) {
        connected = dumping = dumping_anomalies = status_detail = false;
        return;
    }
    if (!connected) {
        connected = true;
        announce = now + 250000;
        marker_banner = true;
        return;
    }
    if (announce) {
        if (now < announce) return;
        announce = 0;
        /* Nonblocking USB can accept only one FIFO's worth per tick. Send
         * this identity separately, then the normal banner on a later tick. */
        if (marker_banner) {
            emit("LATENCY response=%s marker=%s words=%u\r\n",
                 response_names[selected_response], marker_names[selected_marker], SB_LAT_WORDS);
#if SB_LATENCY_RX_FIRST
            emit("ENGINE=rx-first-v2 full_words=%u\r\n", SB_ROM_IMAGE_SIZE / 2);
#endif
            marker_banner = false;
            announce = now + 20000;
            return;
        }
        emit("FujiSpring LATENCY DIAGNOSTIC / bounded reads / 150MHz\r\n"
             "ROM=%u CRC32=%08lx; starts DISARMED\r\n"
             "b=64 reads s=status x=stop q=config\r\n",
             SB_ROM_IMAGE_SIZE, (unsigned long)SB_ROM_IMAGE_CRC);
        return;
    }
    if (status_detail) {
        emit("enabled=%lu not_enabled=%lu address_errors=%lu anomalies=%lu\r\n",
             (unsigned long)enabled, (unsigned long)not_enabled,
             (unsigned long)address_errors, (unsigned long)anomaly_count);
        status_detail = false;
        return;
    }
    const int command = getchar_timeout_us(0);
    switch (command) {
    case 'R': case 'Z': case 'F': case 'z': case 'f':
        if (stopped && !arm) {
            selected_response = command == 'R' ? SB_LAT_CPU_ROM : command == 'Z' ? SB_LAT_CPU_ZERO :
                command == 'F' ? SB_LAT_CPU_ONES : command == 'z' ? SB_LAT_PIO_ZERO : SB_LAT_PIO_ONES;
        } else emit("config refused: require stopped\r\n");
        break;
    case '0': case '1': case '2': case '3':
        if (stopped && !arm) selected_marker = (enum sb_latency_marker)(command - '0');
        else emit("config refused: require stopped\r\n");
        break;
    case 'q':
        emit("LATENCY response=%s marker=%s words=%u\r\n",
             response_names[selected_response], marker_names[selected_marker], SB_LAT_WORDS);
        break;
    case 'h':
        emit("R/Z/F=CPU ROM/zero/ones z/f=PIO zero/ones 0..3=marker b=64 reads\r\n");
#if SB_LATENCY_RX_FIRST
        emit("B=full ROM (cpu-rom only)\r\n");
#endif
        break;
    case 'b':
#if SB_LATENCY_RX_FIRST
    case 'B':
        if (command == 'B' && selected_response != SB_LAT_CPU_ROM) {
            emit("full ROM refused: require cpu-rom\r\n");
            break;
        }
#endif
        if (rom_ready && ready && stopped && !arm) {
            dumping = dumping_anomalies = false;
#if SB_LATENCY_RX_FIRST
            arm_limit = command == 'B' ? SB_ROM_IMAGE_SIZE / 2 : SB_LAT_WORDS;
#else
            arm_limit = SB_LAT_WORDS;
#endif
            __dmb(); arm = true;
            emit("arm requested; s must show stopped=0 before app Read\r\n");
        } else emit("arm refused: require ROM ready, stopped=1, arm_pending=0\r\n");
        break;
    case 's':
        emit("rom_ready=%u ready=%u stopped=%u arm_pending=%u run=%lu "
             "requests=%lu posted=%lu completed=%lu limit=%lu\r\n",
             rom_ready, ready, stopped, arm, (unsigned long)runs,
             (unsigned long)requested, (unsigned long)posted, (unsigned long)completed,
             (unsigned long)active_limit);
        status_detail = true;
        break;
    case 'x':
        if (!arm) { stop = true; emit("stop requested; s must confirm stopped=1\r\n"); }
        else emit("wait for arm_pending=0\r\n");
        break;
    case 'd':
        if (active_response >= SB_LAT_PIO_ZERO) {
            emit("PIO mode: no CPU reply or sampled low-address trace\r\n"); break;
        }
        if (stopped && !arm) {
            __dmb(); index = 0; dumping = true; dumping_anomalies = false;
            emit("CPU modes only: posted is TX; PIO modes have no CPU reply/address trace\r\n");
        } else emit("dump requires stopped=1 arm_pending=0\r\n");
        break;
    case 'e':
        if (active_response >= SB_LAT_PIO_ZERO) {
            emit("PIO mode: anomaly counters only; no CPU address records\r\n"); break;
        }
        if (stopped && !arm) {
            __dmb(); index = 0; dumping_anomalies = true; dumping = false;
            emit("First anomalies; enabled means PIO drove, not CPU sampled in time\r\n");
        } else emit("anomalies require stopped=1 arm_pending=0\r\n");
        break;
    default: break;
    }
    if (dumping_anomalies) {
        if (index >= anomaly_count || index >= TRACE_LIMIT) {
            dumping_anomalies = false; emit("anomalies end\r\n");
        } else {
            const struct anomaly_record r = anomalies[index++];
            emit("index=%lu expected=%06lx address=%06lx high=%08lx enabled=%u\r\n",
                 (unsigned long)r.index, (unsigned long)(r.index * 2),
                 (unsigned long)r.address, (unsigned long)r.high, (unsigned)r.enabled);
        }
    }
    if (dumping) {
        if (index >= requested || index >= TRACE_LIMIT) {
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
    gpio_init(39);
    gpio_put(39, false);
    gpio_set_dir(39, GPIO_OUT);
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
        (void)now;
        /* Only core 1 needs a busy polling loop. Repeated timer reads and
         * GPIO writes here add shared-bus traffic during its response window.
         * USB interrupts continue on core 0; console polling stays at 10 ms.
         * This reduces avoidable load, not a guarantee of bus timing margin. */
        sleep_ms(1);
    }
}
