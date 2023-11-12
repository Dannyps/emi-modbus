#include <bits/types.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>

#define MODBUS_ENOBASE 112345378

typedef struct _modbus modbus_t;

typedef __uint8_t uint8_t;
typedef __uint16_t uint16_t;
typedef __uint32_t uint32_t;
typedef __uint64_t uint64_t;

typedef struct _sft {
    int slave;
    int function;
    int t_id;
} sft_t;

typedef __ssize_t ssize_t;

typedef long int __fd_mask;

typedef struct _modbus_backend {
    unsigned int backend_type;
    unsigned int header_length;
    unsigned int checksum_length;
    unsigned int max_adu_length;
    int (*set_slave)(modbus_t* ctx, int slave);
    int (*build_request_basis)(
        modbus_t* ctx, int function, int addr, int nb, uint8_t size, uint8_t* req);
    int (*build_response_basis)(sft_t* sft, uint8_t* rsp);
    int (*prepare_response_tid)(const uint8_t* req, int* req_length);
    int (*send_msg_pre)(uint8_t* req, int req_length);
    ssize_t (*send)(modbus_t* ctx, const uint8_t* req, int req_length);
    int (*receive)(modbus_t* ctx, uint8_t* req);
    ssize_t (*recv)(modbus_t* ctx, uint8_t* rsp, int rsp_length);
    int (*check_integrity)(modbus_t* ctx, uint8_t* msg, const int msg_length);
    int (*pre_check_confirmation)(modbus_t* ctx,
        const uint8_t* req,
        const uint8_t* rsp,
        int rsp_length);
    int (*connect)(modbus_t* ctx);
    unsigned int (*is_connected)(modbus_t* ctx);
    void (*close)(modbus_t* ctx);
    int (*flush)(modbus_t* ctx);
    int (*select)(modbus_t* ctx, fd_set* rset, struct timeval* tv, int msg_length);
    void (*free)(modbus_t* ctx);
} modbus_backend_t;

struct _modbus {
    /* Slave address */
    int slave;
    /* Socket or file descriptor */
    int s;
    int debug;
    int error_recovery;
    int quirks;
    struct timeval response_timeout;
    struct timeval byte_timeout;
    struct timeval indication_timeout;
    const modbus_backend_t* backend;
    void* backend_data;
};

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef OFF
#define OFF 0
#endif

#ifndef ON
#define ON 1
#endif

typedef enum {
    MODBUS_ERROR_RECOVERY_NONE = 0,
    MODBUS_ERROR_RECOVERY_LINK = (1 << 1),
    MODBUS_ERROR_RECOVERY_PROTOCOL = (1 << 2)
} modbus_error_recovery_mode;

typedef enum {
    _MODBUS_BACKEND_TYPE_RTU = 0,
    _MODBUS_BACKEND_TYPE_TCP
} modbus_backend_type_t;

#define _MODBUS_RTU_HEADER_LENGTH 1
#define _MODBUS_RTU_PRESET_REQ_LENGTH 6
#define _MODBUS_RTU_PRESET_RSP_LENGTH 2
#define _MODBUS_RTU_CHECKSUM_LENGTH 2
#define MODBUS_RTU_MAX_ADU_LENGTH 256

typedef enum {
    MODBUS_QUIRK_NONE = 0,
    MODBUS_QUIRK_MAX_SLAVE = (1 << 1),
    MODBUS_QUIRK_REPLY_TO_BROADCAST = (1 << 2),
    MODBUS_QUIRK_ALL = 0xFF
} modbus_quirks;

/*
 *  ---------- Request     Indication ----------
 *  | Client | ---------------------->| Server |
 *  ---------- Confirmation  Response ----------
 */
typedef enum {
    /* Request message on the server side */
    MSG_INDICATION,
    /* Request message on the client side */
    MSG_CONFIRMATION
} msg_type_t;

#define MODBUS_BROADCAST_ADDRESS 0

/* Protocol exceptions */
enum {
    MODBUS_EXCEPTION_ILLEGAL_FUNCTION = 0x01,
    MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS,
    MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE,
    MODBUS_EXCEPTION_SLAVE_OR_SERVER_FAILURE,
    MODBUS_EXCEPTION_ACKNOWLEDGE,
    MODBUS_EXCEPTION_SLAVE_OR_SERVER_BUSY,
    MODBUS_EXCEPTION_NEGATIVE_ACKNOWLEDGE,
    MODBUS_EXCEPTION_MEMORY_PARITY,
    MODBUS_EXCEPTION_NOT_DEFINED,
    MODBUS_EXCEPTION_GATEWAY_PATH,
    MODBUS_EXCEPTION_GATEWAY_TARGET,
    MODBUS_EXCEPTION_MAX
};

#define EMBXILFUN (MODBUS_ENOBASE + MODBUS_EXCEPTION_ILLEGAL_FUNCTION)
#define EMBXILADD (MODBUS_ENOBASE + MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS)
#define EMBXILVAL (MODBUS_ENOBASE + MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE)
#define EMBXSFAIL (MODBUS_ENOBASE + MODBUS_EXCEPTION_SLAVE_OR_SERVER_FAILURE)
#define EMBXACK (MODBUS_ENOBASE + MODBUS_EXCEPTION_ACKNOWLEDGE)
#define EMBXSBUSY (MODBUS_ENOBASE + MODBUS_EXCEPTION_SLAVE_OR_SERVER_BUSY)
#define EMBXNACK (MODBUS_ENOBASE + MODBUS_EXCEPTION_NEGATIVE_ACKNOWLEDGE)
#define EMBXMEMPAR (MODBUS_ENOBASE + MODBUS_EXCEPTION_MEMORY_PARITY)
#define EMBXGPATH (MODBUS_ENOBASE + MODBUS_EXCEPTION_GATEWAY_PATH)
#define EMBXGTAR (MODBUS_ENOBASE + MODBUS_EXCEPTION_GATEWAY_TARGET)

/* Modbus function codes */
#define MODBUS_FC_READ_COILS               0x01
#define MODBUS_FC_READ_DISCRETE_INPUTS     0x02
#define MODBUS_FC_READ_HOLDING_REGISTERS   0x03
#define MODBUS_FC_READ_INPUT_REGISTERS     0x04
#define MODBUS_FC_WRITE_SINGLE_COIL        0x05
#define MODBUS_FC_WRITE_SINGLE_REGISTER    0x06
#define MODBUS_FC_READ_EXCEPTION_STATUS    0x07
#define MODBUS_FC_WRITE_MULTIPLE_COILS     0x0F
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS 0x10
#define MODBUS_FC_REPORT_SLAVE_ID          0x11
#define MODBUS_FC_MASK_WRITE_REGISTER      0x16
#define MODBUS_FC_WRITE_AND_READ_REGISTERS 0x17

/* Native libmodbus error codes */
#define EMBBADCRC (EMBXGTAR + 1)
#define EMBBADDATA (EMBXGTAR + 2)
#define EMBBADEXC (EMBXGTAR + 3)
#define EMBUNKEXC (EMBXGTAR + 4)
#define EMBMDATA (EMBXGTAR + 5)
#define EMBBADSLAVE (EMBXGTAR + 6)

#define _MIN_REQ_LENGTH 12
#define MAX_MESSAGE_LENGTH 260

typedef enum {
    _STEP_FUNCTION,
    _STEP_META,
    _STEP_DATA
} _step_t;

const char* modbus_strerror(int errnum);
int modbus_set_slave(modbus_t* ctx, int slave);
int modbus_connect(modbus_t* ctx);
void modbus_close(modbus_t* ctx);
void modbus_free(modbus_t* ctx);
int modbus_set_debug(modbus_t* ctx, int flag);
int modbus_set_error_recovery(modbus_t* ctx, modbus_error_recovery_mode error_recovery);
int modbus_flush(modbus_t* ctx);
int modbus_read_input_registers(modbus_t* ctx, int addr, int nb, __uint8_t size, void* dest);
int modbus_get_response_timeout(modbus_t *ctx, uint32_t *to_sec, uint32_t *to_usec);
int modbus_set_response_timeout(modbus_t *ctx, uint32_t to_sec, uint32_t to_usec);
int _modbus_receive_msg(modbus_t* ctx, uint8_t* msg, msg_type_t msg_type);