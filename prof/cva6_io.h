/* cva6_io.h - output + cycle counting that works on BOTH simulators.
 *
 *   Spike            : printf over HTIF works, UART does not
 *   Verilator (RTL)  : UART works, printf does NOT
 *                      (ariane_tb.cpp never services HTIF syscalls -
 *                       it only watches top->exit_o for the exit code)
 *
 * Build with -DUSE_UART for the RTL simulator, without it for Spike.
 */
#ifndef CVA6_IO_H
#define CVA6_IO_H

#include <stdint.h>
#include <stdarg.h>

/* ---------------- cycle / instruction counters ----------------
 * rv32: mcycle is the low word, mcycleh the high word.  Re-read the
 * high word and retry if it changed, otherwise a carry landing between
 * the two reads gives a wildly wrong value.
 * Use mcycle, NOT rdcycle: rdcycle (CSR 0xC00) traps under Spike
 * because the ISA string omits zicntr.
 */
static inline uint64_t rd_mcycle(void) {
#if __riscv_xlen == 32
    uint32_t hi, lo, hi2;
    do {
        __asm__ volatile ("csrr %0, mcycleh" : "=r"(hi));
        __asm__ volatile ("csrr %0, mcycle"  : "=r"(lo));
        __asm__ volatile ("csrr %0, mcycleh" : "=r"(hi2));
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
#else
    uint64_t c;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(c));
    return c;
#endif
}

static inline uint64_t rd_minstret(void) {
#if __riscv_xlen == 32
    uint32_t hi, lo, hi2;
    do {
        __asm__ volatile ("csrr %0, minstreth" : "=r"(hi));
        __asm__ volatile ("csrr %0, minstret"  : "=r"(lo));
        __asm__ volatile ("csrr %0, minstreth" : "=r"(hi2));
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
#else
    uint64_t c;
    __asm__ volatile ("csrr %0, minstret" : "=r"(c));
    return c;
#endif
}

/* ---------------- output back-end ---------------- */
#ifdef USE_UART

#define CVA6_UART_BASE        0x10000000u
#define CVA6_UART_THR         (CVA6_UART_BASE + 0)
#define CVA6_UART_LINE_STATUS (CVA6_UART_BASE + 20)
#define CVA6_UART_LINE_CTRL   (CVA6_UART_BASE + 12)
#define CVA6_UART_DLAB_LSB    (CVA6_UART_BASE + 0)
#define CVA6_UART_DLAB_MSB    (CVA6_UART_BASE + 4)
#define CVA6_UART_FIFO_CTRL   (CVA6_UART_BASE + 8)
#define CVA6_UART_INT_EN      (CVA6_UART_BASE + 4)
#define CVA6_UART_MODEM_CTRL  (CVA6_UART_BASE + 16)

static inline void io_wr8(uintptr_t a, uint8_t v) { *(volatile uint8_t *)a = v; }
static inline uint8_t io_rd8(uintptr_t a)         { return *(volatile uint8_t *)a; }

static inline void io_init(void) {
    /* 8N1, no interrupts.  mock_uart.sv ignores the divisor but the
       real 16550 model does not, so program it anyway. */
    io_wr8(CVA6_UART_INT_EN,     0x00);
    io_wr8(CVA6_UART_LINE_CTRL,  0x80);   /* DLAB on  */
    io_wr8(CVA6_UART_DLAB_LSB,   0x01);
    io_wr8(CVA6_UART_DLAB_MSB,   0x00);
    io_wr8(CVA6_UART_LINE_CTRL,  0x03);   /* DLAB off, 8N1 */
    io_wr8(CVA6_UART_FIFO_CTRL,  0xC7);
    io_wr8(CVA6_UART_MODEM_CTRL, 0x0B);
}

static inline void io_putc(char c) {
    while ((io_rd8(CVA6_UART_LINE_STATUS) & 0x20) == 0) { }  /* THR empty? */
    io_wr8(CVA6_UART_THR, (uint8_t)c);
}

#else   /* Spike: HTIF via syscalls.c */

#include <stdio.h>
static inline void io_init(void) { }
static inline void io_putc(char c) { putchar((int)c); }

#endif

/* ---------------- shared helpers ---------------- */

static inline void io_puts(const char *s) {
    while (*s) io_putc(*s++);
}

/* Unsigned decimal.  No printf dependency: on the UART path there is
   no libc, and on the HTIF path printf buffers until a newline anyway. */
static inline void io_putu64(uint64_t v) {
    char buf[21];
    int i = 0;
    if (v == 0) { io_putc('0'); return; }
    while (v > 0) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) io_putc(buf[--i]);
}

/* label=value, newline-terminated.
   The newline matters: syscalls.c's putchar only flushes on '\n'. */
static inline void io_report(const char *label, uint64_t v) {
    io_puts(label);
    io_putc('=');
    io_putu64(v);
    io_putc('\n');
}

#endif /* CVA6_IO_H */
