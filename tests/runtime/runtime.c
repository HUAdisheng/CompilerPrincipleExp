#include <stdint.h>

static volatile uint8_t* const UART0 = (volatile uint8_t*)0x10000000u;
static volatile uint8_t* const UART0_LSR = (volatile uint8_t*)0x10000005u;

static void uart_putc(char ch) {
    while (((*UART0_LSR) & 0x20u) == 0u) {
    }
    *UART0 = (uint8_t)ch;
}

static void uart_puts(const char* str) {
    while (*str != '\0') {
        uart_putc(*str);
        ++str;
    }
}

static void uart_put_uint32(uint32_t value) {
    char buffer[10];
    int index = 0;

    if (value == 0u) {
        uart_putc('0');
        return;
    }

    while (value != 0u) {
        buffer[index++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (index > 0) {
        uart_putc(buffer[--index]);
    }
}

void runtime_report_exit(int code) {
    uart_putc('E');
    uart_putc('X');
    uart_putc('I');
    uart_putc('T');
    uart_putc(':');
    uart_put_uint32((uint32_t)(code & 0xff));
    uart_putc('\n');
}
