#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <modbus-rtu.h>

#define SERVER_ID 0x01

int main(int argc, char *argv[])
{
    modbus_t *ctx = NULL;
    uint16_t *tab_rp_bits = NULL;
    uint32_t old_response_to_sec;
    uint32_t old_response_to_usec;
    int rc;
    int server_id = SERVER_ID;

    // UPDATE THE DEVICE NAME AS NECESSARY
    ctx = modbus_new_rtu("/dev/ttyUSB0", 9600, 'N', 8, 1);
    if (ctx == NULL)
    {
        fprintf(stderr, "Could not connect to MODBUS: %s\n", modbus_strerror(errno));
        return -1;
    }

    rc = modbus_set_slave(ctx, server_id);
    if (rc == -1)
    {
        fprintf(stderr, "server_id=%d Invalid slave ID: %s\n", server_id, modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }
    
    modbus_set_debug(ctx, TRUE);

    modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);

    /* Save original timeout */
    modbus_get_response_timeout(ctx, &old_response_to_sec, &old_response_to_usec);

    /* Define a new timeout of 2000ms */
    modbus_set_response_timeout(ctx, 2, 2000000);

    int con = modbus_connect(ctx);
    if (con == -1)
    {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }
    else
    {
        printf("connect returned %d\n", con);
    }

    printf("flushing %d bytes...\n", modbus_flush(ctx));

    tab_rp_bits = (uint16_t *)malloc(8 * sizeof(uint16_t));
    memset(tab_rp_bits, 0, 8 * sizeof(uint16_t));

    while (TRUE)
    {
        rc = modbus_read_registers(ctx, 0x0004, 1, tab_rp_bits);

        if (rc == -1)
        {
            fprintf(stderr, "Failed to modbus_read_input_registers: %s\n", modbus_strerror(errno));
            /* modbus_free(ctx);
            return -1; */
        }

        for (int i = 0; i < 8; i++)
        {
            printf("n: %02d, content: %x\n", i, tab_rp_bits[i]);
        }
        
        sleep(1);
    }

    /* Free the memory */
    free(tab_rp_bits);
    
    /* Close the connection */
    modbus_close(ctx);
    modbus_free(ctx);

    return 0;
}