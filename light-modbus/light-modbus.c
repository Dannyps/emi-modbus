#include "light-modbus.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MSG_LENGTH_UNDEFINED -1

void _error_print(modbus_t *ctx, const char *context)
{
    if (ctx->debug)
    {
        fprintf(stderr, "ERROR %s", modbus_strerror(errno));
        if (context != NULL)
        {
            fprintf(stderr, ": %s\n", context);
        }
        else
        {
            fprintf(stderr, "\n");
        }
    }
}

static void _sleep_response_timeout(modbus_t *ctx)
{
    /* Response timeout is always positive */
    /* usleep source code */
    struct timespec request, remaining;
    request.tv_sec = ctx->response_timeout.tv_sec;
    request.tv_nsec = ((long int)ctx->response_timeout.tv_usec) * 1000;
    while (nanosleep(&request, &remaining) == -1 && errno == EINTR)
    {
        request = remaining;
    }
}

/* Get the timeout interval used to wait for a response */
int modbus_get_response_timeout(modbus_t *ctx, uint32_t *to_sec, uint32_t *to_usec)
{
    if (ctx == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    *to_sec = ctx->response_timeout.tv_sec;
    *to_usec = ctx->response_timeout.tv_usec;
    return 0;
}

int modbus_set_response_timeout(modbus_t *ctx, uint32_t to_sec, uint32_t to_usec)
{
    if (ctx == NULL || (to_sec == 0 && to_usec == 0) || to_usec > 999999)
    {
        errno = EINVAL;
        return -1;
    }

    ctx->response_timeout.tv_sec = to_sec;
    ctx->response_timeout.tv_usec = to_usec;
    return 0;
}

/* Computes the length of the expected response */
static unsigned int compute_response_length_from_request(modbus_t *ctx, uint8_t *req, uint8_t size)
{
    int length;
    const int offset = ctx->backend->header_length;

    if (size % 2 == 1)
    {
        size++;
    }

    switch (req[offset])
    {
    case MODBUS_FC_READ_COILS:
    case MODBUS_FC_READ_DISCRETE_INPUTS:
    {
        /* Header + nb values (code from write_bits) */
        int nb = (req[offset + 3] << 8) | req[offset + 4];
        length = 2 + (nb / 8) + ((nb % 8) ? 1 : 0);
    }
    break;
    case MODBUS_FC_WRITE_AND_READ_REGISTERS:
    case MODBUS_FC_READ_HOLDING_REGISTERS:
    case MODBUS_FC_READ_INPUT_REGISTERS:
        /* Header + 2 * nb values */
        length = 2 + size * (req[offset + 3] << 8 | req[offset + 4]);
        break;
    case MODBUS_FC_READ_EXCEPTION_STATUS:
        length = 3;
        break;
    case MODBUS_FC_REPORT_SLAVE_ID:
        /* The response is device specific (the header provides the
           length) */
        return MSG_LENGTH_UNDEFINED;
    case MODBUS_FC_MASK_WRITE_REGISTER:
        length = 7;
        break;
    default:
        length = 5;
    }

    return offset + length + ctx->backend->checksum_length;
}

/* Computes the length to read after the function received */
static uint8_t compute_meta_length_after_function(int function, msg_type_t msg_type)
{
    int length;

    if (msg_type == MSG_INDICATION)
    {
        if (function <= MODBUS_FC_WRITE_SINGLE_REGISTER)
        {
            length = 4;
        }
        else if (function == MODBUS_FC_WRITE_MULTIPLE_COILS || function == MODBUS_FC_WRITE_MULTIPLE_REGISTERS)
        {
            length = 5;
        }
        else if (function == MODBUS_FC_MASK_WRITE_REGISTER)
        {
            length = 6;
        }
        else if (function == MODBUS_FC_WRITE_AND_READ_REGISTERS)
        {
            length = 9;
        }
        else
        {
            /* MODBUS_FC_READ_EXCEPTION_STATUS, MODBUS_FC_REPORT_SLAVE_ID */
            length = 0;
        }
    }
    else
    {
        /* MSG_CONFIRMATION */
        switch (function)
        {
        case MODBUS_FC_WRITE_SINGLE_COIL:
        case MODBUS_FC_WRITE_SINGLE_REGISTER:
        case MODBUS_FC_WRITE_MULTIPLE_COILS:
        case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
            length = 4;
            break;
        case MODBUS_FC_MASK_WRITE_REGISTER:
            length = 6;
            break;
        default:
            length = 1;
        }
    }

    return length;
}

static int
compute_data_length_after_meta(modbus_t *ctx, uint8_t *msg, msg_type_t msg_type)
{
    int function = msg[ctx->backend->header_length];
    int length;

    if (msg_type == MSG_INDICATION)
    {
        switch (function)
        {
        case MODBUS_FC_WRITE_MULTIPLE_COILS:
        case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
            length = msg[ctx->backend->header_length + 5];
            break;
        case MODBUS_FC_WRITE_AND_READ_REGISTERS:
            length = msg[ctx->backend->header_length + 9];
            break;
        default:
            length = 0;
        }
    }
    else
    {
        /* MSG_CONFIRMATION */
        if (function <= MODBUS_FC_READ_INPUT_REGISTERS || function == MODBUS_FC_REPORT_SLAVE_ID || function == MODBUS_FC_WRITE_AND_READ_REGISTERS)
        {
            length = msg[ctx->backend->header_length + 1];
        }
        else
        {
            length = 0;
        }
    }

    length += ctx->backend->checksum_length;

    return length;
}

static int check_confirmation(modbus_t *ctx, uint8_t *req, uint8_t *rsp, uint8_t size, int rsp_length)
{
    int rc;
    int rsp_length_computed;
    const unsigned int offset = ctx->backend->header_length;
    const int function = rsp[offset];

    if (ctx->backend->pre_check_confirmation)
    {
        rc = ctx->backend->pre_check_confirmation(ctx, req, rsp, rsp_length);
        if (rc == -1)
        {
            if (ctx->error_recovery & MODBUS_ERROR_RECOVERY_PROTOCOL)
            {
                _sleep_response_timeout(ctx);
                modbus_flush(ctx);
            }
            return -1;
        }
    }

    rsp_length_computed = compute_response_length_from_request(ctx, req, size);

    /* Exception code */
    if (function >= 0x80)
    {
        if (rsp_length == (int)(offset + 2 + ctx->backend->checksum_length) && req[offset] == (rsp[offset] - 0x80))
        {
            /* Valid exception code received */

            int exception_code = rsp[offset + 1];
            if (exception_code < MODBUS_EXCEPTION_MAX)
            {
                errno = MODBUS_ENOBASE + exception_code;
            }
            else
            {
                errno = EMBBADEXC;
            }
            _error_print(ctx, NULL);
            return -1;
        }
        else
        {
            errno = EMBBADEXC;
            _error_print(ctx, NULL);
            return -1;
        }
    }

    /* Check length */
    if ((rsp_length == rsp_length_computed || rsp_length_computed == MSG_LENGTH_UNDEFINED) && function < 0x80)
    {
        int req_nb_value;
        int rsp_nb_value;
        int resp_addr_ok = TRUE;
        int resp_data_ok = TRUE;

        /* Check function code */
        if (function != req[offset])
        {
            if (ctx->debug)
            {
                fprintf(
                    stderr,
                    "Received function not corresponding to the request (0x%X != 0x%X)\n",
                    function,
                    req[offset]);
            }
            if (ctx->error_recovery & MODBUS_ERROR_RECOVERY_PROTOCOL)
            {
                _sleep_response_timeout(ctx);
                modbus_flush(ctx);
            }
            errno = EMBBADDATA;
            return -1;
        }

        uint8_t paddedSize = (size % 2 == 1) ? size + 1 : size;

        /* Check the number of values is corresponding to the request */
        switch (function)
        {
        case MODBUS_FC_READ_COILS:
        case MODBUS_FC_READ_DISCRETE_INPUTS:
            /* Read functions, 8 values in a byte (nb
             * of values in the request and byte count in
             * the response. */
            req_nb_value = (req[offset + 3] << 8) + req[offset + 4];
            req_nb_value = (req_nb_value / 8) + ((req_nb_value % 8) ? 1 : 0);
            rsp_nb_value = rsp[offset + 1];
            break;
        case MODBUS_FC_WRITE_AND_READ_REGISTERS:
        case MODBUS_FC_READ_HOLDING_REGISTERS:
        case MODBUS_FC_READ_INPUT_REGISTERS:
            /* Read functions 1 value = 2 bytes */
            req_nb_value = (req[offset + 3] << 8) + req[offset + 4];
            rsp_nb_value = (rsp[offset + 1] / paddedSize);
            break;
        case MODBUS_FC_WRITE_MULTIPLE_COILS:
        case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
            /* address in request and response must be equal */
            if ((req[offset + 1] != rsp[offset + 1]) || (req[offset + 2] != rsp[offset + 2]))
            {
                resp_addr_ok = FALSE;
            }
            /* N Write functions */
            req_nb_value = (req[offset + 3] << 8) + req[offset + 4];
            rsp_nb_value = (rsp[offset + 3] << 8) | rsp[offset + 4];
            break;
        case MODBUS_FC_REPORT_SLAVE_ID:
            /* Report slave ID (bytes received) */
            req_nb_value = rsp_nb_value = rsp[offset + 1];
            break;
        case MODBUS_FC_WRITE_SINGLE_COIL:
        case MODBUS_FC_WRITE_SINGLE_REGISTER:
            /* address in request and response must be equal */
            if ((req[offset + 1] != rsp[offset + 1]) || (req[offset + 2] != rsp[offset + 2]))
            {
                resp_addr_ok = FALSE;
            }
            /* data in request and response must be equal */
            if ((req[offset + 3] != rsp[offset + 3]) || (req[offset + 4] != rsp[offset + 4]))
            {
                resp_data_ok = FALSE;
            }
            /* 1 Write functions & others */
            req_nb_value = rsp_nb_value = 1;
            break;
        default:
            /* 1 Write functions & others */
            req_nb_value = rsp_nb_value = 1;
            break;
        }

        if ((req_nb_value == rsp_nb_value) && (resp_addr_ok == TRUE) && (resp_data_ok == TRUE))
        {
            rc = rsp_nb_value;
        }
        else
        {
            if (ctx->debug)
            {
                fprintf(stderr,
                        "Received data not corresponding to the request (%d != %d)\n",
                        rsp_nb_value,
                        req_nb_value);
            }

            if (ctx->error_recovery & MODBUS_ERROR_RECOVERY_PROTOCOL)
            {
                _sleep_response_timeout(ctx);
                modbus_flush(ctx);
            }

            errno = EMBBADDATA;
            rc = -1;
        }
    }
    else
    {
        if (ctx->debug)
        {
            fprintf(
                stderr,
                "Message length not corresponding to the computed length (%d != %d)\n",
                rsp_length,
                rsp_length_computed);
        }
        if (ctx->error_recovery & MODBUS_ERROR_RECOVERY_PROTOCOL)
        {
            _sleep_response_timeout(ctx);
            modbus_flush(ctx);
        }
        errno = EMBBADDATA;
        rc = -1;
    }

    return rc;
}

int _modbus_receive_msg(modbus_t *ctx, uint8_t *msg, msg_type_t msg_type)
{
    int rc;
    fd_set rset;
    struct timeval tv;
    struct timeval *p_tv;
    unsigned int length_to_read;
    int msg_length = 0;
    _step_t step;

    if (ctx->debug)
    {
        if (msg_type == MSG_INDICATION)
        {
            printf("Waiting for an indication...\n");
        }
        else
        {
            printf("Waiting for a confirmation...\n");
        }
    }

    if (!ctx->backend->is_connected(ctx))
    {
        if (ctx->debug)
        {
            fprintf(stderr, "ERROR The connection is not established.\n");
        }
        return -1;
    }

    /* Add a file descriptor to the set */
    FD_ZERO(&rset);
    FD_SET(ctx->s, &rset);

    /* We need to analyse the message step by step.  At the first step, we want
     * to reach the function code because all packets contain this
     * information. */
    step = _STEP_FUNCTION;
    length_to_read = ctx->backend->header_length + 1;

    if (msg_type == MSG_INDICATION)
    {
        /* Wait for a message, we don't know when the message will be
         * received */
        if (ctx->indication_timeout.tv_sec == 0 && ctx->indication_timeout.tv_usec == 0)
        {
            /* By default, the indication timeout isn't set */
            p_tv = NULL;
        }
        else
        {
            /* Wait for an indication (name of a received request by a server, see schema)
             */
            tv.tv_sec = ctx->indication_timeout.tv_sec;
            tv.tv_usec = ctx->indication_timeout.tv_usec;
            p_tv = &tv;
        }
    }
    else
    {
        tv.tv_sec = ctx->response_timeout.tv_sec;
        tv.tv_usec = ctx->response_timeout.tv_usec;
        p_tv = &tv;
    }

    while (length_to_read != 0)
    {
        rc = ctx->backend->select(ctx, &rset, p_tv, length_to_read);
        if (rc == -1)
        {
            _error_print(ctx, "select");
            if (ctx->error_recovery & MODBUS_ERROR_RECOVERY_LINK)
            {
                int saved_errno = errno;
                if (errno == ETIMEDOUT)
                {
                    _sleep_response_timeout(ctx);
                    modbus_flush(ctx);
                }
                else if (errno == EBADF)
                {
                    modbus_close(ctx);
                    modbus_connect(ctx);
                }
                errno = saved_errno;
            }
            return -1;
        }

        rc = ctx->backend->recv(ctx, msg + msg_length, length_to_read);
        if (rc == 0)
        {
            errno = ECONNRESET;
            rc = -1;
        }

        if (rc == -1)
        {
            _error_print(ctx, "read");
            if ((ctx->error_recovery & MODBUS_ERROR_RECOVERY_LINK) && (ctx->backend->backend_type == _MODBUS_BACKEND_TYPE_TCP) && (errno == ECONNRESET || errno == ECONNREFUSED || errno == EBADF))
            {
                int saved_errno = errno;
                modbus_close(ctx);
                modbus_connect(ctx);
                /* Could be removed by previous calls */
                errno = saved_errno;
            }
            return -1;
        }

        /* Display the hex code of each character received */
        if (ctx->debug)
        {
            int i;
            for (i = 0; i < rc; i++)
                printf("<%.2X>", msg[msg_length + i]);
        }

        /* Sums bytes received */
        msg_length += rc;
        /* Computes remaining bytes */
        length_to_read -= rc;

        if (length_to_read == 0)
        {
            switch (step)
            {
            case _STEP_FUNCTION:
                /* Function code position */
                length_to_read = compute_meta_length_after_function(
                    msg[ctx->backend->header_length], msg_type);
                if (length_to_read != 0)
                {
                    step = _STEP_META;
                    break;
                } /* else switches straight to the next step */
            case _STEP_META:
                length_to_read = compute_data_length_after_meta(ctx, msg, msg_type);
                if ((msg_length + length_to_read) > ctx->backend->max_adu_length)
                {
                    errno = EMBBADDATA;
                    _error_print(ctx, "too many data");
                    return -1;
                }
                step = _STEP_DATA;
                break;
            default:
                break;
            }
        }

        if (length_to_read > 0 && (ctx->byte_timeout.tv_sec > 0 || ctx->byte_timeout.tv_usec > 0))
        {
            /* If there is no character in the buffer, the allowed timeout
               interval between two consecutive bytes is defined by
               byte_timeout */
            tv.tv_sec = ctx->byte_timeout.tv_sec;
            tv.tv_usec = ctx->byte_timeout.tv_usec;
            p_tv = &tv;
        }
        /* else timeout isn't set again, the full response must be read before
           expiration of response timeout (for CONFIRMATION only) */
    }

    if (ctx->debug)
        printf("\n");

    return ctx->backend->check_integrity(ctx, msg, msg_length);
}

/* Sends a request/response */
static int send_msg(modbus_t *ctx, uint8_t *msg, int msg_length)
{
    int rc;
    int i;

    msg_length = ctx->backend->send_msg_pre(msg, msg_length);

    if (ctx->debug)
    {
        for (i = 0; i < msg_length; i++)
            printf("[%.2X]", msg[i]);
        printf("\n");
    }

    /* In recovery mode, the write command will be issued until to be
       successful! Disabled by default. */
    do
    {
        rc = ctx->backend->send(ctx, msg, msg_length);
        if (rc == -1)
        {
            _error_print(ctx, NULL);
            if (ctx->error_recovery & MODBUS_ERROR_RECOVERY_LINK)
            {
                int saved_errno = errno;

                if ((errno == EBADF || errno == ECONNRESET || errno == EPIPE))
                {
                    modbus_close(ctx);
                    _sleep_response_timeout(ctx);
                    modbus_connect(ctx);
                }
                else
                {
                    _sleep_response_timeout(ctx);
                    modbus_flush(ctx);
                }
                errno = saved_errno;
            }
        }
    } while ((ctx->error_recovery & MODBUS_ERROR_RECOVERY_LINK) && rc == -1);

    if (rc > 0 && rc != msg_length)
    {
        errno = EMBBADDATA;
        return -1;
    }

    return rc;
}

/* Reads the data from a remote device and put that data into an array */
static int
read_registers(modbus_t *ctx, int function, int addr, int nb, u_int8_t size, void *dest)
{
    int rc;
    int req_length;
    uint8_t req[_MIN_REQ_LENGTH];
    uint8_t rsp[MAX_MESSAGE_LENGTH];

    req_length = ctx->backend->build_request_basis(ctx, function, addr, nb, size, req);

    rc = send_msg(ctx, req, req_length);
    if (rc > 0)
    {
        unsigned int offset;
        int i;

        rc = _modbus_receive_msg(ctx, rsp, MSG_CONFIRMATION);
        if (rc == -1)
            return -1;

        rc = check_confirmation(ctx, req, rsp, size, rc);
        if (rc == -1)
            return -1;

        offset = ctx->backend->header_length;
        uint8_t paddedSize = (size % 2 == 1) ? size + 1 : size;

        for (i = 0; i < rc; i++)
        {
            memcpy((char *)dest + i, rsp + offset + 2 + i * paddedSize, size);
        }
    }

    return rc;
}

const char *modbus_strerror(int errnum)
{
    switch (errnum)
    {
    case EMBXILFUN:
        return "Illegal function";
    case EMBXILADD:
        return "Illegal data address";
    case EMBXILVAL:
        return "Illegal data value";
    case EMBXSFAIL:
        return "Slave device or server failure";
    case EMBXACK:
        return "Acknowledge";
    case EMBXSBUSY:
        return "Slave device or server is busy";
    case EMBXNACK:
        return "Negative acknowledge";
    case EMBXMEMPAR:
        return "Memory parity error";
    case EMBXGPATH:
        return "Gateway path unavailable";
    case EMBXGTAR:
        return "Target device failed to respond";
    case EMBBADCRC:
        return "Invalid CRC";
    case EMBBADDATA:
        return "Invalid data";
    case EMBBADEXC:
        return "Invalid exception code";
    case EMBMDATA:
        return "Too many data";
    case EMBBADSLAVE:
        return "Response not from requested slave";
    default:
        return strerror(errnum);
    }
}

/* Define the slave number */
int modbus_set_slave(modbus_t *ctx, int slave)
{
    if (ctx == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    return ctx->backend->set_slave(ctx, slave);
}

int modbus_connect(modbus_t *ctx)
{
    if (ctx == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    return ctx->backend->connect(ctx);
}

void modbus_close(modbus_t *ctx)
{
    if (ctx == NULL)
        return;

    ctx->backend->close(ctx);
}

void modbus_free(modbus_t *ctx)
{
    if (ctx == NULL)
        return;

    ctx->backend->free(ctx);
}

int modbus_set_debug(modbus_t *ctx, int flag)
{
    if (ctx == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    ctx->debug = flag;
    return 0;
}

int modbus_set_error_recovery(modbus_t *ctx, modbus_error_recovery_mode error_recovery)
{
    if (ctx == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    /* The type of modbus_error_recovery_mode is unsigned enum */
    ctx->error_recovery = (uint8_t)error_recovery;
    return 0;
}

int modbus_flush(modbus_t *ctx)
{
    int rc;

    if (ctx == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    rc = ctx->backend->flush(ctx);
    if (rc != -1 && ctx->debug)
    {
        /* Not all backends are able to return the number of bytes flushed */
        printf("Bytes flushed (%d)\n", rc);
    }
    return rc;
}

int modbus_read_input_registers(
    modbus_t *ctx, int addr, int nb, __uint8_t size, void *dest)
{
    int status;

    if (ctx == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    status = read_registers(ctx, MODBUS_FC_READ_INPUT_REGISTERS, addr, nb, size, dest);

    return status;
}