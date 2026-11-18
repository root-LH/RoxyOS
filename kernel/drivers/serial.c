#include "../include/serial.h"
#include "../include/io.h"

/* 16550 UART register offsets */
#define UART_DATA        0  /* DLAB=0: RX/TX, DLAB=1: divisor low  */
#define UART_IER         1  /* DLAB=0: IER,   DLAB=1: divisor high */
#define UART_FCR         2
#define UART_LCR         3
#define UART_MCR         4
#define UART_LSR         5

#define UART_LSR_THRE    0x20

static int serial_ok = 0;

int serial_init(void)
{
    outb(COM1_PORT + UART_IER, 0x00);   /* disable UART interrupts   */
    outb(COM1_PORT + UART_LCR, 0x80);   /* enable DLAB               */
    outb(COM1_PORT + UART_DATA, 0x01);  /* divisor 1 = 115200 baud   */
    outb(COM1_PORT + UART_IER, 0x00);
    outb(COM1_PORT + UART_LCR, 0x03);   /* 8N1, DLAB off             */
    outb(COM1_PORT + UART_FCR, 0xC7);   /* FIFO on, clear, 14 bytes  */
    outb(COM1_PORT + UART_MCR, 0x1E);   /* loopback mode for self-test */

    outb(COM1_PORT + UART_DATA, 0xAE);

    if (inb(COM1_PORT + UART_DATA) != 0xAE)
    {
        serial_ok = 0;
        return -1;
    }

    outb(COM1_PORT + UART_MCR, 0x0F);   /* normal mode, OUT1/OUT2    */
    serial_ok = 1;

    return 0;
}

int serial_ready(void)
{
    return serial_ok;
}

static void serial_raw_putchar(char c)
{
    /* bounded wait so a dead UART can never hang the kernel */
    for (int i = 0; i < 100000; i++)
    {
        if (inb(COM1_PORT + UART_LSR) & UART_LSR_THRE)
            break;
    }

    outb(COM1_PORT + UART_DATA, (uint8_t)c);
}

void serial_putchar(char c)
{
    if (!serial_ok)
        return;

    if (c == '\n')
        serial_raw_putchar('\r');

    serial_raw_putchar(c);
}

void serial_write(const char *str)
{
    while (*str)
        serial_putchar(*str++);
}
