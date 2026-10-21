/* selfobs-demo v5-cells — S1 merged study: handler x core0-load x capture-load
 *
 * One firmware serves BOTH the faculty demo and the 13-cell study.
 * New over v4-wp1:
 *   - LATRUN new form: LATRUN <s1|s2|s3> <usb|floop|rloop|thrash> <off|25M|62M> <n>
 *       handler : s1 flash-resident | s2 SRAM-resident | s3 SDK callback path
 *       core0   : usb idle | floop flash busy-loop | rloop RAM busy-loop | thrash cache sweep
 *       capture : analyzer OFF | 25 MS/s | 62.5 MS/s (capture SM + wrapped DMA = load)
 *   - old demo form still works: LATRUN raw|poll|crit <n> [thrash]  (raw == s2)
 *   - FIXTURE / UARTCAP / PING unchanged -> demo.py still runs on this firmware
 *
 * Load-loop bodies are DUPLICATED textually (not shared helpers) so flash vs RAM
 * placement is the only difference between floop and rloop.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/io_bank0.h"

#include "demo.pio.h"

#define PIN_STIM   0u
#define PIN_RESP   1u
#define PIN_UART   4u
#define SYS_HZ     125000000u
#define LAT_NS_PER_COUNT 16u
#define STIM_PERIOD_COUNTS 625000u              /* 100 Hz */
#define LAT_MAX_EVENTS 8192u
#define CAP_MAX_WORDS  6144u
#define CAP_RATE_HZ    500000u
#define UART_CLKDIV    814.0f

#define THRASH_SIZE 65536u
#define THRASH_CHUNK 2048u
#define RING_WORDS   16384u                     /* power of two, for DMA write wrap */

static PIO const p0 = pio0;
static PIO const p1 = pio1;
static uint sm_cap, sm_stim, sm_fix, sm_lat, sm_uart;
static uint off_cap, off_stim, off_fix, off_lat, off_uart;
static int  dma_cap = -1, dma_ring = -1;
static uint32_t latbuf[LAT_MAX_EVENTS];
static uint32_t capbuf[CAP_MAX_WORDS];
static uint32_t capring[RING_WORDS];

static const uint8_t thrash_src[THRASH_SIZE] = {1, 2, 3, 4};
static volatile uint32_t thrash_pos;
static volatile uint32_t thrash_sink;
static volatile uint32_t load_sink;

/* ---------- core-1 mailbox ---------- */
typedef enum {
    M_IDLE = 0, M_POLL = 2, M_CRIT = 3, M_DISARM = 4,
    M_FLASH = 5, M_SRAM = 6, M_SDK = 7
} dut_mode_t;
typedef enum { INST_NONE = 0, INST_FLASH, INST_SRAM, INST_SDK } inst_t;
static volatile uint32_t mb_mode   = M_IDLE;
static volatile uint32_t mb_status = M_IDLE;
static volatile uint32_t mb_events = 0;
static inst_t cur_inst = INST_NONE;

/* ---------- self-contained DUT bodies (no SDK calls inside) ---------- */
#define DUT_PULSE_BODY()                                                    \
    do {                                                                    \
        sio_hw->gpio_set = 1u << PIN_RESP;                                  \
        for (volatile int i_ = 0; i_ < 12; i_++) __asm volatile ("nop");    \
        sio_hw->gpio_clr = 1u << PIN_RESP;                                  \
    } while (0)

#define DUT_ACK_STIM()                                                      \
    (io_bank0_hw->intr[PIN_STIM >> 3] =                                      \
        GPIO_IRQ_EDGE_RISE << (4 * (PIN_STIM & 7u)))

static void dut_flash_handler(void)
{
    DUT_PULSE_BODY();
    DUT_ACK_STIM();
    mb_events++;
}

static void __not_in_flash_func(dut_sram_handler)(void)
{
    DUT_PULSE_BODY();
    DUT_ACK_STIM();
    mb_events++;
}

static void dut_cb(__unused uint gpio, __unused uint32_t events)
{
    DUT_PULSE_BODY();
    mb_events++;
}

/* ---------- core-1 mode switching (SDK semantics verified from sources) ---------- */
static void core1_apply(uint32_t m)
{
    gpio_set_irq_enabled(PIN_STIM, GPIO_IRQ_EDGE_RISE, false);
    irq_set_enabled(IO_IRQ_BANK0, false);

    if (cur_inst == INST_SDK) {
        gpio_set_irq_enabled_with_callback(PIN_STIM, GPIO_IRQ_EDGE_RISE, false, NULL);
    } else if (cur_inst == INST_FLASH) {
        irq_remove_handler(IO_IRQ_BANK0, dut_flash_handler);
    } else if (cur_inst == INST_SRAM) {
        irq_remove_handler(IO_IRQ_BANK0, dut_sram_handler);
    }
    cur_inst = INST_NONE;

    if (m == M_FLASH) {
        irq_set_exclusive_handler(IO_IRQ_BANK0, dut_flash_handler);
        cur_inst = INST_FLASH;
    } else if (m == M_SRAM || m == M_CRIT) {
        irq_set_exclusive_handler(IO_IRQ_BANK0, dut_sram_handler);
        cur_inst = INST_SRAM;
    } else if (m == M_SDK) {
        gpio_set_irq_enabled_with_callback(PIN_STIM, GPIO_IRQ_EDGE_RISE, true, dut_cb);
        cur_inst = INST_SDK;
    }

    if (cur_inst == INST_FLASH || cur_inst == INST_SRAM) {
        gpio_set_irq_enabled(PIN_STIM, GPIO_IRQ_EDGE_RISE, true);
        irq_set_enabled(IO_IRQ_BANK0, true);
    }
    mb_events = 0;
}

static void core1_main(void)
{
    for (;;) {
        while (mb_mode == mb_status) tight_loop_contents();
        uint32_t m = mb_mode;
        core1_apply(m);
        mb_status = m;

        if (m == M_POLL) {
            while (mb_mode == M_POLL) {
                if (sio_hw->gpio_in & (1u << PIN_STIM)) {
                    DUT_PULSE_BODY();
                    mb_events++;
                    while (sio_hw->gpio_in & (1u << PIN_STIM)) tight_loop_contents();
                }
            }
        } else if (m == M_CRIT) {
            while (mb_mode == M_CRIT) {
                uint32_t s = save_and_disable_interrupts();
                busy_wait_us(100);
                restore_interrupts(s);
                busy_wait_us(150);
            }
        }
    }
}

/* ---------- core-0 load workloads (duplicated bodies on purpose) ---------- */
static void load_step_flash(void)
{
    static uint32_t buf[64];
    uint32_t acc = load_sink;
    for (uint32_t i = 0; i < 500; i++) {
        acc += buf[i & 63u];
        buf[i & 63u] = acc ^ (i << 3);
    }
    load_sink = acc;
}

static void __not_in_flash_func(load_step_sram)(void)
{
    static uint32_t buf[64];
    uint32_t acc = load_sink;
    for (uint32_t i = 0; i < 500; i++) {
        acc += buf[i & 63u];
        buf[i & 63u] = acc ^ (i << 3);
    }
    load_sink = acc;
}

static void thrash_step(void)
{
    uint32_t base = thrash_pos;
    uint32_t sum = 0;
    const volatile uint8_t *p = (const volatile uint8_t *)thrash_src + base;
    for (uint32_t i = 0; i < THRASH_CHUNK; i++) sum += p[i];
    thrash_sink += sum;
    thrash_pos = (base + THRASH_CHUNK) & (THRASH_SIZE - 1u);
}

/* ---------- core-0 helpers ---------- */
static bool mb_wait_status(uint32_t v, uint32_t ms)
{
    uint64_t dl = time_us_64() + (uint64_t)ms * 1000u;
    while (mb_status != v) {
        if (time_us_64() > dl) return false;
        tight_loop_contents();
    }
    return true;
}

static void dut_set(dut_mode_t m)
{
    mb_mode = M_DISARM;
    mb_wait_status(M_DISARM, 500);
    if (m == M_IDLE) return;
    mb_mode = (uint32_t)m;
    mb_wait_status((uint32_t)m, 500);
}

static void lat_arm(void)
{
    pio_sm_config c = demo_lattimer_program_get_default_config(off_lat);
    sm_config_set_jmp_pin(&c, PIN_RESP);
    sm_config_set_clkdiv(&c, 1.0f);
    pio_sm_init(p0, sm_lat, off_lat, &c);
    pio_sm_clear_fifos(p0, sm_lat);
    pio_sm_set_enabled(p0, sm_lat, true);
}

static uint32_t c0_mode;   /* 0 usb, 1 floop, 2 rloop, 3 thrash */

static bool lat_word(uint32_t *w)
{
    uint64_t dl = time_us_64() + 300000u;
    while (pio_sm_is_rx_fifo_empty(p0, sm_lat)) {
        if (time_us_64() > dl) return false;
        switch (c0_mode) {
            case 1:  load_step_flash(); break;
            case 2:  load_step_sram();  break;
            case 3:  thrash_step();     break;
            default: tight_loop_contents(); break;
        }
    }
    *w = pio_sm_get(p0, sm_lat);
    return true;
}

static void stim_arm(uint32_t period_counts)
{
    pio_sm_config c = demo_stim_program_get_default_config(off_stim);
    sm_config_set_set_pins(&c, PIN_STIM, 1);
    sm_config_set_clkdiv(&c, 1.0f);
    pio_sm_init(p0, sm_stim, off_stim, &c);
    pio_sm_set_consecutive_pindirs(p0, sm_stim, PIN_STIM, 1, true);
    pio_sm_set_pins_with_mask(p0, sm_stim, 0u, 1u << PIN_STIM);
    pio_sm_clear_fifos(p0, sm_stim);
    pio_sm_put(p0, sm_stim, period_counts);
    pio_sm_set_enabled(p0, sm_stim, true);
}

static void stim_disarm(void)
{
    pio_sm_set_enabled(p0, sm_stim, false);
    pio_sm_set_pins_with_mask(p0, sm_stim, 0u, 1u << PIN_STIM);
}

static void fix_arm(uint32_t period_counts)
{
    pio_sm_config c = demo_fixture_program_get_default_config(off_fix);
    sm_config_set_set_pins(&c, PIN_STIM, 2);
    sm_config_set_clkdiv(&c, 1.0f);
    pio_sm_init(p0, sm_fix, off_fix, &c);
    pio_sm_set_consecutive_pindirs(p0, sm_fix, PIN_STIM, 2, true);
    pio_sm_set_pins_with_mask(p0, sm_fix, 0u, (1u << PIN_STIM) | (1u << PIN_RESP));
    pio_sm_clear_fifos(p0, sm_fix);
    pio_sm_put(p0, sm_fix, period_counts);
    pio_sm_set_enabled(p0, sm_fix, true);
}

static void fix_disarm(void)
{
    pio_sm_set_enabled(p0, sm_fix, false);
    pio_sm_set_pins_with_mask(p0, sm_fix, 0u, (1u << PIN_STIM) | (1u << PIN_RESP));
}

static void pin1_to_pio(void) { pio_gpio_init(p0, PIN_RESP); }
static void pin1_to_sio(void)
{
    gpio_init(PIN_RESP);
    gpio_set_dir(PIN_RESP, GPIO_OUT);
    gpio_put(PIN_RESP, 0);
}

/* ---------- capture-load (the "is the analyzer on?" factor) ---------- */
static void cap_load_start(uint32_t cap)
{
    if (cap == 0) return;
    float div = (cap == 25) ? 5.0f : 2.0f;     /* 125/5 = 25 MS/s, 125/2 = 62.5 MS/s */
    pio_sm_config c = demo_capture_program_get_default_config(off_cap);
    sm_config_set_in_pins(&c, 0);
    sm_config_set_in_shift(&c, true, true, 32);
    sm_config_set_clkdiv(&c, div);
    pio_sm_init(p0, sm_cap, off_cap, &c);
    pio_sm_clear_fifos(p0, sm_cap);

    dma_channel_config dc = dma_channel_get_default_config((uint)dma_ring);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_ring(&dc, true, 14);     /* write wrap over 16384 words */
    channel_config_set_dreq(&dc, pio_get_dreq(p0, sm_cap, false));
    dma_channel_configure((uint)dma_ring, &dc, capring, &p0->rxf[sm_cap],
                          0xFFFFFFFFu, true);
    pio_sm_set_enabled(p0, sm_cap, true);
}

static void cap_load_stop(void)
{
    pio_sm_set_enabled(p0, sm_cap, false);
    dma_channel_abort((uint)dma_ring);
}

/* ---------- runs ---------- */
static uint32_t run_cell(uint32_t hmode, uint32_t c0, uint32_t cap, uint32_t n)
{
    if (n == 0) return 0;
    if (n > LAT_MAX_EVENTS) n = LAT_MAX_EVENTS;
    dut_set((dut_mode_t)hmode);
    lat_arm();
    c0_mode = c0;
    thrash_pos = 0;
    cap_load_start(cap);
    stim_arm(STIM_PERIOD_COUNTS);
    uint32_t got = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!lat_word(&latbuf[i])) break;
        got++;
    }
    stim_disarm();
    cap_load_stop();
    c0_mode = 0;
    pio_sm_set_enabled(p0, sm_lat, false);
    dut_set(M_IDLE);
    return got;
}

static uint32_t run_fixture(uint32_t n)
{
    if (n == 0) return 0;
    if (n > LAT_MAX_EVENTS) n = LAT_MAX_EVENTS;
    dut_set(M_IDLE);
    pin1_to_pio();
    lat_arm();
    c0_mode = 0;
    fix_arm(STIM_PERIOD_COUNTS);
    uint32_t got = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!lat_word(&latbuf[i])) break;
        got++;
    }
    fix_disarm();
    pio_sm_set_enabled(p0, sm_lat, false);
    pin1_to_sio();
    return got;
}

static void uart_capture(const char *text)
{
    size_t len = strlen(text);
    if (len == 0) { printf("#ERR empty text\n"); return; }
    if (len > 60) len = 60;

    uint32_t samples = (uint32_t)(((uint64_t)len * 10u * CAP_RATE_HZ) / 9600u) + 256u;
    uint32_t words = (samples + 3u) / 4u;
    if (words > CAP_MAX_WORDS) words = CAP_MAX_WORDS;

    pio_sm_config c = demo_capture_program_get_default_config(off_cap);
    sm_config_set_in_pins(&c, 0);
    sm_config_set_in_shift(&c, true, true, 32);
    sm_config_set_clkdiv(&c, (float)SYS_HZ / (float)CAP_RATE_HZ);
    pio_sm_init(p0, sm_cap, off_cap, &c);
    pio_sm_clear_fifos(p0, sm_cap);

    dma_channel_config dc = dma_channel_get_default_config((uint)dma_cap);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_dreq(&dc, pio_get_dreq(p0, sm_cap, false));
    dma_channel_configure((uint)dma_cap, &dc, capbuf, &p0->rxf[sm_cap], words, true);

    pio_sm_set_enabled(p0, sm_cap, true);
    for (size_t i = 0; i < len; i++) pio_sm_put_blocking(p1, sm_uart, (uint32_t)(uint8_t)text[i]);

    uint64_t dl = time_us_64() + ((uint64_t)len * 10u * 1000000u) / 9600u + 200000u;
    while (dma_channel_is_busy((uint)dma_cap)) {
        if (time_us_64() > dl) break;
    }
    pio_sm_set_enabled(p0, sm_cap, false);
    dma_channel_abort((uint)dma_cap);

    printf("#UARTCAP words=%lu\n", (unsigned long)words);
    for (uint32_t i = 0; i < words; i++) printf("%08lX\n", (unsigned long)capbuf[i]);
    printf("#END\n");
}

static void demo_uart_init(void)
{
    pio_gpio_init(p1, PIN_UART);
    pio_sm_config c = demo_uarttx_program_get_default_config(off_uart);
    sm_config_set_set_pins(&c, PIN_UART, 1);
    sm_config_set_out_pins(&c, PIN_UART, 1);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_clkdiv(&c, UART_CLKDIV);
    pio_sm_init(p1, sm_uart, off_uart, &c);
    pio_sm_set_consecutive_pindirs(p1, sm_uart, PIN_UART, 1, true);
    pio_sm_set_pins_with_mask(p1, sm_uart, 1u << PIN_UART, 1u << PIN_UART);
    pio_sm_set_enabled(p1, sm_uart, true);
}

int main(void)
{
    stdio_init_all();

    off_cap  = pio_add_program(p0, &demo_capture_program);
    off_stim = pio_add_program(p0, &demo_stim_program);
    off_fix  = pio_add_program(p0, &demo_fixture_program);
    off_lat  = pio_add_program(p0, &demo_lattimer_program);
    off_uart = pio_add_program(p1, &demo_uarttx_program);

    sm_cap  = pio_claim_unused_sm(p0, true);
    sm_stim = pio_claim_unused_sm(p0, true);
    sm_fix  = pio_claim_unused_sm(p0, true);
    sm_lat  = pio_claim_unused_sm(p0, true);
    sm_uart = pio_claim_unused_sm(p1, true);

    dma_cap  = (int)dma_claim_unused_channel(true);
    dma_ring = (int)dma_claim_unused_channel(true);

    pio_gpio_init(p0, PIN_STIM);
    pin1_to_sio();
    for (uint pin = 2; pin <= 7; pin++) {
        if (pin == PIN_UART) continue;
        gpio_init(pin);
        gpio_pull_down(pin);
    }
    demo_uart_init();

    multicore_launch_core1(core1_main);

    printf("selfobs-demo v5-cells ready.\n");
    printf("LATRUN <s1|s2|s3> <usb|floop|rloop|thrash> <off|25M|62M> <n>   (study)\n");
    printf("LATRUN <raw|poll|crit> <n> [thrash] | FIXTURE <n> | UARTCAP <text>\n");
    printf("thrash array at 0x%08lX (%s)\n",
           (unsigned long)(uintptr_t)thrash_src,
           ((uintptr_t)thrash_src < 0x20000000u) ? "flash/XIP - OK" : "WARNING: in RAM?!");

    char line[96];
    for (;;) {
        if (!fgets(line, sizeof line, stdin)) { sleep_ms(10); continue; }
        char cmd[16] = {0}, t1[16] = {0}, t2[16] = {0}, t3[16] = {0}, t4[16] = {0};
        int nt = sscanf(line, "%15s %15s %15s %15s %15s", cmd, t1, t2, t3, t4);
        if (nt < 1) continue;

        if (strcmp(cmd, "PING") == 0) {
            printf("PONG selfobs-demo v5-cells\n");
        } else if (strcmp(cmd, "FIXTURE") == 0 && nt >= 2) {
            uint32_t cnt = run_fixture((uint32_t)strtoul(t1, NULL, 10));
            printf("#FIX n=%lu\n", (unsigned long)cnt);
            for (uint32_t i = 0; i < cnt; i++) printf("%08lX\n", (unsigned long)latbuf[i]);
            printf("#END\n");
        } else if (strcmp(cmd, "LATRUN") == 0 && nt >= 3) {
            long nold = strtol(t2, NULL, 10);
            if (nold > 0) {
                /* old demo form: LATRUN <mode> <n> [thrash] */
                uint32_t m;
                const char *name = t1;
                bool thrash = (nt >= 4 && strcmp(t3, "thrash") == 0);
                if      (strcmp(t1, "s1") == 0) m = M_FLASH;
                else if (strcmp(t1, "s2") == 0 || strcmp(t1, "raw") == 0) { m = M_SRAM; name = "s2"; }
                else if (strcmp(t1, "s3") == 0) m = M_SDK;
                else if (strcmp(t1, "poll") == 0) m = M_POLL;
                else if (strcmp(t1, "crit") == 0) m = M_CRIT;
                else { printf("#ERR mode\n"); continue; }
                uint32_t c0 = thrash ? 3u : 0u;
                uint32_t got;
                if (m == M_POLL || m == M_CRIT) got = run_cell(m, 0, 0, (uint32_t)nold);
                else got = run_cell(m, c0, 0, (uint32_t)nold);
                printf("#LAT h=%s c0=%s cap=off thrasharr=%08lX n=%lu\n",
                       name, thrash ? "thrash" : "usb",
                       (unsigned long)(uintptr_t)thrash_src, (unsigned long)got);
                for (uint32_t i = 0; i < got; i++) printf("%08lX\n", (unsigned long)latbuf[i]);
                printf("#END\n");
            } else {
                /* new study form */
                uint32_t m;
                if      (strcmp(t1, "s1") == 0) m = M_FLASH;
                else if (strcmp(t1, "s2") == 0) m = M_SRAM;
                else if (strcmp(t1, "s3") == 0) m = M_SDK;
                else { printf("#ERR handler s1|s2|s3\n"); continue; }
                uint32_t c0;
                if      (strcmp(t2, "usb") == 0)    c0 = 0;
                else if (strcmp(t2, "floop") == 0)  c0 = 1;
                else if (strcmp(t2, "rloop") == 0)  c0 = 2;
                else if (strcmp(t2, "thrash") == 0) c0 = 3;
                else { printf("#ERR core0 usb|floop|rloop|thrash\n"); continue; }
                uint32_t cap;
                if      (strcmp(t3, "off") == 0) cap = 0;
                else if (strcmp(t3, "25M") == 0) cap = 25;
                else if (strcmp(t3, "62M") == 0) cap = 62;
                else { printf("#ERR cap off|25M|62M\n"); continue; }
                uint32_t n = (uint32_t)strtoul(t4, NULL, 10);
                uint32_t got = run_cell(m, c0, cap, n);
                printf("#LAT h=%s c0=%s cap=%s thrasharr=%08lX n=%lu\n",
                       t1, t2, t3, (unsigned long)(uintptr_t)thrash_src, (unsigned long)got);
                for (uint32_t i = 0; i < got; i++) printf("%08lX\n", (unsigned long)latbuf[i]);
                printf("#END\n");
            }
        } else if (strcmp(cmd, "UARTCAP") == 0) {
            const char *text = line + strlen("UARTCAP");
            while (*text == ' ') text++;
            char *nl = strpbrk(text, "\r\n"); if (nl) *nl = 0;
            uart_capture(text);
        } else if (strcmp(cmd, "HELP") == 0) {
            printf("LATRUN <s1|s2|s3> <usb|floop|rloop|thrash> <off|25M|62M> <n>\n");
            printf("LATRUN <raw|poll|crit> <n> [thrash] | FIXTURE <n> | UARTCAP <text>\n");
        } else {
            printf("#ERR unknown cmd\n");
        }
    }
}

