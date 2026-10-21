#include "rom_program.h"
#include <apio.h>
#include <epio_priv.h> /* Pinned emulator: observe direction, not floating data. */
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)
#define DATA (UINT64_C(0xffff) << 20)
#define RESET (UINT64_C(1) << 45)
#define IDLE (RESET | (UINT64_C(15) << 16))
#define READ (RESET | (UINT64_C(10) << 16))
#if SB_ROM_DRIVE_MARKER
#define MARKER (UINT64_C(1) << SB_ROM_DRIVE_MARKER_PIN)
#else
#define MARKER UINT64_C(0)
#endif
#define OUTPUTS (DATA | MARKER)
#define EXTERNAL (((UINT64_C(1) << 48) - 1) & ~OUTPUTS)

/* Pinned EPIO lacks side-set and treats bit12 as sixteen delay clocks.
 * Preserve real encoded words, strip that field only in the test instance,
 * and model mandatory side-set each clock, including stalls. Compare it to
 * independently executed data PINDIRS. This does not model hardware pad skew. */
static uint16_t encoded[32];
static epio_t *model_from_apio(void) {
#if SB_ROM_DRIVE_MARKER
    /* EPIO applies EXECs before GPIO muxes and drops SET side effects for
     * unmapped pins. Silicon retains PIO latches regardless of the pad mux.
     * Pre-map just this diagnostic output so EPIO executes the real SETs. */
    CHECK(_apio_emulated_gpios.output_block[SB_ROM_DRIVE_MARKER_PIN] == 1);
    epio_t *e = epio_init();
    CHECK(e);
    epio_set_gpio_output_control(e, SB_ROM_DRIVE_MARKER_PIN, 1);
    epio_update_from_apio(e);
#else
    epio_t *e = epio_from_apio();
#endif
    CHECK(e);
    for (unsigned i = 0; i < 32; ++i) {
        encoded[i] = epio_get_instr(e, 1, i);
#if SB_ROM_DRIVE_MARKER
        CHECK((bool)(encoded[i] & 0x1000) == (i >= 23 && i < 29));
        CHECK((encoded[i] & 0x0f00) == 0);
        epio_set_instr(e, 1, i, encoded[i] & ~0x1000u);
#else
        CHECK((encoded[i] & 0x1f00) == 0);
#endif
    }
    epio_sm_reg_t cfg;
    epio_get_sm_reg(e, 1, 0, &cfg);
#if SB_ROM_DRIVE_MARKER
    CHECK(APIO_SIDE_SET_COUNT_FROM_REG(cfg.pinctrl) == 1);
    CHECK(APIO_SIDE_SET_BASE_FROM_REG(cfg.pinctrl) == 23);
    CHECK(APIO_SET_COUNT_FROM_REG(cfg.pinctrl) == 1);
    CHECK(APIO_SET_BASE_FROM_REG(cfg.pinctrl) == 23);
    CHECK(!APIO_EXECCTRL_SIDE_EN_FROM_REG(cfg.execctrl));
    CHECK(!APIO_EXECCTRL_SIDE_PINDIR_FROM_REG(cfg.execctrl));
    CHECK((e->gpio.gpio_direction & MARKER) == MARKER);
    CHECK(!(e->gpio.gpio_output_state & MARKER));
#else
    CHECK(APIO_SIDE_SET_COUNT_FROM_REG(cfg.pinctrl) == 0);
#endif
    return e;
}
static void step(epio_t *e, unsigned cycles) {
    while (cycles--) {
#if SB_ROM_DRIVE_MARKER
        const unsigned pc = e->block[1].sm[0].pc;
        CHECK(pc < 32 && !e->block[1].sm[0].delay);
        const bool marker_high = (encoded[pc] & 0x1000) != 0;
#endif
        epio_step_cycles(e, 1);
#if SB_ROM_DRIVE_MARKER
        if (marker_high) e->gpio.gpio_output_state |= MARKER;
        else e->gpio.gpio_output_state &= ~MARKER;
        CHECK(marker_high == ((e->gpio.gpio_direction & DATA) != 0));
#endif
        CHECK((e->gpio.gpio_direction & ~OUTPUTS) == 0);
    }
}

static void bus(epio_t *e, uint64_t pins, unsigned cycles) {
    epio_drive_gpios_ext(e, EXTERNAL, pins);
    if (cycles) step(e, cycles);
}
static bool driven(epio_t *e) {
    CHECK((e->gpio.gpio_direction & ~OUTPUTS) == 0);
    CHECK((e->gpio.gpio_direction & DATA) == 0 ||
          (e->gpio.gpio_direction & DATA) == DATA);
    return (e->gpio.gpio_direction & DATA) != 0;
}
static epio_t *fresh(void) {
    sb_rom_program_init(true);
    epio_t *e = model_from_apio();
    CHECK(e);
    CHECK(epio_get_gpio_output_control(e, 1) == OUTPUTS);
    CHECK(epio_get_gpio_output_control(e, 0) == 0);
    epio_sm_reg_t config;
    epio_get_sm_reg(e, 1, 0, &config);
    CHECK(((config.execctrl >> 12) & 31) == 31);
    bus(e, IDLE, 30);
    CHECK(!driven(e));
    CHECK(!epio_peek_block_irq_num(e, 1, 1));
    return e;
}
static void request(epio_t *e) {
    bus(e, READ | 0x2468, 30);
    CHECK(epio_rx_fifo_depth(e, 1, 0) == 1);
    CHECK((epio_pop_rx_fifo(e, 1, 0) & 15) == 10);
    CHECK(!driven(e));
}
/* Same ordering as the single-owner firmware: while IRQ-WAIT parks the SM,
 * flush RX and TX, then clear IRQ. Never acknowledge before a late put. */
static void ack(epio_t *e) {
    CHECK(epio_peek_block_irq_num(e, 1, 0));
    CHECK(!driven(e));
    while (epio_rx_fifo_depth(e, 1, 0)) epio_pop_rx_fifo(e, 1, 0);
    while (epio_tx_fifo_depth(e, 1, 0)) epio_pop_tx_fifo(e, 1, 0);
    epio_clear_block_irq(e, 1, 1);
    epio_clear_block_irq(e, 1, 0);
}

int main(void) {
    sb_rom_program_init(true);
    epio_t *startup = model_from_apio();
    bus(startup, READ, 100);
    CHECK(!driven(startup) && epio_rx_fifo_depth(startup, 1, 0) == 0);
    bus(startup, IDLE, 20);
    request(startup);
    epio_free(startup);

    /* Every data line: the first enabled cycle must already have the word,
     * and a held read cannot generate a second request. */
    for (unsigned bit = 0; bit < 16; ++bit) {
        epio_t *e = fresh();
        request(e);
        epio_push_tx_fifo(e, 1, 0, 1u << bit);
        unsigned n = 0;
        while (!driven(e) && n++ < 40) step(e, 1);
        CHECK(driven(e));
        CHECK(((e->gpio.gpio_output_state & DATA) >> 20) == (1u << bit));
        step(e, 100);
        CHECK(epio_peek_block_irq_num(e, 1, 1));
        CHECK(epio_rx_fifo_depth(e, 1, 0) == 0);
        epio_free(e);
    }
    /* Each invalid condition must release autonomously, regardless of CPU
     * progress, at every phase of the pending/reply/drive paths. */
    const uint64_t invalid[] = {
        IDLE, RESET | (UINT64_C(14) << 16), /* CS or OE deasserted */
        RESET | (UINT64_C(6) << 16),       /* write */
        RESET | (UINT64_C(2) << 16),       /* OE + WE */
        RESET | (UINT64_C(8) << 16),       /* both selects */
        UINT64_C(10) << 16,               /* RESET */
    };
    unsigned worst = 0;
    for (unsigned k = 0; k < sizeof(invalid) / sizeof(invalid[0]); ++k) {
        for (unsigned phase = 0; phase < 80; ++phase) {
            epio_t *e = fresh();
            request(e);
            epio_push_tx_fifo(e, 1, 0, 0xbeef);
            bool ever_enabled = false;
            for (unsigned tick = 0; tick < phase; ++tick) {
                step(e, 1);
                ever_enabled |= driven(e);
            }
            bus(e, invalid[k], 0);
            unsigned n = 0;
            /* Reaching IRQ also proves a pre-enable cancellation cannot
             * enable later. Merely checking present Hi-Z would miss that. */
            while (!epio_peek_block_irq_num(e, 1, 0) && n < 40) {
                step(e, 1);
                ever_enabled |= driven(e);
                ++n;
            }
            CHECK(n <= 12 && !driven(e));
            CHECK((bool)epio_peek_block_irq_num(e, 1, 1) == ever_enabled);
            if (n > worst) worst = n;
            step(e, 500); /* CPU dead: stays released */
            CHECK(!driven(e));
            /* Late reply, next read already selected: no stale data. */
            if (!epio_tx_fifo_depth(e, 1, 0))
                epio_push_tx_fifo(e, 1, 0, 0xdead);
            bus(e, READ, 50);
            CHECK(!driven(e));
            ack(e);
            step(e, 50);
            CHECK(!driven(e) && epio_rx_fifo_depth(e, 1, 0) == 0);
            bus(e, IDLE, 20);
            request(e);
            epio_push_tx_fifo(e, 1, 0, 0x1234);
            step(e, 40);
            CHECK(driven(e));
            CHECK(((e->gpio.gpio_output_state & DATA) >> 20) == 0x1234);
            epio_free(e);
        }
    }
    for (unsigned order = 0; order < 2; ++order) {
        epio_t *e = fresh();
        bus(e, RESET | ((uint64_t)(order ? 11 : 14) << 16), 30);
        CHECK(!driven(e) && epio_rx_fifo_depth(e, 1, 0) == 0);
        CHECK(!epio_peek_block_irq_num(e, 1, 1));
        request(e);
        step(e, 1000); /* No reply: never enables, no RX flood. */
        CHECK(!driven(e) && epio_rx_fifo_depth(e, 1, 0) == 0);
        bus(e, IDLE, 30);
        CHECK(epio_peek_block_irq_num(e, 1, 0) && !driven(e));
        epio_free(e);
    }
    /* Startup selected and writes/CS1/short CS-without-OE do not drive. */
    epio_t *e = fresh();
    const unsigned controls[] = {6, 9, 8, 2, 14, 11};
    for (unsigned i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
        bus(e, RESET | ((uint64_t)controls[i] << 16), 80);
        bus(e, IDLE, 30);
        CHECK(!driven(e) && epio_rx_fifo_depth(e, 1, 0) == 0);
    }
    epio_free(e);
    /* Characterize, do not hide, the sampled guard's blind interval. A one-
     * clock deselect can be missed: this image is restricted to the app's
     * widely spaced manual reads, not unqualified ROM scanning/boot. */
    bool missed_short_gap = false;
    for (unsigned phase = 0; phase < 16; ++phase) {
        e = fresh();
        request(e);
        if (phase) step(e, phase);
        bus(e, IDLE, 1);
        bus(e, READ, 20);
        if (!epio_peek_block_irq_num(e, 1, 0)) {
            epio_push_tx_fifo(e, 1, 0, 0xdead);
            step(e, 40);
            missed_short_gap |= driven(e);
        }
        epio_free(e);
    }
    CHECK(missed_short_gap);
    printf("ROM guard: 480 cancellation phases passed; worst park=%u PIO cycles\n", worst);
#if SB_ROM_DRIVE_MARKER
    puts("GPIO39 side-set model matches data PINDIRS on every tested clock; no added PIO clocks");
#endif
    puts("KNOWN LIMIT: one-clock deselect can be missed; not boot/timing qualified");
    return 0;
}
