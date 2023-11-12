#include "light-modbus.h"

typedef struct _modbus_rtu {
    /* Device: "/dev/ttyS0", "/dev/ttyUSB0" or "/dev/tty.USA19*" on Mac OS X. */
    char *device;
    /* Bauds: 9600, 19200, 57600, 115200, etc */
    int baud;
    /* Data bit */
    uint8_t data_bit;
    /* Stop bit */
    uint8_t stop_bit;
    /* Parity: 'N', 'O', 'E' */
    char parity;
    /* Save old termios settings */
    struct termios old_tios;
    /* To handle many slaves on the same link */
    int confirmation_to_ignore;
} modbus_rtu_t;

/* Timeouts in microsecond (0.5 s) */
#define _RESPONSE_TIMEOUT 500000
#define _BYTE_TIMEOUT     500000

modbus_t* modbus_new_rtu(const char* device, int baud, char parity, int data_bit, int stop_bit);