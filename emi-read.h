#include "light-modbus/light-modbus-rtu.h"

typedef struct __attribute__ ((__packed__)) {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t hundredthOfSecond;
    uint16_t deviation;
    uint8_t clockStatus;
} emi_clock_t;

char* getOctetString(modbus_t* ctx, uint16_t registerAddress, uint8_t nb);

/**
 * @brief Get the Double From U Int16 object
 * 
 * @param ctx the modbus context.
 * @param registerAddress the register address to read from
 * @param scaller the scaller to apply (* 10 ^ {scaller})
 * @param res the palce to store the result
 * @return the return value from the inner modbus_read_input_registers call.
 */
int getDoubleFromUInt16(modbus_t* ctx, uint16_t registerAddress, signed char scaller, double* res);

/**
 * @brief Get the Double From U Int32 object
 * 
 * @param ctx the modbus context.
 * @param registerAddress the register address to read from
 * @param scaller the scaller to apply (* 10 ^ {scaller})
 * @param res the palce to store the result
 * @return the return value from the inner modbus_read_input_registers call.
 */
int getDoubleFromUInt32(modbus_t* ctx, uint16_t registerAddress, signed char scaler, double* res);

emi_clock_t* getTime(modbus_t* ctx);
int _MQTTClient_publishInt(MQTTClient handle, const char* topicName, int n);
int _MQTTClient_publishDouble(MQTTClient handle, const char* topicName, double n, uint8_t decimals);
int _MQTTClient_publishString(MQTTClient handle, const char* topicName, char* str);
void runContinuously();
void runHourly();
unsigned char getCurrentHour();

void mqtt_connect(MQTTClient client, char** argv);
void mqtt_disconnect(MQTTClient client);